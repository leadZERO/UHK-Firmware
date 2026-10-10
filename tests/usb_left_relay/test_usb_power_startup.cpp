#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <cstdint>
#include <array>
#include <algorithm>
#include "usb_power_features.inc"
#define UHK_CONTROL_COUNT(index) ((void)0)
#define UHK_CONTROL_TRACE(...) ((void)0)
enum { NRFX_POWER_USB_STATE_DISCONNECTED, NRFX_POWER_USB_STATE_CONNECTED, NRFX_POWER_USB_STATE_READY };
static int regulator;
static int nrfx_power_usbstatus_get() { return regulator; }
namespace usb::power { enum class state { L3_OFF, L2_SUSPEND, L0_ON }; }
static usb::power::state initialPower() {
    return
#include "usb_initial_power_under_test.inc"
    ;
}
struct device { bool enabled=false; unsigned enables=0; };
struct net_buf { struct { uint8_t ep=0; } info; };
[[maybe_unused]] static auto *udc_get_buf_info(net_buf *buf) { return &buf->info; }
namespace usb::endpoint {
struct address { uint8_t value; explicit address(uint8_t v):value(v) {} unsigned number()const {return value&0x0f;} };
}
namespace usb { enum class result { ok,other }; }
namespace etl {
template<class> struct delegate;
template<> struct delegate<void()> {
    template<void(*Func)()> static auto create() { return Func; }
    template<class T,void(T::*Func)()> static auto create(T&) { return []{}; }
};
}
enum { UDC_EVT_RESET,UDC_EVT_SUSPEND,UDC_EVT_RESUME,UDC_EVT_VBUS_READY,UDC_EVT_VBUS_REMOVED,
       UDC_EVT_ERROR,UDC_EVT_EP_REQUEST,UDC_EVT_SOF };
struct udc_event { device *dev=nullptr; int type=0; net_buf *buf=nullptr; };
static bool udc_is_enabled(device *dev) { return dev->enabled; }
static int udc_enable(device *dev) { dev->enabled=true; ++dev->enables; return 0; }
static int udc_disable(device *dev) { dev->enabled=false; return 0; }
[[maybe_unused]] static bool udc_ep_queue_is_empty(device *,int) { return true; }
enum { USB_CONTROL_EP_OUT=0, USB_CONTROL_EP_IN=0x80 };
[[maybe_unused]] static int udc_set_address(device *,int) { return 0; }
[[maybe_unused]] static int udc_ep_dequeue(device *,int) { return 0; }
namespace usb::df {
namespace diag {
enum class bus_event { RESET,SUSPEND,RESUME,VBUS_ON,VBUS_OFF,ERROR };
[[maybe_unused]] static void bus(bus_event) {}
}
struct fake_device {
    unsigned powerChanges=0;
    void on_power_state_change(usb::power::state) { ++powerChanges; }
};
class mac {
protected:
    bool active_=false;
    usb::power::state power_state_;
    fake_device dev;
    fake_device *dev_if_=&dev;
    virtual bool set_attached(bool)=0;
    void set_power_state(usb::power::state);
    void bus_reset() { set_power_state(usb::power::state::L0_ON); }
public:
    explicit mac(usb::power::state state):power_state_(state) {}
    bool active() const { return active_; }
    void start();
    usb::power::state power_state() const { return power_state_; }
    void deactivate() { active_=false; }
    unsigned changes() const { return dev.powerChanges; }
};
#include "usb_mac_start_under_test.inc"
#include "usb_mac_power_under_test.inc"
}
namespace usb::df::zephyr {
constexpr bool udc_managed_ctrl=true;
class udc_mac;
static std::array<udc_mac*,2> retiring_macs{};
class udc_mac:public usb::df::mac {
    device *dev_;
    bool control_retiring_=false;
    bool control_barrier_queued_=false;
    void retire_control();
    void try_queue_control_retirement() {}
    void finish_control_retirement();
    void ctrl_setup() {}
    void process_ep_event(net_buf *) { ++epEvents; }
public:
    unsigned epEvents=0;
    udc_mac(device *dev,usb::power::state state):mac(state),dev_(dev) {}
    bool set_attached(bool) override;
    bool synchronize_vbus(bool);
    int process_event(const udc_event&);
    static usb::result queue_task(void (*)()) { return usb::result::ok; }
};
#include "usb_attach_under_test.inc"
#include "usb_control_barrier_under_test.inc"
#ifdef TEST_VBUS_RECONCILE
#include "usb_reconcile_under_test.inc"
#endif
}
#ifdef TEST_VBUS_RECONCILE
// Compile the actual main-loop fence gate: power reconciliation cannot run
// before cancellation/completion ownership of the preceding generation drains.
using atomic_t=int;
static atomic_t lifecycleFinished,lifecycleQueued;
#ifdef TEST_POWER_TASK
static atomic_t powerSyncQueued;
static atomic_t usbWorkerProgress, usbWorkerLastSeen;


static uint32_t now=100;
static uint32_t Timer_GetCurrentTime() { return now; }
#endif
static uint32_t generation=4;
static unsigned powerChecks,queuedCancels;
static unsigned workerPowerSyncs;
static bool managerOpen=true;




[[maybe_unused]] static void LogInf(const char *,...) {}
static uint32_t Hid_LocalUsbGeneration() { return generation; }
static int atomic_get(atomic_t *p) { return *p; }
static void atomic_set(atomic_t *p,int value) { *p=value; }

static bool atomic_cas(atomic_t *p,int from,int to) { if(*p!=from) return false; *p=to; return true; }
[[maybe_unused]] static void checkUsbPower() { ++powerChecks; }
static void lifecycleCancel() {}
struct usb_manager {
    static bool active() { return managerOpen; }


    static auto &mac() { static usb_manager instance; return instance; }

    usb::result queue_task(void (*)()) { ++queuedCancels; return usb::result::ok; }
    bool synchronize_vbus(bool) { ++workerPowerSyncs; return false; }
};
#include "usb_power_barrier_under_test.inc"
#include "usb_power_task_under_test.inc"
#endif
int main() {
    for(int status : {NRFX_POWER_USB_STATE_DISCONNECTED,NRFX_POWER_USB_STATE_CONNECTED,NRFX_POWER_USB_STATE_READY}) {
        regulator=status;
        device hardware;
        usb::df::zephyr::udc_mac mac(&hardware,initialPower());
        // Run the actual SDK start/attach methods. No fresh DETECTED event is
        // supplied: VBUS may already be READY before application construction.
        mac.start();
        assert(mac.active());
        if(status==NRFX_POWER_USB_STATE_DISCONNECTED) {
            assert(!hardware.enabled && hardware.enables==0);
        } else {
            assert(hardware.enabled && hardware.enables==1);
        }
        mac.start();
        assert(hardware.enables==(status==NRFX_POWER_USB_STATE_DISCONNECTED ? 0u : 1u));
    }
#ifdef TEST_VBUS_RECONCILE
    device hardware;
    usb::df::zephyr::udc_mac mac(&hardware,usb::power::state::L3_OFF);
    mac.start();
    assert(!hardware.enabled);
    assert(mac.synchronize_vbus(true)); // Missed insertion edge, VBUS already present.
    assert(hardware.enabled && hardware.enables==1);
    assert(!mac.synchronize_vbus(true)); // Stable power never cycles the controller.
    assert(mac.synchronize_vbus(false)); // Missing removal edge uses normal detach.
    assert(!hardware.enabled && mac.power_state()==usb::power::state::L3_OFF);
    assert(!mac.synchronize_vbus(false));
    assert(mac.synchronize_vbus(true)); // Reinsert with bridge power: fresh attach.
    assert(hardware.enables==2);
    mac.deactivate();
    assert(!mac.synchronize_vbus(false)); // Never reopen/modify an intentionally closed device.
    Hid_LocalUsbService(); assert(!powerChecks && queuedCancels==1);
    Hid_LocalUsbService(); assert(!powerChecks && queuedCancels==1);
    lifecycleFinished=4; lifecycleQueued=0;
    Hid_LocalUsbService(); assert(powerChecks==1);
    ++generation;
    Hid_LocalUsbService(); assert(powerChecks==1 && queuedCancels==2);
#ifdef TEST_POWER_TASK
    // A new detach can occur between queueing the power task and running it.
    powerSyncQueued=1;
    synchronizePowerOnUsbThread(); assert(!workerPowerSyncs && !powerSyncQueued);
    lifecycleFinished=generation;
    powerSyncQueued=1;
    synchronizePowerOnUsbThread(); assert(workerPowerSyncs==1 && !powerSyncQueued);
    managerOpen=false; powerSyncQueued=1;
    synchronizePowerOnUsbThread(); assert(workerPowerSyncs==1 && !powerSyncQueued);
#endif
#endif
    puts("USB initial-power/attachment tests passed");
}
