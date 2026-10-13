#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define DEVICE_IS_UHK80_RIGHT 1
#define CONFIG_UHK_USB_LEFT_RELAY 1
enum { LatencyMode_NUS, LatencyMode_BleHid, LatencyMode_Idle, LatencyMode_UsbLeftRelay };
typedef int latency_mode_t;
typedef int connection_id_t;
typedef int connection_type_t;
enum { ConnectionType_UsbHidRight=1, ConnectionType_UsbHidLeft=2, ConnectionType_BtHid=3,
       ConnectionType_NusDongle=4, ConnectionType_NusLeft=5 };
enum { ConnectionId_NusServerLeft=29, PeerIdUnknown=0, PeerIdFirst=1, PeerCount=3 };
enum { PowerMode_LastAwake=2 };
static int CurrentPowerMode, CurrentHostConnectionId=1, link=29;
static bool awake=true;
static int Connections_Type(int id) { return id==29 ? 5 : id; }
static int UsbLeft_LinkConnection(void) { return link; }
static bool UsbLeft_Awake(void) { return awake; }
struct bt_conn { int id; };
static struct bt_conn conns[2]={{29},{3}};
static struct { struct bt_conn *conn; int connectionId; } Peers[3];
static int8_t appliedLatencyMode[3];
struct bt_le_conn_param { int min,max,latency,timeout; };
#define BT_LE_CONN_PARAM_INIT(a,b,c,d) {a,b,c,d}
static unsigned updates;
static struct bt_le_conn_param last;
static void configure(bool selectedLeft,bool wireless,bool hostAwake) {
    CurrentHostConnectionId=selectedLeft ? 2 : 1;
    link=wireless ? 29 : 27; awake=hostAwake;
}
static void setLatency(struct bt_conn *conn,const struct bt_le_conn_param *params) {
    assert(conn==&conns[0]); ++updates; last=*params;
}
#include "relay_latency_under_test.inc"
int main(void) {
    memset(appliedLatencyMode,-1,sizeof(appliedLatencyMode));
    Peers[1].conn=&conns[0]; Peers[1].connectionId=29;
    configure(false,true,true); BtConn_UpdateConnectionLatencies();
    assert(last.latency==4);
    configure(true,true,true); BtConn_UpdateConnectionLatencies();
    assert(last.min==6 && last.max==6 && last.latency==0 && last.timeout==100);
    unsigned before=updates;
    for(unsigned i=0;i<100;++i) BtConn_UpdateConnectionLatencies();
    assert(updates==before); // Main-loop refresh cannot repeatedly renegotiate.
    configure(true,false,true); BtConn_UpdateConnectionLatencies(); assert(last.latency==4);
    configure(true,true,false); BtConn_UpdateConnectionLatencies(); assert(last.latency==4);
    configure(true,true,true); BtConn_UpdateConnectionLatencies(); assert(last.latency==0);
    // Unrelated HID/dongle/half profiles retain their existing selection rules.
    CurrentHostConnectionId=3; assert(desiredLatencyMode(3)==LatencyMode_BleHid);
    assert(desiredLatencyMode(4)==LatencyMode_Idle);
    CurrentPowerMode=3; assert(desiredLatencyMode(3)==LatencyMode_Idle);
    puts("wireless left-host latency profile tests passed");
}
