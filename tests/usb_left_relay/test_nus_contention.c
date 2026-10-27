#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define MAX_LINK_PACKET_LENGTH 128
#define MessageId_UsbLeftRelay 8
#define K_NO_WAIT 0
#define K_MSEC(ms) (ms)
typedef struct {
    const uint8_t *data;
    uint16_t len;
    uint8_t messageId[2], idsUsed, src, dst, wm;
} message_t;
static unsigned permits = TEST_SERVER ? 2 : 1, sdkCalls, lastTimeout;
static struct k_sem { int unused; } nusBusy;
static int k_sem_take(struct k_sem *sem, unsigned timeout) {
    (void)sem; lastTimeout=timeout;
    if(!permits) return -EAGAIN;
    --permits; return 0;
}
static void k_sem_give(struct k_sem *sem) { (void)sem; ++permits; }
// Allow the runner to exercise the prior implementation: its timeout macro
// logs and continues even though it did not acquire a semaphore slot.
#define SEM_TAKE(sem) ((void)k_sem_take(sem,K_MSEC(256)))
static void Messenger_UpdateWatermarks(message_t *msg) { ++msg->wm; }
static void Trace_Printc(const char *text) { (void)text; }
static void LOG_WRN(const char *fmt, ...) { (void)fmt; }
static int sdkResult;
#if TEST_SERVER
struct bt_conn { int unused; };
static struct bt_conn conn;
static int bt_nus_send(struct bt_conn *peer, const uint8_t *data, uint16_t len) {
    assert(peer==&conn && data && len); ++sdkCalls; return sdkResult;
}
#include "nus_server_under_test.inc"
static int send(message_t *msg) { return NusServer_SendMessageTo(msg,&conn); }
#else
static struct bt_nus_client { int unused; } nus_client;
static const uint8_t *retainedData;
static uint16_t retainedLen;
static int bt_nus_client_send(struct bt_nus_client *client, const uint8_t *data, uint16_t len) {
    (void)client; ++sdkCalls;
    if(!sdkResult) { retainedData=data; retainedLen=len; }
    return sdkResult;
}
#include "nus_client_under_test.inc"
static int send(message_t *msg) { return NusClient_SendMessage(msg); }
#endif
int main(void) {
    uint8_t payload[5]={1,2,3,4,5};
    message_t msg={.data=payload,.len=5,.src=20,.dst=21,.idsUsed=1,.messageId={MessageId_UsbLeftRelay}};
    unsigned slots=permits;
    for(unsigned i=0;i<slots;++i) assert(send(&msg)==0);
    assert(!permits && sdkCalls==slots);
#if !TEST_SERVER
    uint8_t expected[9]={20,21,1,MessageId_UsbLeftRelay,1,2,3,4,5};
    assert(retainedLen==sizeof(expected) && !memcmp(retainedData,expected,sizeof(expected)));
#endif
    payload[0]=99;
    assert(send(&msg)==-EAGAIN && lastTimeout==K_NO_WAIT);
    assert(!permits && sdkCalls==slots); // Never submit or release a slot we didn't acquire.
#if !TEST_SERVER
    assert(!memcmp(retainedData,expected,sizeof(expected))); // First write still owns its bytes.
#endif
    msg.messageId[0]=1;
    assert(send(&msg)==-EAGAIN && lastTimeout==256);
    assert(!permits && sdkCalls==slots); // Legacy bounded wait failure is also an error.
    k_sem_give(&nusBusy); // Successful asynchronous completion releases its slot.
    msg.messageId[0]=MessageId_UsbLeftRelay;
    sdkResult=-ENOTCONN;
    assert(send(&msg)==-ENOTCONN && permits==1); // Immediate SDK rejection returns ownership.
    sdkResult=0;
    msg.len=128;
    unsigned calls=sdkCalls;
    assert(send(&msg)==-1 && permits==1 && sdkCalls==calls); // Oversize doesn't leak a slot.
    msg.len=5;
    assert(send(&msg)==0 && permits==0);
    puts(TEST_SERVER ? "NUS server contention tests passed" : "NUS client contention/lifetime tests passed");
}
