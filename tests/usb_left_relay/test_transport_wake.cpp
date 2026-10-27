#include <cassert>
#include <cerrno>
#include <cstdio>

// Compile the production wake helper and each complete right-USB switch arm.
// Session/queue submission, USB power state and semaphore release are mocked;
// this checks merge routing, not USB callback timing or physical remote wake.
enum { ReportSink_Usb };
enum { RelayKind_Keyboard = 1, RelayKind_Mouse, RelayKind_Controls };
struct fake_report { unsigned buttons; };
struct fake_state {};
static struct { fake_state keyboard, mouse, controls; } UsbSemaphore;
static bool UsbState_HostIsSuspended, wakeSucceeds, hasSession;
static unsigned wakes, queued, direct, released;
static int submitResult;
static fake_state *releasedState;
#define Trace_Printf(...) ((void)0)
#define Trace_Printc(...) ((void)0)
#define LOG_INF(...) ((void)0)
static bool USB_RemoteWakeup()
{
    ++wakes;
    return wakeSucceeds;
}
[[maybe_unused]] static void UsbSemaphore_Release(fake_state *state)
{
    ++released;
    releasedState = state;
}
struct fake_session {
    struct result { int to_int() { return submitResult; } };
    result send_report(const fake_report *) { ++direct; return {}; }
};
static fake_session session;
struct fake_handle {
    fake_session *session() { return hasSession ? &::session : nullptr; }
};
struct keyboard_app {
    static fake_handle usb_handle() { return {}; }
};
using mouse_app = keyboard_app;
using controls_app = keyboard_app;
#ifdef CONFIG_UHK_USB_LEFT_RELAY
static int UsbLeft_QueueUsb(unsigned, const fake_report *, unsigned, const fake_report * = nullptr)
{
    ++queued;
    return submitResult;
}
#endif
#include "transport_wake_under_test.inc"

static void reset(bool suspended, bool canWake, bool connected = true)
{
    UsbState_HostIsSuspended = suspended;
    wakeSucceeds = canWake;
    hasSession = connected;
    wakes = queued = direct = released = 0;
    releasedState = nullptr;
    submitResult = 0;
}
static void checkOneSubmission()
{
#ifdef CONFIG_UHK_USB_LEFT_RELAY
    assert(queued == 1 && direct == 0);
#else
    assert(direct == 1 && queued == 0);
#endif
    assert(released == 0);
}
int main()
{
    int (*send[])(const fake_report *) = {sendKeyboard, sendMouse, sendControls};
    fake_state *states[] = {&UsbSemaphore.keyboard, &UsbSemaphore.mouse, &UsbSemaphore.controls};
    fake_report report = {1};
    for (unsigned i = 0; i < 3; ++i) {
        reset(true, true);
        assert(send[i](&report) == 0);
        assert(wakes == 1 && queued == 0 && direct == 0);
        assert(released == 1 && releasedState == states[i]);
        reset(false, true);
        assert(send[i](&report) == 0 && wakes == 0);
        checkOneSubmission();
        reset(true, false);
        submitResult = -EBUSY;
        assert(send[i](&report) == -EBUSY && wakes == 1);
        checkOneSubmission();
        reset(true, true, false);
        assert(send[i](&report) == -ECONNRESET);
        assert(wakes == 0 && queued == 0 && direct == 0 && released == 0);
    }
    reset(true, true);
    report.buttons = 0;
    assert(sendMouse(&report) == 0 && wakes == 0);
    checkOneSubmission();
    puts("right USB wake/drop and single-submission routing passed");
}
