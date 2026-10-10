#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <memory>
#include "usb/df/transfer.hpp"

// Only the platform/event plumbing is mocked. The runner includes the actual
// process_ep_event method from the patched, pinned SDK and its transfer class.
struct udc_buf_info { uint8_t ep; int err; };
struct net_buf {
    uint8_t *data, *__buf;
    uint16_t size, len;
    udc_buf_info info;
};
static udc_buf_info *udc_get_buf_info(net_buf *buf) { return &buf->info; }
#define LOG_ERR(...) ((void)0)
#define c2usb_log(...) ((void)0)
namespace usb::endpoint {
class address {
    uint8_t value;
public:
    explicit address(uint8_t v) : value(v) {}
    uint8_t number() const { return value & 0x0f; }
    usb::direction direction() const { return value & 0x80 ? usb::direction::IN : usb::direction::OUT; }
};
}
namespace usb::df {
class mac {
protected:
    static ep_handle create_ep_handle(uint8_t id) { return ep_handle(id); }
};
}
namespace usb::df::zephyr {
constexpr bool udc_managed_ctrl = true;
namespace diag {
constexpr int EP_ERROR = 0;
[[maybe_unused]] static void record(int, uint8_t, int) {}
[[maybe_unused]] static void success(uint8_t) {}
}
class udc_mac : public usb::df::mac {
public:
    struct { void clear(endpoint::address) {} } busy_flags_;
    std::array<net_buf *, 1> ep_bufs_{};
    transfer completed{};
    bool called = false;
    void process_ctrl_ep_event(net_buf *, const udc_buf_info &) {}
    void process_ctrl_ep(net_buf *, const udc_buf_info &) {}
    void ep_transfer_complete(endpoint::address, const transfer &value) {
        completed = value;
        called = true;
    }
    void process_ep_event(net_buf *buf);
};
#include "c2usb_completion_under_test.inc"
}

static usb::df::transfer complete(net_buf &buf) {
    usb::df::zephyr::udc_mac mac;
    mac.ep_bufs_[0] = &buf;
    mac.process_ep_event(&buf);
    assert(mac.called);
    return mac.completed;
}
int main() {
    std::array<uint8_t, 128> bytes{};
    // Includes a ZLP, short packet, full packet, and multi-packet transfer.
    for (uint16_t length : {uint16_t(0), uint16_t(8), uint16_t(33), uint16_t(64), uint16_t(128)}) {
        for (bool consumed : {false, true}) {
            net_buf buf{bytes.data() + (consumed ? length : 0), bytes.data(), length,
                        uint16_t(consumed ? 0 : length), {0x81, 0}};
            auto completed = complete(buf);
            assert(completed.data() == bytes.data());
            assert(completed.transferred_size() == length);
            assert(completed.success() && completed.endpoint().valid());
        }
    }
    // Partially consumed and cancelled/error IN transfers acknowledge no bytes.
    for (int error : {-ECONNABORTED, -EIO}) {
        net_buf buf{bytes.data() + 4, bytes.data(), 33, 29, {0x81, error}};
        auto completed = complete(buf);
        assert(completed.data() == bytes.data());
        assert(completed.transferred_size() == 0 && completed.cancelled());
    }
    // OUT reports received length, never the advertised receive capacity.
    for (uint16_t length : {uint16_t(0), uint16_t(3), uint16_t(64), uint16_t(128)}) {
        net_buf buf{bytes.data(), bytes.data(), 128, length, {0x01, 0}};
        auto completed = complete(buf);
        assert(completed.data() == bytes.data() && completed.transferred_size() == length);
    }
    net_buf failedOut{bytes.data(), bytes.data(), 128, 3, {0x01, -EIO}};
    assert(complete(failedOut).transferred_size() == 0);
    // EP0 still uses its separate control-transfer path.
    net_buf control{bytes.data(), bytes.data(), 64, 8, {0, 0}};
    usb::df::zephyr::udc_mac mac;
    mac.process_ep_event(&control);
    assert(!mac.called);
    puts("USB completion metadata: IN, OUT, ZLP, errors, and EP0 passed");
}

