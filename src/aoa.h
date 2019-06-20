#ifndef AOA_H
#define AOA_H

#include <inttypes.h>
#include <stdbool.h>
#include <libusb-1.0/libusb.h>

enum lookup_type {
    // devices supporting adb
    LOOKUP_BY_ADB_INTERFACE,
    // devices having the provided serial
    LOOKUP_BY_SERIAL,
    // devices having the provided vid:pid
    LOOKUP_BY_VID_PID,
};

struct lookup {
    enum lookup_type type;
    union {
        const char *serial;
        struct {
            uint16_t vid;
            uint16_t pid;
        };
    };
};

struct usb_device {
    uint16_t vid;
    uint16_t pid;
    char *serial;
    libusb_device *device;
};

bool
aoa_init(void);

void
aoa_exit(void);

ssize_t
aoa_find_devices(const struct lookup *lookup,
                 struct usb_device *devices, size_t len);

bool
aoa_forward_audio(const struct usb_device *device);

void
aoa_destroy_device(struct usb_device *device);

// there is no function to disable forwarding, because it just does not work
// you need to unplug the device

#endif
