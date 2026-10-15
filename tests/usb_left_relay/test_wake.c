/* Real relay/protocol; the shared harness mocks packet delivery and USB only. */
#define main relaySuiteMain
#include "test_relay.c"
#undef main

static void suspendPair(fake_t *a, fake_t *b, uint32_t *now, bool armed)
{
    relay_usb_state_t usb = b->relay.usb;
    usb.flags |= RelayFlag_Suspended;
    if (armed) { usb.flags |= RelayFlag_Wakeup; }
    else { usb.flags &= ~RelayFlag_Wakeup; }
    Relay_SetUsb(&b->relay, b->relay.usb_generation, &usb);
    step(a, b, now, 20);
}

static void idleSuspend(void)
{
    fake_t a, b;
    uint32_t now;
    initPair(&a, &b, &now);
    /* A transfer held during suspend must drain without turning cleanup,
     * heartbeat/state traffic, or retained selection into wake intent. */
    uint8_t report[29] = {0, 1};
    assert(Relay_SendReport(&a.relay, 1, report));
    b.block_drain = true;
    suspendPair(&a, &b, &now, true);
    step(&a, &b, &now, 200);
    b.block_drain = false;
    step(&a, &b, &now, 15000);
    printf("idle suspend: wake_calls=%u wanted=%u\n", b.wake_calls, a.relay.wanted);
    fflush(stdout);
    assert(!b.wake_calls && a.relay.wanted && !a.relay.active && a.relay.quiescent);
    Relay_SetLink(&a.relay, false);
    Relay_SetLink(&b.relay, false);
    Relay_SetLink(&a.relay, true);
    Relay_SetLink(&b.relay, true);
    step(&a, &b, &now, 1200);
    assert(a.relay.negotiated && !b.wake_calls);
}

static relay_message_t wakeMessage(fake_t *a, fake_t *b)
{
    return (relay_message_t){.op = RelayOp_Wake,
        .right_boot = a->relay.boot, .left_boot = b->relay.boot,
        .challenge = b->relay.challenge, .usb_generation = b->relay.usb_generation,
        .selection = a->relay.usb.revision};
}

static void rapidSleepReplay(void)
{
    fake_t a, b;
    uint32_t now;
    initPair(&a, &b, &now);
    suspendPair(&a, &b, &now, true);
    relay_message_t stale = wakeMessage(&a, &b);
    unsigned before = b.wake_calls;
    relay_usb_state_t usb = b.relay.usb;
    usb.flags &= ~RelayFlag_Suspended;
    Relay_SetUsb(&b.relay, b.relay.usb_generation, &usb);
    usb.flags |= RelayFlag_Suspended;
    Relay_SetUsb(&b.relay, b.relay.usb_generation, &usb);
    assert(stale.right_boot == b.relay.peer_boot && stale.left_boot == b.relay.boot &&
           stale.challenge == b.relay.challenge && stale.usb_generation == b.relay.usb_generation);
    inject(&b.relay, &stale);
    printf("rapid sleep replay: extra_wake_calls=%u\n", b.wake_calls - before);
    fflush(stdout);
    assert(b.wake_calls == before);
}

#if !defined(WAKE_IDLE_ONLY) && !defined(WAKE_REPLAY_ONLY)
static void intentionalWakeAndCycles(void)
{
    fake_t a, b;
    uint32_t now;
    initPair(&a, &b, &now);
    assert(!Relay_RequestWake(&a.relay)); // Awake input is never retained.
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        suspendPair(&a, &b, &now, true);
        assert(b.wake_calls == cycle * 2);
        assert(Relay_RequestWake(&a.relay));
        relay_message_t duplicate = wakeMessage(&a, &b);
        step(&a, &b, &now, 20);
        assert(b.wake_calls == cycle * 2 + 1);
        inject(&b.relay, &duplicate);
        step(&a, &b, &now, 2000);
        assert(b.wake_calls == cycle * 2 + 1); // No automatic retry/duplicate wake.
        assert(Relay_RequestWake(&a.relay)); // A new press can retry a failed OS wake.
        step(&a, &b, &now, 20);
        assert(b.wake_calls == cycle * 2 + 2);
        unsigned neutral[3];
        memcpy(neutral, b.submissions, sizeof(neutral));
        relay_usb_state_t usb = b.relay.usb;
        usb.flags &= ~RelayFlag_Suspended;
        Relay_SetUsb(&b.relay, b.relay.usb_generation, &usb);
        step(&a, &b, &now, 600);
        assert(a.relay.active && b.relay.active);
        for (unsigned kind = 0; kind < 3; ++kind) {
            assert(b.submissions[kind] >= neutral[kind] + 1);
            assert(!b.relay.slots[kind].pending);
        }
    }
}

static void deniedAndExpiredRequests(void)
{
    fake_t a, b;
    uint32_t now;
    initPair(&a, &b, &now);
    suspendPair(&a, &b, &now, false);
    assert(!Relay_RequestWake(&a.relay));
    relay_message_t denied = wakeMessage(&a, &b);
    inject(&b.relay, &denied);
    assert(!b.wake_calls);
    suspendPair(&a, &b, &now, true);
    Relay_Select(&a.relay, false);
    assert(!Relay_RequestWake(&a.relay));
    Relay_Select(&a.relay, true);
    step(&a, &b, &now, 20);
    assert(!b.wake_calls);
    assert(Relay_RequestWake(&a.relay));
    a.reject_wake = true;
    step(&a, &b, &now, 1000);
    assert(a.wake_packets == 1 && !b.wake_calls);
    a.reject_wake = false;
    assert(Relay_RequestWake(&a.relay));
    step(&a, &b, &now, 10);
    assert(b.wake_calls == 1);

    initPair(&a, &b, &now);
    b.block_drain = true;
    suspendPair(&a, &b, &now, true);
    assert(Relay_RequestWake(&a.relay));
    step(&a, &b, &now, RELAY_REPORT_DEADLINE_MS + 10);
    b.block_drain = false;
    step(&a, &b, &now, 1000);
    assert(!b.wake_calls); // A blocked cleanup does not hold an intent forever.
    assert(Relay_RequestWake(&a.relay));
    step(&a, &b, &now, 10);
    assert(b.wake_calls == 1);
}

static void pendingWakeSurvivesOrdinaryCleanup(void)
{
    fake_t a, b;
    uint32_t now;
    initPair(&a, &b, &now);
    b.block_drain = true;
    suspendPair(&a, &b, &now, true);
    assert(Relay_RequestWake(&a.relay));
    step(&a, &b, &now, 10); // END causes ordinary cleanup revision changes.
    assert(a.relay.wake_pending && !b.wake_calls);
    b.block_drain = false;
    step(&a, &b, &now, 50);
    assert(b.wake_calls == 1 && !a.relay.wake_pending);
}

static void staleRequestsAndPackets(void)
{
    fake_t a, b;
    uint32_t now;
    for (unsigned change = 0; change < 6; ++change) {
        initPair(&a, &b, &now);
        suspendPair(&a, &b, &now, true);
        assert(Relay_RequestWake(&a.relay));
        relay_message_t stale = wakeMessage(&a, &b);
        relay_usb_state_t usb = b.relay.usb;
        switch (change) {
        case 0:
            Relay_Select(&a.relay, false);
            Relay_Select(&a.relay, true);
            break;
        case 1:
            /* No BEGIN, reset or new USB generation between sleep cycles. */
            usb.flags &= ~RelayFlag_Suspended;
            Relay_SetUsb(&b.relay, b.relay.usb_generation, &usb);
            usb.flags |= RelayFlag_Suspended;
            Relay_SetUsb(&b.relay, b.relay.usb_generation, &usb);
            pump(&a);
            break;
        case 2:
            Relay_SetUsb(&b.relay, b.relay.usb_generation + 1, &usb);
            pump(&a);
            break;
        case 3:
            Relay_SetLink(&a.relay, false);
            Relay_SetLink(&b.relay, false);
            Relay_SetLink(&a.relay, true);
            Relay_SetLink(&b.relay, true);
            break;
        case 4: {
            relay_platform_t platform = b.relay.platform;
            Relay_Init(&b.relay, false, &platform, &b);
            Relay_SetUsb(&b.relay, 1, &usb);
            Relay_SetLink(&b.relay, true);
            Relay_Tick(&a.relay, now += 1001); // Peer stale before a new handshake.
            break;
        }
        case 5:
            usb.flags &= ~RelayFlag_Wakeup;
            Relay_SetUsb(&b.relay, b.relay.usb_generation, &usb);
            pump(&a);
            break;
        }
        if (change != 0) { inject(&b.relay, &stale); }
        step(&a, &b, &now, 1200);
        assert(!b.wake_calls && !a.relay.wake_pending);
    }
    initPair(&a, &b, &now);
    suspendPair(&a, &b, &now, true);
    relay_message_t oldPeer = {.op = RelayOp_Hello, .right_boot = 9001,
        .schema = 0x01DD041Du, .capabilities = RelayCaps_All};
    inject(&b.relay, &oldPeer);
    assert(b.relay.peer_boot == a.relay.boot); // Old automatic-wake peer is incompatible.
}
#endif

int main(void)
{
#ifdef WAKE_REPLAY_ONLY
    (void)idleSuspend;
    rapidSleepReplay();
#else
    (void)rapidSleepReplay;
    idleSuspend();
#ifndef WAKE_IDLE_ONLY
    rapidSleepReplay();
    intentionalWakeAndCycles();
    deniedAndExpiredRequests();
    pendingWakeSurvivesOrdinaryCleanup();
    staleRequestsAndPackets();
#endif
#endif
    puts("left USB wake tests passed");
}
