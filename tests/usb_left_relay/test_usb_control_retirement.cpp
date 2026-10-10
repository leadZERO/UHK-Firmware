#include <cassert>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <deque>
#include <array>
#include <algorithm>
#include <functional>
#include <cstdio>
#include <string_view>
#include "usb_control_features.inc"
#define UHK_CONTROL_COUNT(index) ((void)0)
#define UHK_CONTROL_TRACE(...) ((void)0)

enum { USB_CONTROL_EP_OUT=0, USB_CONTROL_EP_IN=0x80, K_FOREVER=-1, UDC_STATUS_ENABLED=0 };
enum { UDC_EVT_RESET, UDC_EVT_SUSPEND, UDC_EVT_RESUME, UDC_EVT_VBUS_READY,
       UDC_EVT_VBUS_REMOVED, UDC_EVT_ERROR, UDC_EVT_EP_REQUEST, UDC_EVT_SOF };
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define UDC_COMMON_LOG_LEVEL 0
#define LOG_LEVEL_DBG 4
#define USB_EP_DIR_IS_IN(ep) ((ep) & 0x80)
struct udc_buf_info { uint8_t ep=0; bool setup=false, data=false, status=false; int err=0; };
struct net_buf { udc_buf_info info; unsigned len=0, size=64; uint8_t data[64]{}; };
struct udc_ep_config {
    uint8_t addr;
    struct { bool enabled=true, halted=false, busy=false; } stat;
    std::deque<net_buf*> fifo;
};
struct udc_data { unsigned status=1; bool setup_pending=false, setup_valid=false; uint8_t setup[8]{}; };
struct device;
namespace etl {
template<class> struct delegate;
template<> struct delegate<void()> {
    std::function<void()> f;
    template<class T,void(T::*Method)()>static delegate create(T& t) { return {[&t]{(t.*Method)();}}; }
    void operator()()const { f(); }
};
}
struct udc_api {
    void (*lock)(const device*);
    void (*unlock)(const device*);
    int (*disable)(const device*);
    int (*ep_enqueue)(const device*,udc_ep_config*,net_buf*);
    int (*ep_dequeue)(const device*,udc_ep_config*);
};
struct device { udc_api *api; udc_data *data; udc_ep_config out{0,{},{}}, in{0x80,{},{}}; unsigned enables=0; };
struct udc_event { int type; const device *dev=nullptr; net_buf *buf=nullptr; etl::delegate<void()> value{}; };
static std::deque<std::function<void()>> events;
static unsigned queueCapacity=1000;
struct WorkerWouldDeadlock {};
struct QueueDrained {};
static std::function<void(const udc_event&)> dispatch;
static unsigned oldControlDelivered=0;
static auto *udc_get_buf_info(net_buf *buf) { return &buf->info; }
static auto *udc_get_ep_cfg(const device *dev,uint8_t ep) { return ep ? &const_cast<device*>(dev)->in : &const_cast<device*>(dev)->out; }
static bool udc_is_initialized(const device *) { return true; }
static bool udc_is_enabled(const device *dev) { return dev->data->status & 1; }
static void atomic_clear_bit(unsigned *status,int bit) { *status &= ~(1u<<bit); }
static void udc_lock_internal(const device *,int) {}
static void udc_unlock_internal(const device *) {}
static void udc_ep_set_busy(udc_ep_config *cfg,bool busy) { cfg->stat.busy=busy; }
static net_buf *udc_buf_get(udc_ep_config *cfg) { if(cfg->fifo.empty())return nullptr; auto *b=cfg->fifo.front();cfg->fifo.pop_front();return b; }
static bool k_fifo_is_empty(std::deque<net_buf*> *q) { return q->empty(); }
static void udc_debug_ep_enqueue(const device *,udc_ep_config *) {}
static void net_buf_add_mem(net_buf *b,const void *p,unsigned len) { assert(b->len+len<=b->size);std::memcpy(b->data+b->len,p,len);b->len+=len; }
static void net_buf_reset(net_buf *b) { b->len=0; }
static int udc_submit_ep_event(const device *dev,net_buf *buf,int err) {
    assert(events.size()<queueCapacity); // all cancellation notifications fit
    buf->info.err=err;
    events.emplace_back([=] { dispatch({UDC_EVT_EP_REQUEST,dev,buf}); });
    return 0;
}
static int driverDisable(const device *dev) { const_cast<device*>(dev)->out.stat.enabled=false;const_cast<device*>(dev)->in.stat.enabled=false; return 0; }
static int driverEnqueue(const device *,udc_ep_config *cfg,net_buf *buf) {
    // Fixed buffers must have one owner. Duplicate queueing corrupts k_fifo.
    for(auto *b:cfg->fifo) assert(b!=buf);
    cfg->fifo.push_back(buf);return 0;
}
static int driverDequeue(const device *dev,udc_ep_config *cfg) {
    while(auto *b=udc_buf_get(cfg)) udc_submit_ep_event(dev,b,-ECONNABORTED);
    cfg->stat.busy=false;return 0;
}
static void driverLock(const device *) {}
static udc_api api{driverLock,driverLock,driverDisable,driverEnqueue,driverDequeue};
static int udc_enable(const device *dev) {
    auto *d=const_cast<device*>(dev);d->data->status|=1;d->out.stat.enabled=d->in.stat.enabled=true;++d->enables;return 0;
}
[[maybe_unused]] static int udc_set_address(const device *,int) { return 0; }
static bool udc_ep_queue_is_empty(const device *dev,int ep) { return udc_get_ep_cfg(dev,ep)->fifo.empty(); }
#include "usb_control_common.inc"
static int udc_buf_enqueue(const device *dev,net_buf *buf) { return udc_ep_enqueue(dev,buf); }
namespace usb {
enum class result { ok };
namespace power { enum class state { L3_OFF,L2_SUSPEND,L0_ON }; }
namespace endpoint {
struct address { uint8_t value;explicit address(uint8_t v):value(v){} unsigned number()const{return value&0x0f;} };
}
namespace df::diag {
enum class bus_event { RESET,SUSPEND,RESUME,VBUS_ON,VBUS_OFF,ERROR };
static void bus(bus_event) {}
}
}
static constexpr int UDC_MAC_TASK=-1;
static constexpr int UDC_MAC_CONTROL_RETIRED=-2;
namespace usb::zephyr { class udc_mac; }
[[maybe_unused]] static std::array<usb::zephyr::udc_mac*,2> retiring_macs{};
[[maybe_unused]] static usb::zephyr::udc_mac *mac_context;
[[maybe_unused]] static auto *get_event_ctx(const device *) { return mac_context; }
struct FakeMessageQueue {
    bool try_post(const udc_event& event) {
        if(events.size()>=queueCapacity) return false;
        events.emplace_back([=]{ dispatch(event); });return true;
    }
    udc_event get() {
        if(events.empty()) throw QueueDrained{};
        auto f=events.front();events.pop_front();
        return {UDC_MAC_TASK,nullptr,nullptr,{f}};
    }
};
[[maybe_unused]] static auto &message_queue() { static FakeMessageQueue queue;return queue; }
namespace usb::zephyr {
using namespace usb::df;
constexpr bool udc_managed_ctrl=false; // verified against the pinned udc_data header by runner
class udc_mac {
    const device *dev_;
    usb::power::state state_=power::state::L0_ON;
    bool active_=true;
    net_buf setup_;
    net_buf *setup_buf_=&setup_;
    [[maybe_unused]] bool control_retiring_=false;
    [[maybe_unused]] bool control_barrier_queued_=false;
    void retire_control();
    void try_queue_control_retirement();
    static void retry_control_retirements();
    void finish_control_retirement();
    void set_power_state(power::state value) { state_=value; }
    void bus_reset() { set_power_state(power::state::L0_ON); }
    void ctrl_setup();
    void process_ep_event(net_buf *buf) {
        if(buf->info.err) ++oldControlDelivered;
        else if(buf->info.setup) { ++setups;ctrl_setup(); }
    }
public:
    unsigned setups=0;
    explicit udc_mac(const device *dev):dev_(dev) { setup_.info.setup=true; }
    bool active()const{return active_;}
    power::state power_state()const{return state_;}
    bool set_attached(bool);
    int process_event(const udc_event&);
    static int event_callback(const udc_event&);
    static void worker(void*,void*,void*);
    static result queue_task(etl::delegate<void()> task) {
        if(events.size()>=queueCapacity) throw WorkerWouldDeadlock{};
        events.push_back(task.f);return result::ok;
    }
    bool retiring()const{return control_retiring_;}
    void close() { active_=false; }
    void start() { ctrl_setup(); }
};
#include "usb_control_mac.inc"
}
static void drain() {
#ifdef TEST_NONBLOCK_CONTROL
    try { usb::zephyr::udc_mac::worker(nullptr,nullptr,nullptr); }
    catch(const QueueDrained&) {}
#else
    while(!events.empty()) { auto f=events.front();events.pop_front();f(); }
#endif
}
int main(int argc,char **argv) {
    assert(argc==2);
    std::string_view scenario=argv[1];
    udc_data state;
    device dev{&api,&state};
    usb::zephyr::udc_mac mac(&dev);
#ifdef TEST_NONBLOCK_CONTROL
    mac_context=&mac;
    dispatch=[&](const udc_event& event){mac.event_callback(event);};
#else
    dispatch=[&](const udc_event& event){mac.process_event(event);};
#endif
    const uint8_t setup[8]{0x80,6,0,1,0,0,18,0};
    net_buf status;status.info.status=true;
    if(scenario=="full") {
        queueCapacity=4;
        unsigned taskProgress=0;
        // Dequeue adds IN+OUT notifications behind two existing opaque tasks,
        // filling the consumer's queue before it tries to append the barrier.
        net_buf data;data.info.ep=USB_CONTROL_EP_IN;data.info.data=true;
        driverEnqueue(&dev,&dev.in,&data);driverEnqueue(&dev,&dev.out,&status);
        events.emplace_back([&]{
            ++taskProgress;assert(mac.retiring() && !mac.setups);
            // A producer takes the first free slot: first retry still cannot
            // post. Main/worker progress must continue with buffers protected.
            events.emplace_back([&]{++taskProgress;assert(mac.retiring() && !mac.setups);});
        });
        events.emplace_back([&]{++taskProgress;assert(mac.retiring() && !mac.setups);});
        mac.process_event({UDC_EVT_RESET,&dev});
        assert(mac.retiring() && events.size()==queueCapacity && !mac.setups);
        drain();
        assert(taskProgress==3 && !mac.retiring() && oldControlDelivered==0);
        udc_setup_received(&dev,setup);drain();
        assert(mac.setups==1 && !state.setup_pending);
        assert(dev.out.fifo.size()==1 && dev.out.fifo.front()->info.setup);
        assert(dev.enables==0);
    } else if(scenario=="reset" || scenario=="reset_cached") {
        // Bridge power may keep VBUS present: reset must rearm EP0 OUT too.
        driverEnqueue(&dev,&dev.out,&status);
        mac.process_event({UDC_EVT_RESET,&dev});
        if(scenario=="reset_cached") udc_setup_received(&dev,setup);
        drain();
        if(scenario=="reset") { udc_setup_received(&dev,setup);drain(); }
        assert(mac.setups==1 && !state.setup_pending);
        assert(dev.out.fifo.size()==1 && dev.out.fifo.front()->info.setup);
        assert(oldControlDelivered==0);
        assert(dev.enables==0); // Controller stays enabled, no artificial restart.
    } else if(scenario=="cached") {
        // A SETUP arrived while the previous stack response was delayed.
        state.setup_pending=state.setup_valid=true;std::memcpy(state.setup,setup,8);
        mac.process_event({UDC_EVT_VBUS_REMOVED,&dev});drain();
        assert(!state.setup_pending && !state.setup_valid);
        mac.process_event({UDC_EVT_VBUS_READY,&dev});drain();
        assert(mac.setups==0); // Obsolete request must never be replayed on new cable.
    } else {
        // Previous SETUP event was posted but not processed when VBUS fell.
        // Its OUT STATUS remains queued. Queue nonempty != SETUP armed.
        driverEnqueue(&dev,&dev.out,&status);
        // Exercise READY already queued before dequeue completions, too.
        if(scenario=="late") events.emplace_back([&]{mac.process_event({UDC_EVT_VBUS_READY,&dev});});
        mac.process_event({UDC_EVT_VBUS_REMOVED,&dev});
        if(scenario=="closed") {
            mac.close();drain();assert(!udc_is_enabled(&dev));
            assert(dev.out.fifo.empty());
        } else {
            if(scenario=="retained") {drain();mac.process_event({UDC_EVT_VBUS_READY,&dev});}
            drain();
            assert(udc_is_enabled(&dev));
            udc_setup_received(&dev,setup);drain();
            assert(mac.setups==1 && !state.setup_pending);
            assert(oldControlDelivered==0); // cancellation cannot touch fresh buffers
            assert(dev.out.fifo.size()==1 && dev.out.fifo.front()->info.setup);
            // Subsequent detach remains detached; barrier must not reopen it.
            mac.process_event({UDC_EVT_VBUS_REMOVED,&dev});drain();
            assert(!udc_is_enabled(&dev) && dev.out.fifo.empty());
        }
    }
    std::printf("USB control retirement %s test passed\n",argv[1]);
}
