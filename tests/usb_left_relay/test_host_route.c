/* Host-side harness for the actual coordinator, with deterministic platform hooks. */
#include "host_input_gate.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __USB_LEFT_RELAY_UHK_H__
#define __CONNECTIONS_H__
#define __DEVICE_H__
#define __EVENT_SCHEDULER_H__
#define __TIMER_H__
#define __HOST_CONNECTION_H__
#define __BT_CONN_H__
#define __USB_STATE_H__
#define DEVICE_IS_UHK80_RIGHT 1
#define __ZEPHYR__ 1
#define SERIALIZED_HOST_CONNECTION_COUNT_MAX 22
#define HOST_CONNECTION_COUNT_MAX 24
#define ConnectionId_HostConnectionFirst 1
#define PeerIdUnknown 0
#define ConnectionState_Disconnected 0

enum {
    ConnectionType_Unknown,
    ConnectionType_Empty,
    ConnectionType_UsbHidRight,
    ConnectionType_UsbHidLeft,
    ConnectionType_BtHid
};
enum {
    HostConnectionType_Empty = ConnectionType_Empty,
    HostConnectionType_UsbHidRight = ConnectionType_UsbHidRight,
    HostConnectionType_UsbHidLeft = ConnectionType_UsbHidLeft
};
struct {
    uint8_t type;
} HostConnections[HOST_CONNECTION_COUNT_MAX];
struct {
    uint8_t peerId, state;
} Connections[HOST_CONNECTION_COUNT_MAX + 1];
uint8_t CurrentHostConnectionId;
bool UsbState_HostIsSuspended, UsbState_TransportUp;
static bool available, awake, active, quiescent, drained, neutral, usbNeutral, wanted;
static bool physicalHeld[2];
static uint8_t held[1];
static uint32_t clockMs, commits, resets;
static uint32_t usbGeneration, observedLocalUsbGeneration, quiesceCalls, usbNeutralCalls;
static uint8_t lastCommitted;
static bool lastExplicit;
static bool wakeArmed;
static bool wakeQueued;
static unsigned wakeRequests;
static uint32_t wakeEpochValue;
#ifdef WAKE_INPUT_TEST
static void resetWakeInput(bool suppressHeld);
#endif
uint8_t Connections_Type(uint8_t host)
{
    assert(host > 0 && host <= 24);
    return HostConnections[host - 1].type;
}
void Connections_SetStateAsync(uint8_t host, uint8_t state)
{
    Connections[host].state = state;
}
void Connections_CommitHost(uint8_t host, bool explicitPick)
{
    assert(drained && quiescent);
    CurrentHostConnectionId = host;
    ++commits;
    lastCommitted = host;
    lastExplicit = explicitPick;
}
void EventVector_WakeMain(void) {}
uint32_t Timer_GetCurrentTime(void)
{
    return clockMs;
}
bool UsbLeft_Active(void)
{
    return active;
}
bool UsbLeft_Quiescent(void)
{
    return quiescent;
}
bool UsbLeft_Connected(void)
{
    return available;
}
bool UsbLeft_Awake(void)
{
    return awake;
}
bool UsbLeft_Initialized(void)
{
    return true;
}
void UsbLeft_Select(bool selected)
{
    if (wanted != selected) { ++wakeEpochValue; }
    if (!selected) { wakeQueued = false; }
    wanted = selected;
}
/* Adapter boundary: the separate relay test exercises real peer/USB guards. */
const void *UsbLeft_GetState(void) { return NULL; }
bool Relay_CanWake(const void *state)
{
    (void)state;
    return wanted && available && !awake && wakeArmed;
}
bool UsbLeft_RequestWake(void)
{
    if (!Relay_CanWake(NULL)) { return false; }
    ++wakeRequests;
    wakeQueued = true;
    return true;
}
uint32_t UsbLeft_WakeEpoch(void) { return wakeEpochValue; }
bool Hid_LocalUsbQuiesce(void)
{
    ++quiesceCalls;
    return drained;
}
bool Hid_NeutralizeCurrentHost(void)
{
    return Connections_Type(CurrentHostConnectionId) == ConnectionType_UsbHidLeft ? quiescent
                                                                                  : neutral;
}
bool Hid_NeutralizeLocalUsb(void)
{
    ++usbNeutralCalls;
    return usbNeutral;
}
uint32_t Hid_LocalUsbGeneration(void)
{
    return usbGeneration;
}
void Hid_BeginNeutralization(void) {}
void UsbReportUpdater_ResetHostInput(bool suppressHeld)
{
#ifdef WAKE_INPUT_TEST
    resetWakeInput(suppressHeld);
#endif
    ++resets;
    if (suppressHeld)
        for (uint8_t i = 0; i < 2; ++i)
            HostInputGate_Capture(held, i, physicalHeld[i]);
}

#include "../../device/src/host_route.c"

static void observeUsbGeneration(void)
{
#include "usb_generation_under_test.inc"
}

static void resetPlatform(void)
{
    memset(HostConnections, 0, sizeof(HostConnections));
    memset(Connections, 0, sizeof(Connections));
    HostConnections[0].type = HostConnectionType_UsbHidRight;
    HostConnections[1].type = HostConnectionType_UsbHidLeft;
    CurrentHostConnectionId = 1;
    available = awake = quiescent = drained = neutral = usbNeutral = true;
    active = wanted = UsbState_HostIsSuspended = false;
    UsbState_TransportUp = true;
    memset(held, 0, sizeof(held));
    memset(physicalHeld, 0, sizeof(physicalHeld));
    clockMs = commits = resets = 0;
    target = phase = 0;
    started = 0;
    explicitSelection = wasActive = configWaiting = configReady = deferredUsbNeutral = false;
    deferredUsbNeutralDrained = false;
    oldLeftSlots = 0;
    routeGeneration = 0;
    usbGeneration = observedLocalUsbGeneration = quiesceCalls = usbNeutralCalls = 0;
    wakeArmed = true;
    wakeQueued = false;
    wakeRequests = wakeEpochValue = 0;
}

/* Exercise the actual switchHost command and route coordinator. Only argument
 * parsing and the existing host-selection entry points are platform fakes. */
#include "macro_result_under_test.inc"
typedef struct { const char *argument; bool consumed; } parser_context_t;
static bool Macros_DryRun;
static unsigned macroSelections;
static bool Macros_IsNUM(parser_context_t *ctx)
{
    return ctx->argument[0] >= '0' && ctx->argument[0] <= '9';
}
static int32_t Macros_ConsumeInt(parser_context_t *ctx)
{
    ctx->consumed = true;
    return strtol(ctx->argument, NULL, 10);
}
static bool ConsumeToken(parser_context_t *ctx, const char *token)
{
    if (strcmp(ctx->argument, token)) { return false; }
    ctx->consumed = true;
    return true;
}
static void Macros_ConsumeStringToken(parser_context_t *ctx) { ctx->consumed = true; }
static void selectMacroHost(uint8_t connection)
{
    ++macroSelections;
#ifdef CONFIG_UHK_USB_LEFT_RELAY
    if (HostRoute_Request(connection, true)) { return; }
#endif
    Connections_CommitHost(connection, true);
}
static void HostConnections_SelectByHostConnIndex(uint8_t index) { selectMacroHost(index + 1); }
static void HostConnections_SelectByName(parser_context_t *ctx)
{
    if (!strcmp(ctx->argument, "USB Left")) { selectMacroHost(2); }
}
/* These parser branches are compiled but not needed by the route matrix below. */
static void HostConnections_SelectNextConnection(void) { assert(false); }
static void HostConnections_SelectPreviousConnection(void) { assert(false); }
static void HostConnections_SelectNextActiveConnection(void) { assert(false); }
static void HostConnections_SelectPreviousActiveConnection(void) { assert(false); }
static void HostConnections_SelectLastConnection(void) { assert(false); }
static void HostConnections_SelectLastSelectedConnection(void) { assert(false); }
#include "switch_host_under_test.inc"

static void switchMacroYieldsOnlyForPendingRoute(void)
{
    const struct {
        uint8_t from, to;
        const char *argument;
        bool pending, valid;
    } cases[] = {
        {1, 2, "USB Left", true, true},
        {2, 1, "1", true, true},
        {1, 3, "3", false, true},
        {3, 1, "1", false, true},
        {1, 1, "1", false, true},
        {2, 2, "USB Left", false, true},
        {1, 1, "missing-host", false, false},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        resetPlatform();
        HostConnections[2].type = ConnectionType_BtHid;
        CurrentHostConnectionId = cases[i].from;
        active = CurrentHostConnectionId == 2;
        macroSelections = 0;
        parser_context_t ctx = {.argument = cases[i].argument};
        macro_result_t result = processSwitchHostCommand(&ctx);
        bool pending = false;
#ifdef CONFIG_UHK_USB_LEFT_RELAY
        pending = cases[i].pending;
#endif
        assert(ctx.consumed && (result & MacroResult_ActionFinishedFlag));
        assert(!!(result & MacroResult_YieldFlag) == pending);
        assert(HostRoute_Pending() == pending);
        assert(macroSelections == cases[i].valid);
        assert(CurrentHostConnectionId == (pending ? cases[i].from : cases[i].to));
        active = true; // Matching new-route preparation completes.
        HostRoute_Process();
        assert(!HostRoute_Pending() && CurrentHostConnectionId == cases[i].to);
        assert(macroSelections == cases[i].valid); // Command was consumed once.
    }
    resetPlatform();
    macroSelections = 0;
    Macros_DryRun = true;
    parser_context_t ctx = {.argument = "2"};
    assert(processSwitchHostCommand(&ctx) == MacroResult_Finished);
    assert(ctx.consumed && !macroSelections && !HostRoute_Pending());
    Macros_DryRun = false;
}
static void usbReconnectMustDeliverAllUp(void)
{
    resetPlatform();
    physicalHeld[0] = true;
    drained = usbNeutral = false;
    usbGeneration = 4;
    observeUsbGeneration();
    assert(resets == 1 && HostRoute_Blocked() && HostRoute_Transitioning());
    assert(HostInputGate_Suppressed(held, 0, false));
    clockMs = 10000;
    HostRoute_Process();
    assert(HostRoute_Blocked() && !HostRoute_Transitioning() && usbNeutralCalls == 0);
    drained = true;
    HostRoute_Process();
    assert(HostRoute_Blocked() && usbNeutralCalls == 1);
    uint32_t provenDrainCalls = quiesceCalls;
    HostRoute_Process();
    assert(HostRoute_Blocked() && quiesceCalls == provenDrainCalls);
    usbNeutral = true;
    HostRoute_Process();
    assert(!HostRoute_Blocked() && CurrentHostConnectionId == 1 && commits == 0);
    assert(!HostInputGate_Suppressed(held, 0, true));
    uint32_t completedResets = resets;
    observeUsbGeneration();
    assert(resets == completedResets && !HostRoute_Blocked());

    // Reconfiguring the unused local port must not discard input routed left.
    resetPlatform();
    CurrentHostConnectionId = 2;
    active = true;
    usbGeneration = 4;
    observeUsbGeneration();
    assert(resets == 0 && !HostRoute_Blocked());
}
static void healthySwitchAndHeldKeys(void)
{
    resetPlatform();
    physicalHeld[0] = true;
    assert(HostRoute_Request(2, true) && CurrentHostConnectionId == 1 && HostRoute_Blocked());
    drained = false;
    clockMs = 20;
    HostRoute_Process();
    assert(commits == 0);
    drained = true;
    neutral = false;
    HostRoute_Process();
    assert(commits == 0);
    neutral = true;
    HostRoute_Process();
    assert(wanted && commits == 0);
    active = true;
    quiescent = true;
    clockMs = 40;
    HostRoute_Process();
    assert(CurrentHostConnectionId == 2 && commits == 1 && lastExplicit && !HostRoute_Blocked());
    assert(HostInputGate_Suppressed(held, 0, false));
    assert(!HostInputGate_Suppressed(
        held, 1, false)); // A distinct key with the same HID usage remains usable.
    assert(!HostInputGate_Suppressed(held, 0, true));
    HostRoute_Process();
    assert(commits == 1);
}
static void offlineAndPreparationDeadline(void)
{
    resetPlatform();
    available = awake = false;
    assert(HostRoute_Request(2, true));
    HostRoute_Process();
    assert(CurrentHostConnectionId == 2 && commits == 1 && HostRoute_Blocked() &&
           !HostRoute_Pending());
    physicalHeld[1] = true;
    available = awake = active = true;
    HostRoute_Process();
    assert(!HostRoute_Blocked() && HostInputGate_Suppressed(held, 1, false));
    resetPlatform();
    assert(HostRoute_Request(2, true));
    HostRoute_Process();
    clockMs = 101;
    HostRoute_Process();
    assert(CurrentHostConnectionId == 2 && HostRoute_Blocked() && !HostRoute_Transitioning());
}
static void latestRequestWinsAndFailedDrain(void)
{
    resetPlatform();
    assert(HostRoute_Request(2, true));
    HostRoute_Process();
    assert(wanted && commits == 0);
    uint32_t oldGeneration = HostRoute_Generation();
    quiescent = false;
    assert(HostRoute_Request(1, true));
    assert(HostRoute_Generation() != oldGeneration);
    HostRoute_Process();
    assert(!wanted && commits == 0 && HostRoute_Target() == 1);
    clockMs = 10000;
    HostRoute_Process();
    assert(
        commits == 0 && HostRoute_Pending() && !HostRoute_Transitioning() && HostRoute_Blocked());
    quiescent = true;
    HostRoute_Process();
    assert(commits == 1 && lastCommitted == 1 && !HostRoute_Blocked());
}
static void sleepingOldUsbKeepsNeutralDebt(void)
{
    resetPlatform();
    neutral = usbNeutral = false;
    UsbState_HostIsSuspended = true;
    assert(HostRoute_Request(2, true));
    HostRoute_Process();
    active = true;
    clockMs = 40;
    HostRoute_Process();
    assert(
        commits == 1 && CurrentHostConnectionId == 2 && deferredUsbNeutral && !HostRoute_Blocked());
    active = false;
    assert(HostRoute_Request(1, true));
    HostRoute_Process();
    assert(CurrentHostConnectionId == 1 && HostRoute_Blocked());
    UsbState_HostIsSuspended = false;
    usbNeutral = true;
    HostRoute_Process();
    assert(!deferredUsbNeutral && !HostRoute_Blocked());
}
static void configReplacementIsBarrier(void)
{
    resetPlatform();
    CurrentHostConnectionId = 2;
    active = true;
    quiescent = false;
    assert(!HostRoute_PrepareConfig());
    HostRoute_Process();
    assert(HostRoute_Blocked() && CurrentHostConnectionId == 2 && !configReady);
    active = false;
    quiescent = true;
    HostRoute_Process();
    assert(configReady && HostRoute_Blocked() && HostRoute_PrepareConfig());
    HostConnections[1].type = HostConnectionType_Empty;
    Connections[2].state = 2;
    HostRoute_ConfigChanged();
    HostRoute_Process();
    assert(CurrentHostConnectionId == 1 && commits == 1 && Connections[2].state == 0 &&
           !HostRoute_Blocked());
}
static void explicitWakeSelection(void)
{
    resetPlatform();
    awake = false;
    assert(HostRoute_Request(2, true));
    HostRoute_Process();
    assert(wakeRequests == 1 && CurrentHostConnectionId == 2 && HostRoute_Blocked());
    for (unsigned i = 0; i < 100; ++i) { HostRoute_Process(); }
    assert(wakeRequests == 1); // Persistent selection is not new intent.
    assert(HostRoute_Request(2, true)); // Deliberate sleeping-host reselection.
    HostRoute_Process();
    assert(wakeRequests == 2);

    resetPlatform();
    awake = false;
    assert(HostRoute_Request(2, false));
    HostRoute_Process();
    assert(!wakeRequests);
    assert(HostRoute_PrepareConfig() == false);
    HostRoute_Process();
    assert(HostRoute_PrepareConfig());
    HostRoute_ConfigChanged();
    HostRoute_Process();
    assert(!wakeRequests);

    resetPlatform();
    awake = false;
    assert(HostRoute_Request(2, true));
    assert(HostRoute_Request(1, true));
    HostRoute_Process();
    assert(!wakeRequests); // Latest selection wins before old intent exists.

    resetPlatform();
    CurrentHostConnectionId = 2;
    active = wanted = wasActive = true;
    HostRoute_Process();
    active = awake = false;
    HostRoute_Process();
    assert(wanted && HostRoute_Blocked() && !wakeRequests);
    HostRoute_RequestWake();
    assert(wakeRequests == 1);
    wakeArmed = false;
    HostRoute_RequestWake();
    assert(wakeRequests == 1);
}
int main(void)
{
    resetPlatform();
    HostConnections[2].type = ConnectionType_BtHid;
    assert(!HostRoute_Request(3, true));
    assert(HostRoute_Generation() == 1 && !HostRoute_Pending());
    CurrentHostConnectionId = 3;
    assert(!HostRoute_Request(1, true));
    assert(HostRoute_Generation() == 2); // Legacy-route callbacks cannot advance a new route.
    healthySwitchAndHeldKeys();
    offlineAndPreparationDeadline();
    latestRequestWinsAndFailedDrain();
    sleepingOldUsbKeepsNeutralDebt();
    configReplacementIsBarrier();
    usbReconnectMustDeliverAllUp();
    switchMacroYieldsOnlyForPendingRoute();
    explicitWakeSelection();
    puts("host route tests passed");
    return 0;
}
