#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
// Compile the actual driver power-handler, enable and start methods; only
// clock/controller primitives are mocked. Register writes remain visible here.
enum { NRFX_POWER_USB_STATE_DISCONNECTED,NRFX_POWER_USB_STATE_CONNECTED,NRFX_POWER_USB_STATE_READY };
enum { NRFX_POWER_USB_EVT_DETECTED,NRFX_POWER_USB_EVT_READY,NRFX_POWER_USB_EVT_REMOVED };
typedef int nrfx_power_usb_evt_t;
enum { UDC_EVT_VBUS_READY,UDC_EVT_VBUS_REMOVED,USB_CONTROL_EP_OUT=0,USB_CONTROL_EP_IN=0x80,
       USB_EP_TYPE_CONTROL=0,UDC_NRF_EP0_SIZE=64,USBD_IRQn=3 };
struct device { int unused; };
static const struct device dev;
static const struct device *udc_nrf_dev=&dev;
static struct { uint32_t ENABLE,INTEN,USBPULLUP,LOWPOWER,EVENTCAUSE; } regs;
#define USBD_LOWPOWER_LOWPOWER_ForceNormal 0
#define USBD_LOWPOWER_LOWPOWER_Pos 0
#define USBD_EVENTCAUSE_READY_Msk 1
#define NRF_USBD (&regs)
#define CONFIG_UDC_ENABLE_SOF 0
#define IS_ENABLED(x) (x)
#define USBD_INTEN_USBRESET_Msk 1
#define USBD_INTEN_ENDEPIN0_Msk 1
#define USBD_INTEN_ENDEPIN1_Msk 1
#define USBD_INTEN_ENDEPIN2_Msk 1
#define USBD_INTEN_ENDEPIN3_Msk 1
#define USBD_INTEN_ENDEPIN4_Msk 1
#define USBD_INTEN_ENDEPIN5_Msk 1
#define USBD_INTEN_ENDEPIN6_Msk 1
#define USBD_INTEN_ENDEPIN7_Msk 1
#define USBD_INTEN_EP0DATADONE_Msk 1
#define USBD_INTEN_ENDISOIN_Msk 1
#define USBD_INTEN_ENDEPOUT0_Msk 1
#define USBD_INTEN_ENDEPOUT1_Msk 1
#define USBD_INTEN_ENDEPOUT2_Msk 1
#define USBD_INTEN_ENDEPOUT3_Msk 1
#define USBD_INTEN_ENDEPOUT4_Msk 1
#define USBD_INTEN_ENDEPOUT5_Msk 1
#define USBD_INTEN_ENDEPOUT6_Msk 1
#define USBD_INTEN_ENDEPOUT7_Msk 1
#define USBD_INTEN_ENDISOOUT_Msk 1
#define USBD_INTEN_USBEVENT_Msk 1
#define USBD_INTEN_EP0SETUP_Msk 1
#define USBD_INTEN_EPDATA_Msk 1
#define USBD_INTEN_SOF_Msk 1
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
static bool m_bus_suspend,irqLocked;
static unsigned regulator,starts,earlyStarts,readyPosts,removedPosts,enableCalls;
static struct { int notify; } hfxo_cli;
static void *hfxo_mgr;
static void sys_notify_init_spinwait(int *n) { *n=0; }
static int onoff_request(void *mgr,void *client) { (void)mgr; (void)client; return 0; }
static unsigned irq_lock(void) { irqLocked=true; return 1; }
static void irq_unlock(unsigned key) { assert(key==1 && irqLocked); irqLocked=false; }
static void irq_enable(unsigned irq) {
    assert(irq==USBD_IRQn); ++starts; if(!regs.ENABLE) ++earlyStarts;
}
static void nrf_usbd_peripheral_enable(void);
static void nrf_usbd_legacy_enable(void) {
    assert(irqLocked);
    // Model the READY raised by peripheral enable. EVENTCAUSE writes below
    // record W1C intent; this fixture does not emulate the full register bus.
    regs.EVENTCAUSE|=USBD_EVENTCAUSE_READY_Msk;
    nrf_usbd_peripheral_enable(); ++enableCalls;
    assert(!regs.LOWPOWER); // Must leave low power before waiting for regulator READY/start.
}
static bool nrf_usbd_common_errata_187(void) { return false; }
static bool nrf_usbd_common_errata_171(void) { return false; }
static void usbd_errata_187_211_begin(void) {}
static void usbd_errata_187_211_end(void) {}
static void usbd_errata_171_begin(void) {}
static void usbd_errata_171_end(void) {}
static unsigned __attribute__((unused)) nrfx_power_usbstatus_get(void) { return regulator; }
static int udc_ep_enable_internal(const struct device *d,int ep,int type,int size,int interval) {
    assert(d==&dev && (ep==0 || ep==0x80) && !type && size==64 && !interval); return 0;
}
static void udc_submit_event(const struct device *d,int event,int value) {
    assert(d==&dev && !value);
    if(event==UDC_EVT_VBUS_READY) ++readyPosts; else if(event==UDC_EVT_VBUS_REMOVED) ++removedPosts;
    else assert(false);
}
#include "nrf_usb_ready_under_test.inc"
static void reset(void) {
    regs.ENABLE=regs.INTEN=regs.USBPULLUP=regs.LOWPOWER=0;
    regs.EVENTCAUSE=USBD_EVENTCAUSE_READY_Msk;
    m_bus_suspend=false;
    starts=earlyStarts=readyPosts=removedPosts=enableCalls=0; irqLocked=false;
}
int main(void) {
    // Physical READY precedes the deferred stack attach. No second edge arrives.
    reset(); regulator=NRFX_POWER_USB_STATE_READY;
    regs.LOWPOWER=1; // Retained register state after the previous bus suspend.
    m_bus_suspend=true;
    udc_nrf_power_handler(NRFX_POWER_USB_EVT_DETECTED);
    udc_nrf_power_handler(NRFX_POWER_USB_EVT_READY);
    assert(readyPosts==1 && earlyStarts==0 && starts==0 && !regs.USBPULLUP);
    assert(udc_nrf_enable(&dev)==0);
    assert(enableCalls==1 && starts==1 && regs.USBPULLUP==1 && regs.INTEN && !regs.LOWPOWER);
    // Ordinary enable-before-ready still waits for actual regulator readiness.
    reset(); regulator=NRFX_POWER_USB_STATE_CONNECTED;
    assert(udc_nrf_enable(&dev)==0 && starts==0 && !regs.USBPULLUP);
    regulator=NRFX_POWER_USB_STATE_READY;
    udc_nrf_power_handler(NRFX_POWER_USB_EVT_READY);
    assert(starts==1 && !earlyStarts && regs.USBPULLUP==1);
    udc_nrf_power_handler(NRFX_POWER_USB_EVT_READY); // Duplicate READY is harmless.
    assert(enableCalls==1 && regs.USBPULLUP==1);

    udc_nrf_power_handler(NRFX_POWER_USB_EVT_REMOVED); assert(removedPosts==1);
    regs.ENABLE=regs.USBPULLUP=0; starts=0;
    udc_nrf_power_handler(NRFX_POWER_USB_EVT_READY); assert(starts==0 && !regs.USBPULLUP);
    // Disconnect/re-enable after suspend retains LOWPOWER unless start clears it.
    regulator=NRFX_POWER_USB_STATE_READY; regs.LOWPOWER=1;
    assert(udc_nrf_enable(&dev)==0);
    assert(regs.USBPULLUP==1 && !regs.LOWPOWER && !m_bus_suspend);
    puts("nRF USB ready/enable ordering tests passed");
}
