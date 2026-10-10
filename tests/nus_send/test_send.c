#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_LINK_PACKET_LENGTH 128
#define K_MSEC(ms) (ms)
typedef struct {
    const uint8_t *data;
    uint16_t len;
    uint8_t messageId[2], idsUsed, src, dst, wm;
} message_t;

static unsigned permits = TEST_SERVER ? 2 : 1;
static unsigned sdkCalls, lastTimeout;
static struct k_sem { int unused; } nusBusy;
static int k_sem_take(struct k_sem *sem, unsigned timeout) {
    (void)sem;
    lastTimeout = timeout;
    if (!permits) {
        return -EAGAIN;
    }
    --permits;
    return 0;
}
static void k_sem_give(struct k_sem *sem) {
    (void)sem;
    ++permits;
}
static void Messenger_UpdateWatermarks(message_t *msg) { ++msg->wm; }
static void Trace_Printc(const char *text) { (void)text; }
static void LOG_WRN(const char *fmt, ...) { (void)fmt; }

/* Preserve the release-build macro's log-and-continue timeout behavior
 * when testing unchanged source. */
#define SEM_TAKE(sem) ((void)k_sem_take(sem, K_MSEC(256)))

static int sdkResult;
static uint8_t submitted[MAX_LINK_PACKET_LENGTH];
static uint16_t submittedLen;
#if TEST_SERVER
struct bt_conn { int unused; };
static struct bt_conn conn;
static int bt_nus_send(struct bt_conn *peer, const uint8_t *data, uint16_t len) {
    assert(peer == &conn);
    ++sdkCalls;
    if (!sdkResult) {
        memcpy(submitted, data, len);
        submittedLen = len;
    }
    return sdkResult;
}
#include "send_under_test.inc"
static int sendMessage(message_t *msg) { return NusServer_SendMessageTo(msg, &conn); }
#else
static struct bt_nus_client { int unused; } nus_client;
static const uint8_t *retainedData;
static uint16_t retainedLen;
static int bt_nus_client_send(struct bt_nus_client *client, const uint8_t *data, uint16_t len) {
    (void)client;
    ++sdkCalls;
    if (!sdkResult) {
        retainedData = data;
        retainedLen = len;
        memcpy(submitted, data, len);
        submittedLen = len;
    }
    return sdkResult;
}
#include "send_under_test.inc"
static int sendMessage(message_t *msg) { return NusClient_SendMessage(msg); }
#endif

int main(void) {
    uint8_t payload[] = {1, 2, 3, 4, 5};
    /* 1 is the existing StateSync message ID. */
    message_t msg = {.data = payload, .len = sizeof(payload), .src = 20,
                     .dst = 21, .idsUsed = 1, .messageId = {1}};
    unsigned capacity = permits;

#if TEST_LIFETIME
    assert(sendMessage(&msg) == 0);
    uint8_t expected[] = {20, 21, 1, 1, 1, 2, 3, 4, 5};
    assert(retainedLen == sizeof(expected));
    /* Read after return, as permitted by the asynchronous API contract. */
    assert(!memcmp(retainedData, expected, sizeof(expected)));
    assert(permits == capacity - 1);
    payload[0] = 9;
    unsigned calls = sdkCalls;
    assert(sendMessage(&msg) == -EAGAIN);
    assert(sdkCalls == calls && !memcmp(retainedData, expected, sizeof(expected)));
    k_sem_give(&nusBusy); /* Simulate the sent callback. */
    assert(sendMessage(&msg) == 0);
    assert(retainedData[4] == 9);
    puts("client payload survives return and cannot be overwritten while busy");
#else
    /* A missed or delayed completion must not allow another submission. */
    permits = 0;
    sdkResult = TEST_SERVER ? 0 : -EALREADY;
    assert(sendMessage(&msg) == -EAGAIN);
    assert(sdkCalls == 0 && permits == 0 && msg.wm == 0 && lastTimeout == 256);

    /* Submission failures give back exactly the slot that was acquired. */
    permits = capacity;
    sdkResult = -EIO;
    assert(sendMessage(&msg) == -EIO);
    assert(sdkCalls == 1 && permits == capacity && msg.wm == 1);

    /* A valid send holds a slot until its completion callback. */
    sdkResult = 0;
    assert(sendMessage(&msg) == 0);
    uint8_t expected[] = {20, 21, 2, 1, 1, 2, 3, 4, 5};
    assert(submittedLen == sizeof(expected));
    assert(!memcmp(submitted, expected, sizeof(expected)));
    assert(permits == capacity - 1);
    k_sem_give(&nusBusy);
    assert(permits == capacity);

    /* Oversized packets must not consume a slot or call the Bluetooth API. */
    msg.len = MAX_LINK_PACKET_LENGTH;
    assert(sendMessage(&msg) != 0);
    assert(sdkCalls == 2 && permits == capacity && msg.wm == 2);
    puts("timeouts, send errors, completions and length rejection preserve ownership");
#endif
}
