/* tusb_config.h -- TinyUSB device config: composite CDC-ACM + MSC. */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE
#define CFG_TUD_ENABLED         1

#define CFG_TUD_CDC             1
#define CFG_TUD_MSC             1
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

#define CFG_TUD_CDC_RX_BUFSIZE  512
#define CFG_TUD_CDC_TX_BUFSIZE  512
#define CFG_TUD_MSC_EP_BUFSIZE  512

#endif /* TUSB_CONFIG_H */
