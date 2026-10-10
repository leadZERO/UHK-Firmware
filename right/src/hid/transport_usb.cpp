extern "C" {
#include "debug.h"
#ifdef CONFIG_UHK_USB_LEFT_RELAY
    #include "connections.h"
    #include "usb_left_relay_uhk.h"
#include "event_scheduler.h"
#endif
#include "device.h"
#include "key_states.h"
#include "logger.h"
#include "power_mode.h"
#include "timer.h"
#include "usb_report_updater.h"
#include "usb_semaphore.h"
#include "usb_state.h"
#include "user_logic.h"
#ifdef __ZEPHYR__
    #include "device_state.h"
    #include <nrfx_power.h>
    #include <zephyr/kernel.h>
    #include <zephyr/drivers/usb/udc.h>
#else
    #include "buspal/bus_pal_hardware.h"
    #include "trace.h"
#endif
}
#ifdef __ZEPHYR__
    #include "port/zephyr/udc_mac.hpp"
#else
    #include "port/nxp/mcux_mac.hpp"
#endif
#include "command_app.hpp"
#include "controls_app.hpp"
#include "keyboard_app.hpp"
#include "mouse_app.hpp"
#include "usb/df/class/hid.hpp"
#include "usb/df/device.hpp"
#include "usb/df/vendor/microsoft/os_extension.hpp"
#include "usb/df/vendor/microsoft/xinput.hpp"
#include <magic_enum.hpp>

using namespace magic_enum::bitwise_operators;

#ifdef CONFIG_UHK_USB_LEFT_RELAY
class relay_hid_function : public usb::df::hid::function {
  public:
    using usb::df::hid::function::function;
    void cancel()
    {
        if (ep_in_handle().valid()) {
            (void)cancel_ep(ep_in_handle());
        }
    }
};
static relay_hid_function *relayFunctions[RelayKind_Count + 1];
/* udc_ep_dequeue waits on the nRF driver. Run it on c2usb's worker,
 * keeping main-loop scanning and route selection nonblocking. */
static uint32_t cancellationGeneration[RelayKind_Count + 1];
static atomic_t lifecycleQueued, lifecycleFinished;
static atomic_t powerSyncQueued;
static atomic_t usbWorkerProgress;
static atomic_t usbWorkerLastSeen;
extern "C" atomic_t UhkNrfUsbDiag[13];
extern "C" atomic_t UhkUsbCtrlDiag[12], UhkUsbCtrlTraceCount, UhkUsbCtrlTrace[64][6];
extern "C" atomic_t UhkNrfSetupTraceCount, UhkNrfSetupTrace[32][6];
static uint32_t lastPowerSync;
static uint32_t fenceGeneration;
template <uint8_t Kind>
static void cancelOnUsbThread()
{
    uint32_t generation;
    if (Hid_LocalUsbCancellationPending(Kind, &generation) &&
        generation == cancellationGeneration[Kind] && relayFunctions[Kind]) {
        relayFunctions[Kind]->cancel();
    }
}
static void lifecycleBarrier()
{
    atomic_set(&usbWorkerProgress, 3);
    atomic_set(&usbWorkerLastSeen, Timer_GetCurrentTime());
    Hid_LocalUsbFinishFence(fenceGeneration);
    atomic_set(&lifecycleFinished, fenceGeneration);
    atomic_set(&lifecycleQueued, 0);
}
static void lifecycleCancel();
extern "C" void Hid_LocalUsbRequestFence(void)
{
    EventVector_WakeMain();
}

#endif
static uint8_t usb_serial_number[5]{};

constexpr usb::product_info product_info{CONFIG_USB_DEVICE_VID, CONFIG_USB_DEVICE_MANUFACTURER,
    CONFIG_USB_DEVICE_PID, CONFIG_USB_DEVICE_PRODUCT,
    usb::version(CONFIG_USB_DEVICE_PRODUCT_VERSION >> 8, CONFIG_USB_DEVICE_PRODUCT_VERSION),
    usb_serial_number};

struct usb_manager {
    static auto &mac() { return instance().mac_; }
    static usb::df::device &device() { return instance().device_; }
    static bool active() { return device().is_open(); }
    static bool ms_host() { return instance().ms_enum_.msos2_support(); }

    static usb_manager &instance()
    {
        static usb_manager um;
        return um;
    }

    void select_config()
    {
        // pretend that the device is disconnected
        if (device().is_open()) {
            const unsigned bus_reset_delay_ms = 100;
            device().close();
#ifdef __ZEPHYR__
            k_msleep(bus_reset_delay_ms);
#else
            // TODO: use non-blocking delay
            SDK_DelayAtLeastUs(bus_reset_delay_ms * 1000, SystemCoreClock);
#endif
        }

        using namespace usb::df;

        static constexpr auto speed = usb::speed::FULL;
        static
#ifdef CONFIG_UHK_USB_LEFT_RELAY
            relay_hid_function
#else
            usb::df::hid::function
#endif
                usb_kb{keyboard_app::usb_handle(), usb::hid::boot_protocol_mode::KEYBOARD};
        static
#ifdef CONFIG_UHK_USB_LEFT_RELAY
            relay_hid_function
#else
            usb::df::hid::function
#endif
                usb_mouse{mouse_app::usb_handle()};
        static usb::df::hid::function usb_command{command_app::usb_handle()};
        static
#ifdef CONFIG_UHK_USB_LEFT_RELAY
            relay_hid_function
#else
            usb::df::hid::function
#endif
                usb_controls{controls_app::usb_handle()};

#ifdef CONFIG_UHK_USB_LEFT_RELAY
        relayFunctions[RelayKind_Keyboard] = &usb_kb;
        relayFunctions[RelayKind_Mouse] = &usb_mouse;
        relayFunctions[RelayKind_Controls] = &usb_controls;
#endif
        constexpr auto config_header =
            config::header(config::power::bus(500, config::remote_wakeup));

        static const auto base_config = config::make_config(config_header,
            usb_kb.config_entry(speed, usb::endpoint::address(0x81), 1),
            usb_mouse.config_entry(speed, usb::endpoint::address(0x82), 1),
            usb_command.config_entry(speed, usb::endpoint::address(0x83), 8),
            usb_controls.config_entry(speed, usb::endpoint::address(0x84), 1));

        device_.set_config(base_config);
        device_.open();
    }

    usb_manager()
    {

        device_.set_power_event_delegate([](usb::df::device &dev, usb::df::device::event ev) {
            using event = enum usb::df::device::event;
            if ((ev & event::POWER_STATE_CHANGE) != event::NONE) {
                switch (dev.power_state()) {
                case usb::power::state::L2_SUSPEND:
#if DEVICE_IS_UHK60
                    if (dev.configured()) {
                        UsbState_SetHostSuspended(true);
                    }
#else
                    UsbState_SetHostSuspended(true);
#endif
                    break;
                case usb::power::state::L0_ON:
                    UsbState_SetHostSuspended(false);
                    break;
                case usb::power::state::L3_OFF:
                    // Detached - there is no host to be suspended. Leaving the flag set
                    // would make it stale until the next enumeration, which only stays
                    // harmless as long as every reader also checks UsbState_TransportUp.
                    UsbState_SetHostSuspended(false);
                    break;
                default:
                    break;
                }
            }
            if ((ev & event::CONFIGURATION_CHANGE) != event::NONE) {
                // reset the semaphore on USB configuration or reset
#ifdef CONFIG_UHK_USB_LEFT_RELAY
                Hid_LocalUsbConfigurationChanged();
                // The main loop retires only the local route whose generation changed.
#else
                UsbSemaphore_Clear();
#endif

                if (dev.configured()) {
                    // A host that just selected a configuration is awake and listening.
                    // Reconfiguration also clears the host's remote wakeup arming, so a
                    // stale suspended state here would make USB_RemoteWakeup() fail with
                    // EPERM indefinitely.
                    UsbState_SetHostSuspended(false);
                }
            }
        });
    }

#ifdef __ZEPHYR__
    usb::zephyr::udc_mac mac_{DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 256,
        // READY also means VBUS is present. Treating an already-ready regulator
        // as detached can prevent initial attachment when no new power edge fires.
        (nrfx_power_usbstatus_get() != NRFX_POWER_USB_STATE_DISCONNECTED)
            ? usb::power::state::L2_SUSPEND
            : usb::power::state::L3_OFF};
#else
    std::array<uint8_t, 128> ctrl_buffer;
    usb::df::nxp::mcux_mac mac_{usb::df::nxp::mcux_mac::khci(ctrl_buffer)};
#endif
    usb::df::microsoft::alternate_enumeration<usb::speeds(usb::speed::FULL)> ms_enum_{};
    usb::df::device_instance<usb::speeds(usb::speed::FULL)> device_{mac_, product_info, ms_enum_};
};

#ifdef CONFIG_UHK_USB_LEFT_RELAY
static void synchronizePowerOnUsbThread()
{
    atomic_set(&usbWorkerProgress, 4);
    atomic_set(&usbWorkerLastSeen, Timer_GetCurrentTime());
#if DEVICE_IS_UHK80_LEFT
    usb_manager::mac().trace_control_state();
#endif
    // A detach/configuration event can advance the generation after this task
    // was queued. Recheck ownership on the USB worker before reconciling it.
    if (usb_manager::active() &&
        uint32_t(atomic_get(&lifecycleFinished)) == Hid_LocalUsbGeneration()) {
        bool present = nrfx_power_usbstatus_get() != NRFX_POWER_USB_STATE_DISCONNECTED;
        if (usb_manager::mac().synchronize_vbus(present)) {
            LogInf("USB: reconciled VBUS state, present=%u\n", present);
        }
    }
    atomic_set(&powerSyncQueued, 0);
    atomic_set(&usbWorkerProgress, 5);
}
static void checkUsbPower()
{
    uint32_t now = Timer_GetCurrentTime();
    if ((uint32_t)(now - lastPowerSync) < 100 || !atomic_cas(&powerSyncQueued, 0, 1)) {
        return;
    }
    lastPowerSync = now;
    auto result = usb_manager::mac().queue_task(
        etl::delegate<void()>::create<synchronizePowerOnUsbThread>());
    if (result != usb::result::ok) {
        atomic_set(&powerSyncQueued, 0);
    }
}
static void lifecycleCancel()
{
    atomic_set(&usbWorkerProgress, 1);
    atomic_set(&usbWorkerLastSeen, Timer_GetCurrentTime());
    fenceGeneration = Hid_LocalUsbGeneration();
    uint8_t mask = Hid_LocalUsbRetireTickets();
    for (uint8_t kind = 1; kind <= RelayKind_Count; ++kind) {
        if ((mask & (1u << kind)) && relayFunctions[kind]) {
            relayFunctions[kind]->cancel();
        }
    }
    atomic_set(&usbWorkerProgress, 2);
    /* nRF posts dequeued completions before waking the cancellation caller.
     * FIFO ordering places this barrier after those completions. */
    auto result = usb_manager::mac().queue_task(etl::delegate<void()>::create<lifecycleBarrier>());
    if (result != usb::result::ok) {
        atomic_set(&lifecycleQueued, 0);
    }
}
#if DEVICE_IS_UHK80_LEFT
static void logControlTrace(const char *name, atomic_t (*trace)[6], uint32_t depth,
    uint32_t total, uint32_t &last)
{
    if (uint32_t(total - last) > depth) {
        last = total - depth;
    }
    // Bound each diagnostic batch. A stopped enumeration leaves these rings
    // stable, so subsequent batches expose its tail without blocking scanning.
    for (unsigned count = 0; last != total && count < 12; ++count) {
        uint32_t seq = last + 1;
        auto *record = trace[(seq - 1) % depth];
        if (uint32_t(atomic_get(&record[0])) != seq) {
            break;
        }
        uint32_t time = atomic_get(&record[1]), event = atomic_get(&record[2]);
        uint32_t lo = atomic_get(&record[3]), hi = atomic_get(&record[4]);
        uint32_t detail = atomic_get(&record[5]);
        if (uint32_t(atomic_get(&record[0])) != seq) {
            break;
        }
        LogTo(DeviceId_Uhk80_Right, LogTarget_Uart,
            "USB left %s #%u t=%u e=%u hdr=%08x:%08x info=%08x\n",
            name, unsigned(seq), unsigned(time), unsigned(event), unsigned(lo),
            unsigned(hi), unsigned(detail));
        last = seq;
    }
}
#endif
extern "C" void Hid_LocalUsbService(void)
{
#if DEVICE_IS_UHK80_LEFT
    // This diagnostic uses the bridge so it remains visible when left USB
    // cannot enumerate. Sample on the main loop even if the USB worker stalls.
    static uint32_t lastDiagnostic;
    uint32_t now = Timer_GetCurrentTime();
    if (Connections_IsReady(ConnectionId_UartRight) &&
        uint32_t(now - lastDiagnostic) >= 3000) {
        lastDiagnostic = now;
        unsigned enabled = NRF_USBD->ENABLE;
        unsigned low = NRF_USBD->LOWPOWER;
        // USBPULLUP is inaccessible in low power or while disabled (nRF52840
        // product specification 6.35.12). 255 denotes an unavailable reading.
        unsigned pullup = enabled && !low ? unsigned(NRF_USBD->USBPULLUP) : 255;
        LogTo(DeviceId_Uhk80_Right, LogTarget_Uart,
            "USB left hw: vbus=%u power=%u open=%u cfg=%u udc=%u enable=%u pullup=%u low=%u\n",
            unsigned(nrfx_power_usbstatus_get()), unsigned(usb_manager::mac().power_state()),
            usb_manager::active(), usb_manager::device().configured(),
            udc_is_enabled(usb_manager::mac().driver_device()), enabled, pullup, low);
        LogTo(DeviceId_Uhk80_Right, LogTarget_Uart,
            "USB left work r10: gen=%u finished=%u fenceQ=%u powerQ=%u worker=%u age=%u\n",
            unsigned(Hid_LocalUsbGeneration()), unsigned(atomic_get(&lifecycleFinished)),
            unsigned(atomic_get(&lifecycleQueued)), unsigned(atomic_get(&powerSyncQueued)),
            unsigned(atomic_get(&usbWorkerProgress)),
            unsigned(now - uint32_t(atomic_get(&usbWorkerLastSeen))));
        LogTo(DeviceId_Uhk80_Right, LogTarget_Uart,
            "USB left drv: enable=%u ready=%u start=%u stop=%u suspend=%u enState=%x ev=%u st=%x\n",
            unsigned(atomic_get(&UhkNrfUsbDiag[0])), unsigned(atomic_get(&UhkNrfUsbDiag[1])),
            unsigned(atomic_get(&UhkNrfUsbDiag[2])), unsigned(atomic_get(&UhkNrfUsbDiag[3])),
            unsigned(atomic_get(&UhkNrfUsbDiag[4])), unsigned(atomic_get(&UhkNrfUsbDiag[5])),
            unsigned(atomic_get(&UhkNrfUsbDiag[6])), unsigned(atomic_get(&UhkNrfUsbDiag[7])));
        LogTo(DeviceId_Uhk80_Right, LogTarget_Uart,
            "USB left bus: reset=%u setup=%u resume=%u cause=%x clk=%x\n",
            unsigned(atomic_get(&UhkNrfUsbDiag[8])), unsigned(atomic_get(&UhkNrfUsbDiag[9])),
            unsigned(atomic_get(&UhkNrfUsbDiag[10])), unsigned(atomic_get(&UhkNrfUsbDiag[12])),
            unsigned(NRF_CLOCK->HFCLKSTAT));
        LogTo(DeviceId_Uhk80_Right, LogTarget_Uart,
            "USB left ctrl: setup=%u reply=%u stall=%u error=%u enqueueFail=%u flags=%x\n",
            unsigned(atomic_get(&UhkUsbCtrlDiag[0])), unsigned(atomic_get(&UhkUsbCtrlDiag[1])),
            unsigned(atomic_get(&UhkUsbCtrlDiag[2])), unsigned(atomic_get(&UhkUsbCtrlDiag[3])),
            unsigned(atomic_get(&UhkUsbCtrlDiag[4])), unsigned(atomic_get(&UhkUsbCtrlDiag[11])));
        LogTo(DeviceId_Uhk80_Right, LogTarget_Uart,
            "USB left retire: begin=%u full=%u finish=%u dropped=%u offDrop=%u arm=%u\n",
            unsigned(atomic_get(&UhkUsbCtrlDiag[5])), unsigned(atomic_get(&UhkUsbCtrlDiag[6])),
            unsigned(atomic_get(&UhkUsbCtrlDiag[7])), unsigned(atomic_get(&UhkUsbCtrlDiag[8])),
            unsigned(atomic_get(&UhkUsbCtrlDiag[9])), unsigned(atomic_get(&UhkUsbCtrlDiag[10])));
        static uint32_t lastControl, lastDriver;
        uint32_t control = atomic_get(&UhkUsbCtrlTraceCount);
        uint32_t driver = atomic_get(&UhkNrfSetupTraceCount);
        if (usb_manager::device().configured()) {
            lastControl = control;
            lastDriver = driver;
        } else {
            logControlTrace("ep0", UhkUsbCtrlTrace, 64, control, lastControl);
            logControlTrace("setup", UhkNrfSetupTrace, 32, driver, lastDriver);
        }
    }
#endif
    /* Re-requesting a fence while idle is cheap; it is only needed when a
     * generation has not yet passed its ordered USB-worker barrier. */
    uint32_t generation = Hid_LocalUsbGeneration();
    if (uint32_t(atomic_get(&lifecycleFinished)) == generation) {
        // Recover a missed power edge only after the old USB generation's
        // cancellation/completion barrier has finished. Work stays on c2usb.
        checkUsbPower();
        return;
    }
    if (atomic_cas(&lifecycleQueued, 0, 1)) {
        auto result =
            usb_manager::mac().queue_task(etl::delegate<void()>::create<lifecycleCancel>());
        if (result != usb::result::ok) {
            atomic_set(&lifecycleQueued, 0);
        }
    }
}
template <uint8_t Kind>
static void sendOnUsbThread()
{
    Hid_LocalUsbSendQueued(Kind);
}
extern "C" bool Hid_LocalUsbQueueSend(uint8_t kind)
{
    etl::delegate<void()> task;
    switch (kind) {
    case RelayKind_Keyboard:
        task = etl::delegate<void()>::create<sendOnUsbThread<RelayKind_Keyboard>>();
        break;
    case RelayKind_Mouse:
        task = etl::delegate<void()>::create<sendOnUsbThread<RelayKind_Mouse>>();
        break;
    case RelayKind_Controls:
        task = etl::delegate<void()>::create<sendOnUsbThread<RelayKind_Controls>>();
        break;
    default:
        return false;
    }
    return usb_manager::mac().queue_task(task) == usb::result::ok;
}
extern "C" bool Hid_LocalUsbCancel(uint8_t kind)
{
    uint32_t generation;
    if (!Hid_LocalUsbCancellationPending(kind, &generation)) {
        return true;
    }
    cancellationGeneration[kind] = generation;
    etl::delegate<void()> task;
    switch (kind) {
    case RelayKind_Keyboard:
        task = etl::delegate<void()>::create<cancelOnUsbThread<RelayKind_Keyboard>>();
        break;
    case RelayKind_Mouse:
        task = etl::delegate<void()>::create<cancelOnUsbThread<RelayKind_Mouse>>();
        break;
    case RelayKind_Controls:
        task = etl::delegate<void()>::create<cancelOnUsbThread<RelayKind_Controls>>();
        break;
    default:
        return false;
    }
    return usb_manager::mac().queue_task(task) == usb::result::ok;
}
#endif

#ifndef __ZEPHYR__
extern "C" void USB0_IRQHandler(void)
{
    ISR_LIFE_START(usb);
    Trace_Printc("<i6");
    if (usb::df::nxp::mcux_mac::notification_routing) {
        usb_manager::mac().handle_irq();
    } else {
        USB_DeviceKhciIsrFunction(BuspalCompositeUsbDevice.device_handle);
    }
    Trace_Printc(">");
    ISR_LIFE_END(usb);
    SDK_ISR_EXIT_BARRIER;
}
#endif

extern "C" void USB_Enable()
{
    assert(!usb_manager::active());
    usb_manager::instance().select_config();
}

#ifdef CONFIG_UHK_USB_LEFT_RELAY
static void reconfigureOnUsbThread()
{
    if (usb_manager::active()) {
        usb_manager::instance().select_config();
    }
}
#endif
extern "C" void USB_Reconfigure()
{
#ifdef CONFIG_UHK_USB_LEFT_RELAY
    (void)usb_manager::mac().queue_task(etl::delegate<void()>::create<reconfigureOnUsbThread>());
    return;
#endif
    if (usb_manager::active()) {
        usb_manager::instance().select_config();
    }
}

extern "C" bool USB_RemoteWakeup()
{
    auto err = usb_manager::instance().device().remote_wakeup();
    if (err.to_int() == -1 /* not permitted; treat it as awake */) {
        UsbState_SetHostSuspended(false);
        LogInf("USB: remote wakeup not permitted, usb suspended: %d\n", UsbState_HostIsSuspended);
    } else if (err != usb::result::ok) {
        LogErr("USB: remote wakeup request failed: %d\n", err.to_int());
    } else {
        LogInf("USB: remote wakeup request succeeded\n");
    }
    return err == usb::result::ok;
}

extern "C" bool USB_IsMsHost(void)
{
    return usb_manager::ms_host();
}

extern "C" void USB_SetSerialNumber(uint32_t serialNumber)
{
    const uint8_t serialNumberByteCount = 5;

    static_assert(
        sizeof(usb_serial_number) >= serialNumberByteCount, "usb_serial_number size is too small");

    // Convert each pair of decimal digits into a single byte
    for (uint8_t i = serialNumberByteCount - 1; i < 255; --i) {
        uint8_t byte = 0;
        for (uint8_t j = 0; j < 2; ++j) {
            uint8_t digit = serialNumber % 10;
            serialNumber /= 10;

            byte |= digit << (j * 4);
        }

        usb_serial_number[i] = byte;
    }
}

#ifdef CONFIG_UHK_USB_LEFT_RELAY
extern "C" bool Hid_LocalUsbWakeAllowed()
{
    return usb_manager::device().allows_remote_wakeup();
}
#endif
