#include "usb_left_relay.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

// Actual UHK selection, queue, receive, generation observation and Messenger
// dispatch code is extracted by the runner. Only Zephyr/radio/USB plumbing is fake.
#define JOIN_INNER(a,b) a##b
#define JOIN(a,b) JOIN_INNER(a,b)
#define API(name) JOIN(TEST_ROLE,name)
#define UsbLeft_LinkConnection API(_LinkConnection)
#define UsbLeft_IngressGeneration API(_IngressGeneration)
#define UsbLeft_LinkChanged API(_LinkChanged)
#define UsbLeft_Receive API(_Receive)
#define DEVICE_IS_UHK80_LEFT (!DEVICE_IS_UHK80_RIGHT)
#define DEVICE_IS_KEYBOARD 1
#define CONFIG_UHK_USB_LEFT_RELAY 1
#define CONFIG_BT_NUS 1
#define CONFIG_BT_PERIPHERAL 1
#define CONFIG_BT_NUS_CLIENT 1
#define MAX_LINK_PACKET_LENGTH 128
#define K_NO_WAIT 0
#define K_MSEC(ms) (ms)
#define BT_SECURITY_L2 2
#define DEBUG_LOG_MESSAGES 0
enum {
    ConnectionId_Invalid, ConnectionId_HostConnectionFirst,
    ConnectionId_HostConnectionLast, ConnectionId_UartLeft, ConnectionId_UartRight,
    ConnectionId_NusServerLeft, ConnectionId_NusServerRight, ConnectionId_NusClientRight,
};
enum { ConnectionType_NusDongle, DeviceId_Uhk80_Left = 20, DeviceId_Uhk80_Right = 21 };
enum { PeerIdLeft = 1, PeerIdRight = 2, MessageId_UsbLeftRelay = 8 };
#define DEVICE_ID (DEVICE_IS_UHK80_RIGHT ? DeviceId_Uhk80_Right : DeviceId_Uhk80_Left)
typedef uint8_t connection_id_t;
typedef uint8_t device_id_t;
typedef int atomic_t;
static int atomic_get(atomic_t *p) { return *p; }
static void atomic_inc(atomic_t *p) { ++*p; }
static bool ready[8];
static bool Connections_IsReady(uint8_t c) { return ready[c]; }
static int Connections_Type(uint8_t c) { (void)c; return ConnectionType_NusDongle; }
static struct { uint8_t peerId; } Connections[8];
struct bt_conn { unsigned mtu, security; };
static struct bt_conn peer = {131, BT_SECURITY_L2};
static struct { struct bt_conn *conn; } Peers[3];
static unsigned bt_conn_get_security(struct bt_conn *c) { return c->security; }
static unsigned bt_gatt_get_mtu(struct bt_conn *c) { return c->mtu; }
static void EventVector_WakeMain(void) {}
static relay_t relay;
static uint32_t observedGeneration;
static uint8_t observedConnection;
static atomic_t uartIngressGeneration, bleIngressGeneration;
static bool initialized;
static unsigned busyAttempts, attemptCount;
static uint32_t workerTime;
static bool disconnectDuringBusy;
static uint32_t Timer_GetCurrentTime(void) { return workerTime; }
static void k_sleep(unsigned ms) {
    workerTime+=ms;
    if (disconnectDuringBusy) {
        ready[ConnectionId_UartLeft]=ready[ConnectionId_UartRight]=false;
        ready[ConnectionId_NusServerLeft]=ready[ConnectionId_NusClientRight]=false;
    }
}
typedef struct {
    const uint8_t *data;
    uint16_t len;
    uint8_t messageId[2], idsUsed, src, dst, wm, connectionId;
} message_t;
void TestWireSend(bool right, uint8_t connection, const uint8_t *data, size_t len);
static int sendWire(message_t *msg) {
    ++attemptCount;
    if (busyAttempts) { --busyAttempts; return -EAGAIN; }
    assert(msg->src == DEVICE_ID);
    assert(msg->dst == (DEVICE_IS_UHK80_RIGHT ? DeviceId_Uhk80_Left : DeviceId_Uhk80_Right));
    assert(msg->idsUsed == 1 && msg->messageId[0] == MessageId_UsbLeftRelay);
    TestWireSend(DEVICE_IS_UHK80_RIGHT, msg->connectionId, msg->data, msg->len);
    return 0;
}
static int UartBridge_SendMessage(message_t *m) { return sendWire(m); }
static int NusClient_SendMessage(message_t *m) { return sendWire(m); }
static int NusServer_SendMessageTo(message_t *m, struct bt_conn *c) {
    assert(c == &peer); // Relay notifications must address the paired right half.
    return sendWire(m);
}
static int NusServer_SendMessage(message_t *m) {
    (void)m; assert(false && "relay must not broadcast to NUS subscribers"); return -1;
}
static const char *Utils_DeviceIdToString(uint8_t id) { (void)id; return "fake"; }
static void getMessageDescription(uint8_t a, uint8_t b, const char **x, const char **y) {
    (void)a; (void)b; *x = "relay"; *y = "";
}
static void LogU(const char *fmt, ...) { (void)fmt; }
static void Trace(char c) { (void)c; }
#define printk LogU
static int Messenger_SendMessage(message_t *message);
// The generated production method has external linkage within this TU; the
// prior static declaration keeps both endpoint copies isolated in the executable.
#include "relay_messenger_under_test.inc"

struct k_work { int unused; };
struct k_work_q { int unused; };
#define K_THREAD_STACK_DEFINE(name,size) static uint8_t name[size] __attribute__((unused))
static bool workerPending;
static int k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work) {
    assert(queue != NULL); // Relay TX must not run on the Bluetooth completion system queue.
    (void)work; workerPending = true; return 0;
}
struct fake_queue { uint8_t data[4][80]; size_t size; unsigned head, tail; };
#define K_MSGQ_DEFINE(name,type_size,count,alignment) static struct fake_queue name = {.size = type_size}
#define K_WORK_DEFINE(name,callback) static struct k_work name
static int k_msgq_put(struct fake_queue *q, const void *packet, int wait) {
    (void)wait;
    assert(q->size <= sizeof(q->data[0]));
    if (q->tail - q->head == 4) return -ENOSPC;
    memcpy(q->data[q->tail++ % 4], packet, q->size); return 0;
}
static int k_msgq_get(struct fake_queue *q, void *packet, int wait) {
    (void)wait;
    if (q->tail == q->head) return -ENOENT;
    memcpy(packet, q->data[q->head++ % 4], q->size); return 0;
}
uint32_t UsbLeft_IngressGeneration(uint8_t c);
#include "relay_transport_under_test.inc"

static struct { uint64_t token; uint32_t sequence; uint8_t bytes[29]; bool pending; } usb[3];
static bool blockDrain;
static uint8_t deliveredBytes[29];
static unsigned deliveredReports;
static uint64_t nextNonce;
static int submit(void *user, uint8_t kind, const uint8_t *data, uint64_t token, uint32_t seq) {
    (void)user;
    if (usb[kind-1].pending) return -EBUSY;
    usb[kind-1].pending = true; usb[kind-1].token = token; usb[kind-1].sequence = seq;
    memcpy(usb[kind-1].bytes, data, RelayProtocol_ReportSize(kind)); return 0;
}
static bool quiesce(void *user) {
    (void)user;
    if (blockDrain) return false;
    for (unsigned i=0; i<3; ++i) usb[i].pending = false;
    return true;
}
static bool wake(void *user) { (void)user; return true; }
static uint64_t nonce(void *user) { (void)user; return ++nextNonce; }
static void delivered(void *user, uint8_t kind) { (void)user; (void)kind; ++deliveredReports; }
static void changed(void *user) { (void)user; }
void API(_Reset)(bool bridge, bool bluetooth) {
    memset(ready, 0, sizeof(ready)); memset(usb, 0, sizeof(usb));
    memset(deliveredBytes, 0, sizeof(deliveredBytes));
    memset(Connections, 0, sizeof(Connections));
    uartIngressGeneration = bleIngressGeneration = 1;
    observedConnection = ConnectionId_Invalid; observedGeneration = 0;
    relayTxQueue.head = relayTxQueue.tail = 0;
    peer.mtu = 131; peer.security = BT_SECURITY_L2;
    Peers[HALF_PEER_ID].conn = &peer;
    ready[HALF_UART] = bridge; ready[HALF_BLE] = bluetooth;
    blockDrain = workerPending = false; deliveredReports = 0;
    workerTime = busyAttempts = attemptCount = 0; disconnectDuringBusy = false;
    nextNonce = DEVICE_IS_UHK80_RIGHT ? 1000 : 2000;
    relay_platform_t platform = {sendPacket, submit, quiesce, wake, nonce, delivered, changed};
    Relay_Init(&relay, DEVICE_IS_UHK80_RIGHT, &platform, NULL);
    initialized = true;
    relay_usb_state_t state = {.flags=RelayFlag_Configured, .vertical=256, .horizontal=256};
    Relay_SetUsb(&relay, 1, &state);
}
void API(_Tick)(uint32_t now) {
#include "relay_link_observation_under_test.inc"
    Relay_Tick(&relay, now);
}
void API(_Worker)(void) { if (workerPending) { workerPending=false; transmitWorker(&relayTxWork); } }
void API(_CompleteUsb)(void) {
    for (unsigned i=0; i<3; ++i) if (usb[i].pending && !blockDrain) {
        usb[i].pending=false;
        if (i==0) memcpy(deliveredBytes, usb[i].bytes, 29);
        Relay_UsbComplete(&relay, i+1, usb[i].token, usb[i].sequence, true);
    }
}
void API(_Ready)(bool bridge, bool bluetooth) {
    if (ready[HALF_UART] != bridge) UsbLeft_LinkChanged(HALF_UART);
    if (ready[HALF_BLE] != bluetooth) UsbLeft_LinkChanged(HALF_BLE);
    ready[HALF_UART]=bridge; ready[HALF_BLE]=bluetooth;
}
void API(_BleState)(unsigned mtu, unsigned security) { peer.mtu=mtu; peer.security=security; }
void API(_BlockDrain)(bool block) { blockDrain=block; }
void API(_Select)(bool selected) { Relay_Select(&relay, selected); }
bool API(_Send)(const uint8_t *report) { return Relay_SendReport(&relay, 1, report); }
const relay_t *API(_State)(void) { return &relay; }
const uint8_t *API(_Delivered)(void) { return deliveredBytes; }
unsigned API(_Deliveries)(void) { return deliveredReports; }
void API(_Busy)(unsigned attempts, bool disconnect) {
    busyAttempts=attempts; attemptCount=0; disconnectDuringBusy=disconnect;
}
unsigned API(_Attempts)(void) { return attemptCount; }
uint8_t API(_PeerSource)(void) { return HALF_PEER; }
uint8_t API(_Uart)(void) { return HALF_UART; }
uint8_t API(_Ble)(void) { return HALF_BLE; }
