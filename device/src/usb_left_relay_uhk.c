/* UHK adapter: the portable relay runs only on the main event loop. */
#include "usb_left_relay_uhk.h"
#include "connections.h"
#include "bt_conn.h"
#include "device.h"
#include "event_scheduler.h"
#include "host_connection.h"
#include "link_protocol.h"
#include "messenger.h"
#include "shared/slave_protocol.h"
#include "timer.h"
#include "usb_scheduler.h"
#include "usb_semaphore.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>

_Static_assert(
    USB_LEFT_RELAY_MAX_PACKET + 4 <= MAX_LINK_PACKET_LENGTH, "relay exceeds Messenger MTU");
static relay_t relay;
static atomic_t uartIngressGeneration, bleIngressGeneration;
static uint8_t observedConnection;
static uint32_t observedGeneration;
#if DEVICE_IS_UHK80_RIGHT
static uint32_t feedbackRevision, observedLocalUsbGeneration;
#endif
static bool initialized;
static int provisionResult;
static struct k_spinlock completionsLock;
static struct {
    uint64_t token;
    uint32_t sequence;
    bool ready, success;
} completions[RelayKind_Count + 1];
#define HALF_UART (DEVICE_IS_UHK80_RIGHT ? ConnectionId_UartLeft : ConnectionId_UartRight)
#define HALF_BLE (DEVICE_IS_UHK80_RIGHT ? ConnectionId_NusServerLeft : ConnectionId_NusClientRight)
#define HALF_PEER_ID (DEVICE_IS_UHK80_RIGHT ? PeerIdLeft : PeerIdRight)
#define HALF_PEER (DEVICE_IS_UHK80_RIGHT ? DeviceId_Uhk80_Left : DeviceId_Uhk80_Right)

uint8_t UsbLeft_LinkConnection(void)
{
    if (Connections_IsReady(HALF_UART)) {
        return HALF_UART;
    }
    /* The existing NUS connection belongs to the paired other half. Wait for
     * encryption and enough ATT payload space for every relay message. */
    struct bt_conn *conn = Peers[HALF_PEER_ID].conn;
    if (Connections_IsReady(HALF_BLE) && conn &&
        bt_conn_get_security(conn) >= BT_SECURITY_L2 &&
        bt_gatt_get_mtu(conn) >= USB_LEFT_RELAY_MAX_PACKET + 4 + 3) {
        return HALF_BLE;
    }
    return ConnectionId_Invalid;
}

/* Existing UART/NUS send paths wait for transport slots. Keep
 * relay traffic in a bounded worker queue instead of blocking main-loop scans. */
typedef struct {
    uint32_t generation;
    uint8_t connection;
    uint8_t length;
    uint8_t data[USB_LEFT_RELAY_MAX_PACKET];
} relay_tx_t;
K_MSGQ_DEFINE(relayTxQueue, sizeof(relay_tx_t), 4, 4);
K_THREAD_STACK_DEFINE(relayTxStack, 2048);
static struct k_work_q relayTxWorkQueue;
static void transmitWorker(struct k_work *work)
{
    (void)work;
    relay_tx_t packet;
    while (!k_msgq_get(&relayTxQueue, &packet, K_NO_WAIT)) {
        message_t msg = {.data = packet.data,
            .len = packet.length,
            .messageId = {MessageId_UsbLeftRelay},
            .idsUsed = 1,
            .src = DEVICE_ID,
            .dst = HALF_PEER,
            .connectionId = packet.connection};
        uint32_t started = Timer_GetCurrentTime();
        for (;;) {
            if (packet.connection != UsbLeft_LinkConnection() ||
                packet.generation != UsbLeft_IngressGeneration(packet.connection)) {
                break;
            }
            int result = Messenger_SendMessage(&msg);
            if ((result != -EAGAIN && result != -EBUSY && result != -EALREADY && result != -ENOMEM) ||
                (uint32_t)(Timer_GetCurrentTime() - started) >= RELAY_REPORT_RETRY_MS) {
                break;
            }
            /* Keep ownership of these bytes while the shared NUS slot is busy.
             * Yield only this dedicated worker; never stall Bluetooth callbacks
             * or retarget the packet if the preferred connection changes. */
            k_sleep(K_MSEC(2));
        } // Longer loss still uses the protocol's immutable retries/deadline.
    }
    EventVector_WakeMain();
}
K_WORK_DEFINE(relayTxWork, transmitWorker);
static bool sendPacket(void *user, const uint8_t *data, size_t len)
{
    (void)user;
    uint8_t connection = UsbLeft_LinkConnection();
    if (connection == ConnectionId_Invalid || connection != observedConnection ||
        observedGeneration != UsbLeft_IngressGeneration(connection) ||
        len > USB_LEFT_RELAY_MAX_PACKET) {
        return false;
    }
    relay_tx_t packet = {
        .generation = observedGeneration, .connection = connection, .length = len};
    memcpy(packet.data, data, len);
    if (k_msgq_put(&relayTxQueue, &packet, K_NO_WAIT)) {
        return false;
    }
    /* Bluetooth completion work uses the system queue. A transport slot wait
     * must never prevent that queue from releasing the slot we need. */
    (void)k_work_submit_to_queue(&relayTxWorkQueue, &relayTxWork);
    return true;
}
static int submit(void *user, uint8_t kind, const uint8_t *data, uint64_t token, uint32_t seq)
{
    (void)user;
    return Hid_LocalUsbSubmit(kind, data, token, seq, relay.usb_generation);
}
static bool quiesce(void *user)
{
    (void)user;
    return Hid_LocalUsbQuiesce();
}
static bool wake(void *user)
{
    (void)user;
    return Hid_LocalUsbWakeAllowed() && USB_RemoteWakeup();
}
static uint64_t nonce(void *user)
{
    (void)user;
    uint64_t value;
    sys_rand_get(&value, sizeof(value));
    return value ? value : 1;
}
static void changed(void *user)
{
    (void)user;
    EventVector_WakeMain();
}
static void delivered(void *user, uint8_t kind)
{
    (void)user;
    report_send_state_t *states[] = {
        &UsbSemaphore.keyboard, &UsbSemaphore.mouse, &UsbSemaphore.controls};
    /* Relay_Receive already matched this DONE to the active token and pending
     * sequence. The legacy sender may clear inFlight after an earlier give-up;
     * that does not revoke the relay's ownership or its delivery confirmation.
     * Pending relay reports block rebuilds, so advancing this baseline is safe. */
    if (kind && kind <= RelayKind_Count) {
        UsbSemaphore_Release(states[kind - 1]);
        UsbScheduler_ReportDelivered(ReportSink_UsbLeft);
    }
}
bool UsbLeft_Initialized(void)
{
    return initialized;
}
void UsbLeft_Init(void)
{
    k_work_queue_start(&relayTxWorkQueue, relayTxStack, K_THREAD_STACK_SIZEOF(relayTxStack),
        K_PRIO_PREEMPT(8), NULL);
    k_thread_name_set(&relayTxWorkQueue.thread, "usbLeftRelayTx");
    relay_platform_t platform = {sendPacket, submit, quiesce, wake, nonce, delivered, changed};
    Relay_Init(&relay, DEVICE_IS_UHK80_RIGHT, &platform, NULL);
    initialized = true;
}
uint32_t UsbLeft_IngressGeneration(uint8_t connection)
{
    return connection == HALF_UART ? atomic_get(&uartIngressGeneration)
         : connection == HALF_BLE ? atomic_get(&bleIngressGeneration) : 0;
}
void UsbLeft_LinkChanged(uint8_t connection)
{
    if (connection == HALF_UART || connection == HALF_BLE) {
        atomic_inc(connection == HALF_UART ? &uartIngressGeneration : &bleIngressGeneration);
        EventVector_WakeMain();
    }
}
void UsbLeft_LocalComplete(uint8_t kind, uint64_t token, uint32_t sequence, bool success)
{
    if (!kind || kind > RelayKind_Count) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&completionsLock);
    completions[kind].token = token;
    completions[kind].sequence = sequence;
    completions[kind].success = success;
    completions[kind].ready = true;
    k_spin_unlock(&completionsLock, key);
    EventVector_WakeMain();
}
void UsbLeft_Receive(
    uint8_t source, uint8_t connection, uint32_t generation, const uint8_t *packet, size_t len)
{
    if (!initialized || source != HALF_PEER || connection == ConnectionId_Invalid ||
        connection != UsbLeft_LinkConnection() || connection != observedConnection ||
        generation != UsbLeft_IngressGeneration(connection) || generation != observedGeneration ||
        len > USB_LEFT_RELAY_MAX_PACKET) {
        return;
    }
    Relay_Receive(&relay, packet, len);
}
void UsbLeft_Process(void)
{
    if (!initialized) {
        return;
    }
    Hid_LocalUsbService();
    Hid_LocalUsbDrainCompletions();
    uint8_t connection = UsbLeft_LinkConnection();
    uint32_t generation = UsbLeft_IngressGeneration(connection);
    if (observedConnection != connection || observedGeneration != generation) {
        Relay_SetLink(&relay, false);
        observedConnection = connection;
        observedGeneration = generation;
    }
    Relay_SetLink(&relay, connection != ConnectionId_Invalid);
#if DEVICE_IS_UHK80_LEFT
    relay_usb_state_t state = {0};
    uint32_t usbGeneration;
    Hid_LocalUsbSnapshot(&usbGeneration, &state);
    Relay_SetUsb(&relay, usbGeneration, &state);
#endif
    for (uint8_t kind = 1; kind <= RelayKind_Count; ++kind) {
        k_spinlock_key_t key = k_spin_lock(&completionsLock);
        bool ready = completions[kind].ready, success = completions[kind].success;
        uint64_t token = completions[kind].token;
        uint32_t seq = completions[kind].sequence;
        completions[kind].ready = false;
        k_spin_unlock(&completionsLock, key);
        if (ready) {
            Relay_UsbComplete(&relay, kind, token, seq, success);
        }
    }
    Relay_Tick(&relay, Timer_GetCurrentTime());
#if DEVICE_IS_UHK80_RIGHT
    Connections_SetStateAsync(
        ConnectionId_UsbHidLeft, !UsbLeft_Connected() ? ConnectionState_Disconnected
                                 : UsbLeft_Awake()    ? ConnectionState_Ready
                                                      : ConnectionState_Connected);
    uint32_t localGeneration = Hid_LocalUsbGeneration();
    if (localGeneration != observedLocalUsbGeneration) {
        observedLocalUsbGeneration = localGeneration;
        if (Connections_Type(CurrentHostConnectionId) == ConnectionType_UsbHidRight) {
            HostRoute_LocalUsbChanged();
        }
    }
    HostRoute_Process();
    // A committed host or inter-half transport can change here without another
    // switchover callback. The per-peer cache makes unchanged profiles a no-op.
    BtConn_UpdateConnectionLatencies();
    if (feedbackRevision != relay.usb.revision) {
        feedbackRevision = relay.usb.revision;
        if (Connections_Type(CurrentHostConnectionId) == ConnectionType_UsbHidLeft) {
            Hid_UpdateKeyboardLedsState();
        }
    }
#endif
    EventScheduler_Schedule(
        Timer_GetCurrentTime() + 5, EventSchedulerEvent_UsbLeftRelay, "USB left relay");
}
#if DEVICE_IS_UHK80_RIGHT
static unsigned leftSlotCount(void)
{
    unsigned count = 0;
    for (uint8_t i = 0; i < SERIALIZED_HOST_CONNECTION_COUNT_MAX; ++i) {
        if (HostConnections[i].type == HostConnectionType_UsbHidLeft) {
            ++count;
        }
    }
    return count;
}
#endif
bool UsbLeft_Connected(void)
{
#if DEVICE_IS_UHK80_RIGHT
    if (leftSlotCount() != 1) {
        return false;
    }
#endif
    return Relay_Available(&relay);
}
bool UsbLeft_Awake(void)
{
    return UsbLeft_Connected() && Relay_Awake(&relay);
}
bool UsbLeft_Active(void)
{
    return relay.active && UsbLeft_Connected();
}
bool UsbLeft_Quiescent(void)
{
    return relay.quiescent;
}
bool UsbLeft_Pending(void)
{
    for (uint8_t i = 0; i < RelayKind_Count; ++i) {
        if (relay.slots[i].pending) {
            return true;
        }
    }
    return false;
}
void UsbLeft_Select(bool selected)
{
#if DEVICE_IS_UHK80_RIGHT
    selected = selected && leftSlotCount() == 1;
#endif
    Relay_Select(&relay, selected);
}
const relay_t *UsbLeft_GetState(void)
{
    return &relay;
}
bool UsbLeft_RequestWake(void)
{
    return UsbLeft_Connected() && Relay_RequestWake(&relay);
}
uint32_t UsbLeft_WakeEpoch(void)
{
    return relay.wake_epoch;
}
int UsbLeft_SendKeyboard(const hid_keyboard_report_t *report)
{
    _Static_assert(
        HID_KEYBOARD_MIN_BITFIELD_SCANCODE == 0x04 && HID_KEYBOARD_MAX_BITFIELD_SCANCODE == 0xdd,
        "keyboard usage schema changed");
    _Static_assert(sizeof(*report) == USB_LEFT_RELAY_KEYBOARD_SIZE, "keyboard schema changed");
    return Relay_SendReport(&relay, RelayKind_Keyboard, (const uint8_t *)report) ? 0
                                                                                 : -EHOSTUNREACH;
}
static void put16(uint8_t *p, uint16_t value)
{
    p[0] = value;
    p[1] = value >> 8;
}
int UsbLeft_SendMouse(const hid_mouse_report_t *report)
{
    _Static_assert(sizeof(*report) == USB_LEFT_RELAY_MOUSE_SIZE, "mouse schema changed");
    uint8_t data[USB_LEFT_RELAY_MOUSE_SIZE] = {
        report->buttons, report->buttons >> 8, report->buttons >> 16};
    put16(data + 3, report->x);
    put16(data + 5, report->y);
    put16(data + 7, report->wheelY);
    put16(data + 9, report->wheelX);
    return Relay_SendReport(&relay, RelayKind_Mouse, data) ? 0 : -EHOSTUNREACH;
}
int UsbLeft_SendControls(const hid_controls_report_t *report)
{
    _Static_assert(sizeof(*report) == USB_LEFT_RELAY_CONTROLS_SIZE, "controls schema changed");
    uint8_t data[USB_LEFT_RELAY_CONTROLS_SIZE];
    for (uint8_t i = 0; i < 4; ++i) {
        put16(data + 2 * i, report->consumer[i]);
    }
    data[8] = report->system[0];
    data[9] = report->system[1];
    return Relay_SendReport(&relay, RelayKind_Controls, data) ? 0 : -EHOSTUNREACH;
}
int UsbLeft_ProvisionResult(void)
{
    return provisionResult;
}
void UsbLeft_Provision(void)
{
#if DEVICE_IS_UHK80_RIGHT
    if (leftSlotCount() > 1) {
        provisionResult = -EEXIST;
        return;
    }
    for (uint8_t i = 0; i < SERIALIZED_HOST_CONNECTION_COUNT_MAX; ++i) {
        if (HostConnections[i].type == HostConnectionType_UsbHidLeft) {
            provisionResult = ConnectionId_HostConnectionFirst + i;
            return;
        }
    }
    for (uint8_t i = 0; i < SERIALIZED_HOST_CONNECTION_COUNT_MAX; ++i) {
        if (HostConnections[i].type != HostConnectionType_Empty) {
            continue;
        }
        static const char name[] = "USB Left";
        HostConnections[i] = (host_connection_t){.type = HostConnectionType_UsbHidLeft,
            .name = {.start = name, .end = name + sizeof(name) - 1},
            .switchover = false};
        provisionResult = ConnectionId_HostConnectionFirst + i;
        return;
    }
    provisionResult = -ENOSPC;
#else
    provisionResult = -ENOTSUP;
#endif
}
