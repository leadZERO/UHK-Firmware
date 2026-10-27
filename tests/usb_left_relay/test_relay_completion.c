/* Reuse the encoded two-endpoint relay/USB fakes; the new test adds the actual
 * sender retry policy, report comparison, semaphore and UHK delivery callback.
 * GPIO/report construction, scheduler, radio/USB timing and recorder are mocked. */
#include "usb_left_relay.h"
#define main protocolSuiteMain
#define delivered fakeRelayDelivered
#include "test_relay.c"
#undef delivered
#undef main
#include "usb_semaphore.h"
#include <errno.h>

hid_keyboard_report_t keyboardReports[2], *ActiveKeyboardReport;
hid_mouse_report_t mouseReports[2], *ActiveMouseReport;
hid_controls_report_t controlsReports[2], *ActiveControlsReport;
#include "buffers_under_test.inc"
report_send_states_t UsbSemaphore = {
    .keyboard.switchActiveReport = UsbReportUpdater_SwitchActiveKeyboardReport,
    .mouse.switchActiveReport = UsbReportUpdater_SwitchActiveMouseReport,
    .controls.switchActiveReport = UsbReportUpdater_SwitchActiveControlsReport,
};
static fake_t right, left;
static uint32_t now;
static bool failSend, RuntimeMacroRecordingBlind;
static uint32_t UpdateUsbReports_LastUpdateTime, UsbReportUpdater_LastActivityTime,
    UsbReportUpdater_LastMouseActivityTime;
static unsigned scheduled, ultimateFailures, events, confirmed;
enum { EventSchedulerEvent_Postponer, EventVector_SendUsbReports = 1, EventVector_ResendUsbReports = 2 };
enum { PowerMode_LastAwake = 0 };
static unsigned CurrentPowerMode;
#define LOG_WRN(...) ((void)0)
#define DEBUG_KEY_LIFE(...) ((void)0)
#define KEY_TIMING(...) ((void)0)
static uint32_t Timer_GetCurrentTime(void) { return now; }
static uint32_t Timer_GetElapsedTime(const uint32_t *then) { return now - *then; }
static void EventVector_Set(unsigned mask) { events |= mask; }
static void EventVector_Unset(unsigned mask) { events &= ~mask; }
static void EventScheduler_Schedule(uint32_t time, unsigned event, const char *reason)
{ (void)time; (void)event; (void)reason; ++scheduled; }
static void handleUltimateFail(errno_t err) { assert(err); ++ultimateFailures; }
static void MacroRecorder_RecordBasicReport(const hid_keyboard_report_t *report) { (void)report; }
static void Debug_RecordBleSendResult(int result) { (void)result; }
static void clearMouseMovement(void)
{
    ActiveMouseReport->x = ActiveMouseReport->y = 0;
    ActiveMouseReport->wheelX = ActiveMouseReport->wheelY = 0;
}
static void UsbScheduler_ReportDelivered(report_sink_t sink)
{ assert(sink == ReportSink_UsbLeft); ++confirmed; }
bool UsbLeft_Pending(void)
{
    for (unsigned i = 0; i < RelayKind_Count; ++i)
        if (right.relay.slots[i].pending) return true;
    return false;
}
errno_t Hid_SendKeyboardReport(const hid_keyboard_report_t *report)
{
    if (failSend) return -EHOSTUNREACH;
    return Relay_SendReport(&right.relay, RelayKind_Keyboard, (const uint8_t *)report) ? 0 : -EHOSTUNREACH;
}
errno_t Hid_SendMouseReport(const hid_mouse_report_t *report)
{
    if (failSend) return -EHOSTUNREACH;
    return Relay_SendReport(&right.relay, RelayKind_Mouse, (const uint8_t *)report) ? 0 : -EHOSTUNREACH;
}
errno_t Hid_SendControlsReport(const hid_controls_report_t *report)
{
    if (failSend) return -EHOSTUNREACH;
    return Relay_SendReport(&right.relay, RelayKind_Controls, (const uint8_t *)report) ? 0 : -EHOSTUNREACH;
}
#include "comparisons_under_test.inc"
#include "sender_under_test.inc"
#include "semaphore_under_test.inc"
#include "relay_delivery_under_test.inc"

static void reset(void)
{
    memset(keyboardReports, 0, sizeof(keyboardReports));
    memset(mouseReports, 0, sizeof(mouseReports));
    memset(controlsReports, 0, sizeof(controlsReports));
    ActiveKeyboardReport = keyboardReports;
    ActiveMouseReport = mouseReports;
    ActiveControlsReport = controlsReports;
    UsbSemaphore.keyboard.retries = UsbSemaphore.mouse.retries = UsbSemaphore.controls.retries = 0;
    UsbSemaphore.keyboard.needsResending = UsbSemaphore.mouse.needsResending = UsbSemaphore.controls.needsResending = false;
    UsbSemaphore_Clear();
    UsbReportSender_GivenUp = failSend = RuntimeMacroRecordingBlind = false;
    scheduled = ultimateFailures = events = confirmed = 0;
    initPair(&right, &left, &now);
    right.relay.platform.delivered = delivered;
    UpdateUsbReports_LastUpdateTime = now;
}
static void sendDesired(void)
{
    assert(UsbSemaphore_RecalculateIsReady());
    UpdateUsbReports_LastUpdateTime = now;
    sendActiveReports(false);
}
static void exhaustRetries(void)
{
    /* A real report-send failure reaches give-up, not a hand-edited latch.
     * A later fresh input is accepted on the same recovered relay route. */
    failSend = true;
    ActiveKeyboardReport->modifiers = HID_KEYBOARD_MODIFIER_LEFTGUI;
    for (unsigned i = 0; i <= THROTTLE_MAX_RETRIES; ++i)
        sendActiveReports(i != 0);
    assert(ultimateFailures == 1 && UsbReportSender_GivenUp);
    assert(!UsbSemaphore.keyboard.inFlight && !UsbSemaphore.keyboard.needsResending);
    assert(!memcmp(left.relay.slots[0].completed_report, (uint8_t[29]){0}, 29));
    failSend = false;
    memset(ActiveKeyboardReport, 0, sizeof(*ActiveKeyboardReport));
}
static void injectDone(uint8_t kind, uint64_t token, uint32_t sequence)
{
    relay_message_t m = {.op = RelayOp_Done, .token = token, .kind = kind,
                         .sequence = sequence, .status = RelayStatus_Ok};
    uint8_t packet[USB_LEFT_RELAY_MAX_PACKET];
    size_t size = RelayProtocol_Encode(&m, packet, sizeof(packet));
    assert(size);
    Relay_Receive(&right.relay, packet, size);
}
static void keyboardRelease(bool modifier, bool afterGiveUp)
{
    reset();
    if (afterGiveUp) exhaustRetries();
    if (modifier) ActiveKeyboardReport->modifiers = HID_KEYBOARD_MODIFIER_LEFTGUI;
    else ActiveKeyboardReport->bitfield[4] = 0x10; // HID Enter, matching live capture.
    hid_keyboard_report_t pressed = *ActiveKeyboardReport;
    left.drop_done = 1; // Exercise a retry without ever rebuilding a pending report.
    sendDesired();
    assert(UsbLeft_Pending());
    assert(UsbSemaphore.keyboard.inFlight == !afterGiveUp);
    now += USB_SEMAPHORE_TIMEOUT + 1;
    assert(!UsbSemaphore_RecalculateIsReady());
    assert(!memcmp(ActiveKeyboardReport, &pressed, sizeof(pressed)));
    step(&right, &left, &now, 70);
    assert(!UsbLeft_Pending());
    assert(!memcmp(left.relay.slots[0].completed_report, &pressed, sizeof(pressed)));
    /* Ordinary report construction after physical release, only AFTER pending
     * gate opens. No forced buffer overwrite, timeout bypass or fake callback. */
    memset(ActiveKeyboardReport, 0, sizeof(*ActiveKeyboardReport));
    sendDesired();
    step(&right, &left, &now, 70);
    bool released = !memcmp(left.relay.slots[0].completed_report, (uint8_t[29]){0}, 29);
    printf("%s%s: host_released=%d given_up=%d confirmations=%u\n",
           afterGiveUp ? "after give-up " : "ordinary ", modifier ? "GUI" : "Enter",
           released, UsbReportSender_GivenUp, confirmed);
    fflush(stdout);
    assert(released);
    assert(!UsbReportSender_GivenUp && confirmed == 2);
    memset(ActiveKeyboardReport, 0, sizeof(*ActiveKeyboardReport));
    sendDesired(); // The next idle rebuild agrees with the delivered all-up baseline.
    assert(!KeyboardReport_HasChange(keyboardReports));
    assert(!UsbLeft_Pending());
    /* A duplicate DONE cannot swap the now-current all-up baseline again. */
    hid_keyboard_report_t *active = ActiveKeyboardReport;
    injectDone(RelayKind_Keyboard, right.relay.token, right.relay.slots[0].sequence);
    assert(confirmed == 2 && active == ActiveKeyboardReport);
    ActiveKeyboardReport->bitfield[0] = 1;
    sendDesired();
    uint32_t seq = right.relay.slots[0].sequence;
    injectDone(RelayKind_Keyboard, right.relay.token, seq - 1);
    injectDone(RelayKind_Keyboard, right.relay.token ^ 1, seq);
    assert(confirmed == 2 && UsbLeft_Pending() && UsbSemaphore.keyboard.inFlight);
    step(&right, &left, &now, 10);
    assert(confirmed == 3);
}
static void buttonAndControlsRelease(uint8_t kind)
{
    reset(); exhaustRetries();
    if (kind == RelayKind_Mouse) ActiveMouseReport->buttons = 1;
    else ActiveControlsReport->consumer[0] = 0xE9; // volume up
    sendDesired();
    assert(UsbLeft_Pending() && !UsbSemaphore_AnyInFlight());
    step(&right, &left, &now, 10);
    assert(!UsbReportSender_GivenUp && confirmed == 1);
    if (kind == RelayKind_Mouse) memset(ActiveMouseReport, 0, sizeof(*ActiveMouseReport));
    else memset(ActiveControlsReport, 0, sizeof(*ActiveControlsReport));
    sendDesired(); step(&right, &left, &now, 10);
    assert(confirmed == 2);
    assert(!memcmp(left.relay.slots[kind - 1].completed_report, (uint8_t[29]){0}, RelayProtocol_ReportSize(kind)));
}
int main(void)
{
    keyboardRelease(false, false);
    keyboardRelease(true, false);
    keyboardRelease(false, true);
    keyboardRelease(true, true);
    buttonAndControlsRelease(RelayKind_Mouse);
    buttonAndControlsRelease(RelayKind_Controls);
    puts("relay completion after sender give-up regression passed");
}
