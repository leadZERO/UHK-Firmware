#include "usb_left_relay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define DECLARE(role) \
void role##_Reset(bool bridge, bool bluetooth); \
void role##_Tick(uint32_t now); \
void role##_Worker(void); \
void role##_CompleteUsb(void); \
void role##_Ready(bool bridge, bool bluetooth); \
void role##_BleState(unsigned mtu, unsigned security); \
void role##_BlockDrain(bool block); \
void role##_Select(bool selected); \
bool role##_Send(const uint8_t *report); \
const relay_t *role##_State(void); \
const uint8_t *role##_Delivered(void); \
unsigned role##_Deliveries(void); \
void role##_Busy(unsigned attempts,bool disconnect); \
unsigned role##_Attempts(void); \
uint8_t role##_PeerSource(void); \
uint8_t role##_Uart(void); \
uint8_t role##_Ble(void); \
uint8_t role##_LinkConnection(void); \
uint32_t role##_IngressGeneration(uint8_t connection); \
void role##_Receive(uint8_t source,uint8_t connection,uint32_t generation,const uint8_t *packet,size_t size)
DECLARE(Right);
DECLARE(Left);

static struct packet {
    bool toRight;
    uint8_t source, connection, data[USB_LEFT_RELAY_MAX_PACKET];
    uint32_t generation;
    uint32_t readyAt;
    size_t size;
} packets[128];
static unsigned head, tail, bridgeSends, bleSends;
static uint32_t now;
void TestWireSend(bool right, uint8_t connection, const uint8_t *data, size_t len) {
    assert(len<=USB_LEFT_RELAY_MAX_PACKET && tail-head<128);
    bool bridge = connection==(right ? Right_Uart() : Left_Uart());
    assert(bridge || connection==(right ? Right_Ble() : Left_Ble()));
    if (bridge) ++bridgeSends; else ++bleSends;
    uint8_t receiver = bridge ? (right ? Left_Uart() : Right_Uart())
                              : (right ? Left_Ble() : Right_Ble());
    struct packet *p=&packets[tail++ % 128];
    p->toRight=!right; p->size=len; p->connection=receiver;
    p->source=right ? Left_PeerSource() : Right_PeerSource();
    p->generation=right ? Left_IngressGeneration(receiver) : Right_IngressGeneration(receiver);
    p->readyAt=now+(bridge ? 0 : 20); // Radio delay in each direction, including ACKs.
    memcpy(p->data,data,len);
}
static void receive(struct packet *p) {
    if(p->toRight) Right_Receive(p->source,p->connection,p->generation,p->data,p->size);
    else Left_Receive(p->source,p->connection,p->generation,p->data,p->size);
}
static void pump(void) {
    while(head!=tail && packets[head % 128].readyAt<=now) receive(&packets[head++ % 128]);
}
static void step(unsigned ms) {
    for(unsigned i=0;i<ms;++i) {
        ++now; Right_Tick(now); Left_Tick(now);
        Right_Worker(); Left_Worker(); pump();
        Left_CompleteUsb(); Right_Worker(); Left_Worker(); pump();
    }
}
static void reset(bool bridge, bool bluetooth) {
    head=tail=bridgeSends=bleSends=now=0;
    Right_Reset(bridge,bluetooth); Left_Reset(bridge,bluetooth);
    Right_Select(true); step(180);
}
static void active(void) { assert(Right_State()->active && Left_State()->active); }
static void reportAndRelease(void) {
    uint8_t report[29]={0}; report[5]=0x40; // Backspace
    assert(Right_Send(report)); step(100);
    assert(!memcmp(Left_Delivered(),report,29));
    memset(report,0,sizeof(report)); assert(Right_Send(report)); step(100);
    assert(!memcmp(Left_Delivered(),report,29));
}
int main(void) {
    // Fresh wireless-only boot negotiates and delivers key-down and key-up.
    reset(false,true); active();
    assert(Right_LinkConnection()==Right_Ble() && Left_LinkConnection()==Left_Ble());
    assert(bleSends && !bridgeSends); reportAndRelease(); step(3*RELAY_LEASE_MS); active();
    Right_Busy(5,false); Left_Busy(5,false); reportAndRelease(); active();
    assert(Right_Attempts()>5 && Left_Attempts()>5);
    // A perpetually busy slot yields a bounded worker, then the protocol retries.
    Right_Busy(100,false); assert(Right_Send((uint8_t[29]){0})); Right_Worker();
    assert(Right_Attempts()<=17); Right_Busy(0,false); step(100); active();

    // A wired route wins even while the paired wireless route is ready.
    reset(true,true); active(); assert(bridgeSends && !bleSends);
    uint64_t token=Right_State()->token;
    Right_Ready(true,false); Left_Ready(true,false); step(20); active();
    assert(Right_State()->token==token); // Inactive BLE changes do not restart UART.
    Right_Ready(true,true); Left_Ready(true,true); step(20); active();
    assert(Right_State()->token==token && !bleSends);

    // A queued old-route report must never be retargeted to the new BLE route.
    uint8_t report[29]={0}; report[5]=0x40;
    assert(Right_Send(report)); // Leave worker queue undrained.
    unsigned oldBridgeSends=bridgeSends;
    Right_Ready(false,true); Left_Ready(false,true);
    Right_Worker(); assert(bridgeSends==oldBridgeSends && !bleSends);
    Left_BlockDrain(true); step(300);
    assert(!Right_State()->active && !Left_State()->active);
    Left_BlockDrain(false); step(1200); active();
    assert(Right_State()->token!=token && bleSends);
    uint8_t zero[29]={0}; assert(!memcmp(Left_Delivered(),zero,29));
    reportAndRelease();

    // A previously-enqueued BLE frame cannot survive a reconnect generation.
    assert(Right_Send(report)); Right_Worker();
    assert(head!=tail); struct packet stale=packets[head];
    unsigned received=Left_State()->received;
    Right_Ready(false,false); Left_Ready(false,false);
    Right_Ready(false,true); Left_Ready(false,true);
    receive(&stale); assert(Left_State()->received==received);
    step(1200); active(); reportAndRelease();

    // Wrong source and inactive route are rejected even with valid bytes.
    received=Left_State()->received;
    stale.source=0; stale.generation=Left_IngressGeneration(Left_Ble());
    receive(&stale); assert(Left_State()->received==received);

    // Restoring the bridge replaces the wireless session and neutralizes USB.
    token=Right_State()->token;
    Right_Ready(true,true); Left_Ready(true,true); step(1200); active();
    assert(Right_LinkConnection()==Right_Uart() && Right_State()->token!=token);
    received=Left_State()->received;
    stale.source=Left_PeerSource(); stale.generation=Left_IngressGeneration(Left_Ble());
    receive(&stale); assert(Left_State()->received==received);
    reportAndRelease();

    // Total link loss closes the left gate and releases the host's held keys.
    assert(Right_Send(report)); step(100);
    Right_Ready(false,false); Left_Ready(false,false); step(30);
    assert(!Right_State()->active && !Left_State()->active);
    assert(!memcmp(Left_Delivered(),zero,29));
    Right_Ready(false,true); Left_Ready(false,true); step(1200); active(); reportAndRelease();

    // An unencrypted or undersized ATT channel cannot negotiate a relay.
    reset(false,false); Right_BleState(23,2); Left_BleState(23,2);
    Right_Ready(false,true); Left_Ready(false,true); step(200);
    assert(!Right_State()->link && !Left_State()->link);
    Right_BleState(131,1); Left_BleState(131,1); step(200);
    assert(!Right_State()->link && !Left_State()->link);
    Right_BleState(131,2); Left_BleState(131,2); step(1200); active(); reportAndRelease();
    assert(Right_Deliveries());
    // Route loss during a busy retry must stop that same immutable packet.
    Right_Busy(5,true); assert(Right_Send(report)); Right_Worker();
    assert(Right_Attempts()==1);
    puts("wireless relay transport tests passed (both halves)");
}
