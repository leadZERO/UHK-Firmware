/* Actual sender, updater/debouncer/action dispatch and coordinator functions.
 * The postponer, report construction, scheduler, power and USB adapter are mocks.
 * The separate relay test covers encoded packets and both real relay roles. */
#define WAKE_INPUT_TEST
#define main hostRouteSuiteMain
#include "test_host_route.c"
#undef main
#define SRC_STR_UTILS_H_
typedef struct { const char *start, *end; } string_segment_t;
#include "key_action.h"
#include "key_states.h"
#include "secondary_role_driver.h"

typedef struct { struct { uint8_t modifiers; } basic; uint8_t inputModifiers; } usb_keyboard_reports_t;
typedef struct { uint8_t slotId, inSlotId; } key_coordinates_t;
typedef struct { bool pending; } postponer_buffer_record_type_t;
static postponer_buffer_record_type_t queued[SLOT_COUNT][MAX_KEY_COUNT_PER_MODULE];
key_state_t KeyStates[SLOT_COUNT][MAX_KEY_COUNT_PER_MODULE];
static key_action_cached_t actionCache[SLOT_COUNT][MAX_KEY_COUNT_PER_MODULE];
static key_definition_t CurrentKeymap[2][SLOT_COUNT][MAX_KEY_COUNT_PER_MODULE];
static usb_keyboard_reports_t NativeKeyboardReports;
static struct { uint8_t DebounceTimePress, DebounceTimeRelease;
    struct { uint8_t modifierLayerMask; } LayerConfig[2]; } Cfg;
static struct { bool keyLog; } Shell;
static uint8_t ActiveMouseStates[SerializedMouseAction_Last + 1], ToggledMouseStates[SerializedMouseAction_Last + 1];
#define ACTIVE_MOUSE_STATES_COUNT sizeof(ActiveMouseStates)
static uint8_t ActiveLayer, ActiveLayerModifierMask, InputModifiers, InputModifiersPrevious;
static uint8_t OutputModifiers, Postponer_LastKeyLayer = 255, Postponer_LastKeyMods;
static bool SuppressKeys, SuppressMods, InteractivePairingInProgress, TestSwitches;
static bool queueInput, replayInput, DisplaySleepModeActive, KeyBacklightSleepModeActive, Resending;
static unsigned CurrentPowerMode, UsbReportUpdateCounter, sentReports, inputUpdates, clearedMouse;
static uint32_t UpdateUsbReports_LastUpdateTime;
enum { PowerMode_LastAwake = 0, PowerMode_Awake = 0,
    PowerMode_LightSleep = 1, PowerMode_Lock = 2, EventSchedulerEvent_NativeActions = 1 };
enum { EventVector_NativeActionReportsUsed = 1 << 0, EventVector_NativeActions = 1 << 1,
    EventVector_StateMatrix = 1 << 2, EventVector_NativeActionsPostponing = 1 << 3,
    EventVector_SendUsbReports = 1 << 4, EventVector_MacroEngine = 1 << 5,
    EventVector_LayerHolds = 1 << 6, EventVector_MouseKeys = 1 << 7,
    EventVector_MouseController = 1 << 8, EventVector_SomeonePostponing = 1 << 9,
    EventVector_Postponer = 1 << 10, EventVector_ResendUsbReports = 1 << 11 };
static unsigned events;
static bool EventVector_IsSet(unsigned mask) { return events & mask; }
static void EventVector_Set(unsigned mask) { events |= mask; }
static void EventVector_Unset(unsigned mask) { events &= ~mask; }
#define Trace_Printc(...) ((void)0)
#define WATCH_TRIGGER(...) ((void)0)
#define DEBUG_KEY_LIFE(...) ((void)0)
#define KEY_TIMING(...) ((void)0)
#define EVENTLOOP_TIMING(...) ((void)0)
#define Log(...) ((void)0)
static void EventScheduler_Schedule(uint32_t time, unsigned event, const char *label) {}
static void PostponerQuery_InfoByKeystate(key_state_t *state, postponer_buffer_record_type_t **press,
                                        postponer_buffer_record_type_t **release)
{
    size_t index = state - &KeyStates[0][0];
    *press = (&queued[0][0])[index].pending ? &(&queued[0][0])[index] : NULL;
    *release = NULL;
}
#include "wake_sources_under_test.inc"
static void resetWakeInput(bool suppressHeld)
{
#include "capture_input_under_test.inc"
}
static void applyKeystroke(key_state_t *state, key_action_cached_t *action,
                          key_action_t *base, usb_keyboard_reports_t *reports) {}
static void resetStickyMods(key_action_cached_t *action) {}
static void MouseKeys_SetState(serialized_mouse_action_t action, bool lock, bool activate) {}
static void applyToggleLayerAction(key_state_t *state, key_action_t *action) {}
static void applyLayerHolds(key_state_t *state, key_action_t *action) {}
static void SwitchKeymapById(uint8_t id, bool explicitPick) {}
static void StickyMods_ResetLater(key_action_cached_t *action) {}
static void Macros_StartMacro(uint8_t id, key_state_t *state, uint16_t offset,
                             uint8_t activation, uint8_t layer, bool explicitPick, void *parent) {}
static void Macros_StartInlineMacro(const char *text, key_state_t *state, uint8_t activation) {}
static void applyConnectionAction(key_state_t *state, connection_action_t action, uint8_t host)
{
    if (KeyState_ActivatedNow(state)) { HostRoute_Request(host, true); }
}
static void applyOtherAction(other_action_t action) {}
static uint16_t Utils_KeyStateToKeyId(key_state_t *state) { return 0; }
static key_coordinates_t Utils_KeyIdToKeyCoordinates(uint16_t key) { return (key_coordinates_t){0}; }
static void Ledmap_ActivateTestled(uint8_t slot, uint8_t key) {}
static bool PostponerCore_EventsShouldBeQueued(void) { return queueInput; }
static void PostponerCore_TrackKeyEvent(key_state_t *state, bool active, uint8_t layer)
{
    (&queued[0][0])[state - &KeyStates[0][0]].pending = active;
}
static void Macros_WakeBecauseOfKeystateChange(void) {}
static void UsbReportUpdater_ResetKeyboardReports(usb_keyboard_reports_t *reports) {}
static void LayerSwitcher_ResetHolds(void) {}
static void KeyHistory_RecordPress(key_state_t *state) {}
static void KeyHistory_RecordRelease(key_state_t *state) {}
static void PowerMode_ActivateMode(unsigned mode, bool a, bool b, const char *reason) {}
static bool PostponerCore_IsActive(void) { return queueInput; }
static void handleEventInterrupts(key_state_t *state) {}
static void PostponerCore_UpdatePostponedTime(void) {}
static void Macros_ContinueMacro(void) {}
static void handleLayerChanges(void) {}
static void handleUsbStackTestMode(void) {}
static void PostponerCore_RunPostponedEvents(void)
{
    ++inputUpdates;
    if (!replayInput) { return; }
    for (unsigned slot = 0; slot < SLOT_COUNT; ++slot)
        for (unsigned key = 0; key < MAX_KEY_COUNT_PER_MODULE; ++key)
            if (queued[slot][key].pending) {
                KeyStates[slot][key].current = true;
                queued[slot][key].pending = false;
            }
}
static void MouseKeys_ProcessMouseActions(void) {}
static void MouseController_ProcessMouseActions(void) {}
static bool PostponerCore_IsNonEmpty(void) { return false; }
#include "wake_updater_under_test.inc"
static void clearMouseMovement(void) { ++clearedMouse; }
static bool blockedByReportThrottle(void) { return false; }
static void justPreprocessInput(bool force)
{
    for (unsigned slot = 0; slot < SLOT_COUNT; ++slot)
        for (unsigned key = 0; key < MAX_KEY_COUNT_PER_MODULE; ++key)
            preprocessKeyState(&KeyStates[slot][key], force);
}
static void UsbReportUpdater_MergeReports(void) {}
static void sendActiveReports(bool resending) { ++sentReports; }
static void LedManager_UpdateSleepModes(void) {}
#include "wake_sender_under_test.inc"

static key_action_t keyboard = {.type = KeyActionType_Keystroke, .keystroke.scancode = 4};
static void initSleeping(void)
{
    resetPlatform();
    memset(KeyStates, 0, sizeof(KeyStates));
    memset(queued, 0, sizeof(queued));
    memset(suppressedSources, 0, sizeof(suppressedSources));
    memset(wakeSources, 0, sizeof(wakeSources));
    memset(CurrentKeymap, 0, sizeof(CurrentKeymap));
    memset(actionCache, 0, sizeof(actionCache));
    memset(&Cfg, 0, sizeof(Cfg));
    CurrentHostConnectionId = 2;
    wanted = true;
    awake = false;
    SuppressKeys = SuppressMods = InteractivePairingInProgress = TestSwitches = false;
    queueInput = replayInput = false;
    wakeEpoch = wakeEpochValue = 1;
    sentReports = clearedMouse = inputUpdates = events = 0;
    CurrentPowerMode = PowerMode_Awake;
    HostRoute_Process();
}
static void update(void)
{
    ++clockMs;
    events |= EventVector_NativeActions;
    UsbReportSender_UpdateAndSendUsbReports();
}
static void press(unsigned slot, unsigned key, key_action_t action)
{
    CurrentKeymap[0][slot][key].action = action;
    KeyStates[slot][key].hardwareSwitchState = true;
    update();
}
static void freshKeysAndButtons(void)
{
    const key_action_t actions[] = {
        keyboard,
        {.type = KeyActionType_Keystroke, .keystroke.modifiers = 1},
        {.type = KeyActionType_Keystroke, .keystroke.secondaryRole = SecondaryRole_LeftShift},
        {.type = KeyActionType_Keystroke, .keystroke.keystrokeType = KeystrokeType_Media,
         .keystroke.scancode = 0xe9},
        {.type = KeyActionType_Mouse, .mouseAction = SerializedMouseAction_LeftClick},
        {.type = KeyActionType_Mouse, .mouseAction = SerializedMouseAction_Button_20},
    };
    for (unsigned slot = 0; slot < SLOT_COUNT; ++slot)
        for (unsigned action = 0; action < sizeof(actions) / sizeof(actions[0]); ++action) {
            initSleeping();
            press(slot, 0, actions[action]);
            assert(wakeRequests == 1 && inputUpdates == 1 && !sentReports && clearedMouse == 1);
            for (unsigned i = 0; i < 3; ++i) { update(); }
            assert(wakeRequests == 1); // Holding/recomputing is not another press.
            KeyStates[slot][0].hardwareSwitchState = false;
            update();
            assert(wakeRequests == 1);
            press(slot, 0, actions[action]);
            assert(wakeRequests == 2);
        }
}
static void ignoredActionsAndInput(void)
{
    const key_action_t actions[] = {
        {.type = KeyActionType_None}, {.type = KeyActionType_Keystroke},
        {.type = KeyActionType_Mouse, .mouseAction = SerializedMouseAction_MoveUp},
        {.type = KeyActionType_Mouse, .mouseAction = SerializedMouseAction_ScrollDown},
        {.type = KeyActionType_Mouse, .mouseAction = SerializedMouseAction_Accelerate},
        {.type = KeyActionType_PlayMacro}, {.type = KeyActionType_InlineMacro},
        {.type = KeyActionType_SwitchLayer}, {.type = KeyActionType_Other},
    };
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        initSleeping();
        press(0, 0, actions[i]);
        assert(!wakeRequests);
    }
    for (unsigned mode = 0; mode < 7; ++mode) {
        initSleeping();
        switch (mode) {
        case 0: wakeArmed = false; break;
        case 1: available = false; break;
        case 2: CurrentHostConnectionId = 1; wanted = false; break;
        case 3: InteractivePairingInProgress = true; break;
        case 4: TestSwitches = true; break;
        case 5: SuppressKeys = true; break;
        case 6: CurrentPowerMode = PowerMode_Lock; break;
        }
        press(0, 0, keyboard);
        assert(!wakeRequests);
    }
    initSleeping();
    key_state_t pointerKey = {.current = true, .hardwareSwitchState = true};
    key_action_cached_t cached = {.action = keyboard};
    ApplyKeyAction(&pointerKey, &cached, &keyboard, &NativeKeyboardReports);
    update(); // Neutral/no physical transitions do not wake either.
    assert(!wakeRequests);
}
static void heldAndPostponedInput(void)
{
    initSleeping();
    KeyStates[0][0].hardwareSwitchState = true;
    UsbReportUpdater_ResetHostInput(true);
    press(0, 0, keyboard);
    assert(!wakeRequests);
    KeyStates[0][0].hardwareSwitchState = false;
    update();
    press(0, 0, keyboard);
    assert(wakeRequests == 1);

    for (unsigned change = 0; change < 4; ++change) {
        initSleeping();
        queueInput = true;
        if (change == 0) { awake = true; }
        press(0, 0, keyboard);
        assert(!wakeRequests);
        if (change == 0) { awake = false; ++wakeEpochValue; }
        if (change == 1) { ++wakeEpochValue; } // Link/peer/generation or sleep change.
        if (change == 2) { UsbReportUpdater_ResetHostInput(true); }
        if (change == 3) { KeyStates[0][0].hardwareSwitchState = false; update(); }
        queueInput = false;
        replayInput = true;
        update();
        assert(!wakeRequests);
    }
    initSleeping();
    queueInput = true;
    press(0, 0, keyboard);
    queueInput = false;
    replayInput = true;
    update();
    assert(wakeRequests == 1); // Fresh postponed action in the same sleep may wake.
}
static void selectionActions(void)
{
    key_action_t select = {.type = KeyActionType_Connections,
        .connections.command = ConnectionAction_SwitchByHostConnectionId,
        .connections.hostConnectionId = 2};
    initSleeping();
    press(0, 0, select);
    assert(HostRoute_Pending() && !wakeRequests);
    HostRoute_Process();
    assert(wakeRequests == 1);
    update();
    HostRoute_Process();
    assert(wakeRequests == 1); // Held selection action is not a repeated wake.

    initSleeping();
    KeyStates[0][0].hardwareSwitchState = true;
    UsbReportUpdater_ResetHostInput(true);
    press(0, 0, select);
    HostRoute_Process();
    assert(!wakeRequests && !HostRoute_Pending());

    initSleeping();
    CurrentKeymap[0][0][0].action = keyboard;
    select.connections.hostConnectionId = 1;
    CurrentKeymap[0][0][1].action = select;
    KeyStates[0][0].hardwareSwitchState = KeyStates[0][1].hardwareSwitchState = true;
    update();
    assert(wakeRequests == 1 && !wakeQueued && HostRoute_Target() == 1);
    HostRoute_Process();
    assert(CurrentHostConnectionId == 1 && !wakeQueued);
}
int main(void)
{
    freshKeysAndButtons();
    ignoredActionsAndInput();
    heldAndPostponedInput();
    selectionActions();
    puts("blocked-route sender/updater wake tests passed");
    return 0;
}
