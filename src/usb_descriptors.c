/*
 * usb_descriptors.c -- composite CDC-ACM (console) + MSC (deck drive).
 * Shape follows tinyusb examples/dev/cdc_msc.
 */
#include "tusb.h"
#include "pico/unique_id.h"

#define USB_VID 0x2E8A          /* Raspberry Pi */
#define USB_PID 0x0120          /* project-local: GE-120 reader sim */

enum { ITF_NUM_CDC = 0, ITF_NUM_CDC_DATA, ITF_NUM_MSC, ITF_NUM_TOTAL };

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82
#define EPNUM_MSC_OUT   0x03
#define EPNUM_MSC_IN    0x83

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 5, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

static const char *string_desc[] = {
    NULL,                               /* 0: langid, handled below */
    "MusIF-MIAI",                       /* 1 */
    "GE-120 Card Reader Simulator",     /* 2 */
    NULL,                               /* 3: serial from unique id  */
    "GE-120 console",                   /* 4 */
    "GE-120 decks",                     /* 5 */
};

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    static uint16_t desc[32];
    (void)langid;
    uint8_t len;

    if (index == 0) {
        desc[1] = 0x0409;
        len = 1;
    } else if (index == 3) {
        char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
        pico_get_unique_board_id_string(serial, sizeof(serial));
        for (len = 0; serial[len] && len < 31; len++)
            desc[1 + len] = (uint16_t)serial[len];
    } else if (index < sizeof(string_desc) / sizeof(string_desc[0])) {
        const char *s = string_desc[index];
        for (len = 0; s[len] && len < 31; len++)
            desc[1 + len] = (uint16_t)s[len];
    } else {
        return NULL;
    }
    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc;
}
