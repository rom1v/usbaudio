#define _GNU_SOURCE // for strdup()
#include "aoa.h"

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"

// <https://source.android.com/devices/accessories/aoa2>
#define AOA_GET_PROTOCOL     51
#define AOA_START_ACCESSORY  53
#define AOA_SET_AUDIO_MODE   58

#define AUDIO_MODE_NO_AUDIO               0
#define AUDIO_MODE_S16LSB_STEREO_44100HZ  1

#define DEFAULT_TIMEOUT 1000

typedef struct control_params {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    unsigned char *data;
    uint16_t length;
    unsigned int timeout;
} control_params;

static void
log_libusb_error(enum libusb_error errcode) {
    LOGE("%s", libusb_strerror(errcode));
}

static bool
control_transfer(libusb_device_handle *handle, control_params *params) {
    int r = libusb_control_transfer(handle,
                                    params->request_type,
                                    params->request,
                                    params->value,
                                    params->index,
                                    params->data,
                                    params->length,
                                    params->timeout);
    if (r < 0) {
        log_libusb_error(r);
        return false;
    }
    return true;
}

static bool
get_serial(libusb_device *device, struct libusb_device_descriptor *desc,
           char *data, int length) {
    libusb_device_handle *handle;
    int r = libusb_open(device, &handle);
    if (r) {
        LOGD("USB: cannot open device %04x:%04x (%s)",
             desc->idVendor, desc->idProduct, libusb_strerror(r));
        return false;
    }

    if (!desc->iSerialNumber) {
        LOGD("USB: device %04x:%04x has no serial number available",
             desc->idVendor, desc->idProduct);
        libusb_close(handle);
        return false;
    }

    r = libusb_get_string_descriptor_ascii(handle, desc->iSerialNumber,
                                           (unsigned char *) data, length);
    if (r <= 0) {
        LOGD("USB: cannot read serial of device %04x:%04x (%s)",
             desc->idVendor, desc->idProduct, libusb_strerror(r));
        libusb_close(handle);
        return false;
    }

    data[length - 1] = '\0'; // just in case

    libusb_close(handle);
    return true;
}

static bool
has_adb(libusb_device *device, struct libusb_device_descriptor *desc) {
#define ADB_CLASS 0xff
#define ADB_SUBCLASS 0x42
#define ADB_PROTOCOL 0x1
    for (unsigned i = 0; i < desc->bNumConfigurations; ++i) {
        struct libusb_config_descriptor *config;
        int r = libusb_get_config_descriptor(device, i, &config);
        if (r) {
            LOGE("Could not retrieve config descriptors");
            continue;
        }

        for (unsigned j = 0; j < config->bNumInterfaces; ++j) {
            const struct libusb_interface *intf = &config->interface[j];
            for (int k = 0; k < intf->num_altsetting; ++k) {
                const struct libusb_interface_descriptor *d =
                    &intf->altsetting[k];
                if (d->bInterfaceClass == ADB_CLASS &&
                        d->bInterfaceSubClass == ADB_SUBCLASS &&
                        d->bInterfaceProtocol == ADB_PROTOCOL) {
                    // we found it!
                    libusb_free_config_descriptor(config);
                    return true;
                }
            }
        }

        libusb_free_config_descriptor(config);
    }

    return false;
}

ssize_t
aoa_find_devices(const struct lookup *lookup,
                 struct usb_device *devices, size_t len) {
    size_t nr = 0; // number of devices found

    libusb_device **list;
    ssize_t cnt = libusb_get_device_list(NULL, &list);
    if (cnt < 0) {
        log_libusb_error(cnt);
        return -1;
    }

    for (ssize_t i = 0; i < cnt && nr < len; ++i) {
        libusb_device *device = list[i];

        struct usb_device *usb_device = &devices[nr];

        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(device, &desc);

        char serial[128];
        bool match = false;
        switch (lookup->type) {
            case LOOKUP_BY_ADB_INTERFACE:
                match = has_adb(device, &desc);
                break;
            case LOOKUP_BY_SERIAL: {
                bool ok = get_serial(device, &desc, serial, sizeof(serial));
                if (ok) {
                    match = !strcmp(lookup->serial, serial);
                }
                break;
            }
            case LOOKUP_BY_VID_PID:
                match = lookup->vid == desc.idVendor &&
                        lookup->pid == desc.idProduct;
                break;
        }

        if (match) {
            // add the device to the result list
            if (lookup->type != LOOKUP_BY_SERIAL) {
                bool ok = get_serial(device, &desc, serial, sizeof(serial));
                if (!ok) {
                    LOGE("Could not read device serial");
                    continue;
                }
            }

            usb_device->serial = strdup(serial);
            usb_device->vid = desc.idVendor;
            usb_device->pid = desc.idProduct;
            usb_device->device = device;
            libusb_ref_device(usb_device->device);
            nr++;
        }
    }

    libusb_free_device_list(list, 1);

    return nr;
}

void aoa_destroy_device(struct usb_device *usb_device) {
    free(usb_device->serial);
    libusb_unref_device(usb_device->device);
}

static bool
aoa_get_protocol(libusb_device_handle *handle, uint16_t *version) {
    unsigned char data[2];
    control_params params = {
        .request_type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR,
        .request = AOA_GET_PROTOCOL,
        .value = 0,
        .index = 0,
        .data = data,
        .length = sizeof(data),
        .timeout = DEFAULT_TIMEOUT
    };
    if (control_transfer(handle, &params)) {
        // little endian
        *version = (data[1] << 8) | data[0];
        return true;
    }
    return false;
}

static bool
set_audio_mode(libusb_device_handle *handle, uint16_t mode) {
    control_params params = {
        .request_type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
        .request = AOA_SET_AUDIO_MODE,
        // <https://source.android.com/devices/accessories/aoa2.html#audio-support>
        .value = mode,
        .index = 0, // unused
        .data = NULL,
        .length = 0,
        .timeout = DEFAULT_TIMEOUT
    };
    return control_transfer(handle, &params);
}

static bool
start_accessory(libusb_device_handle *handle) {
    control_params params = {
        .request_type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
        .request = AOA_START_ACCESSORY,
        .value = 0, // unused
        .index = 0, // unused
        .data = NULL,
        .length = 0,
        .timeout = DEFAULT_TIMEOUT
    };
    return control_transfer(handle, &params);
}

bool
aoa_init(void) {
    return !libusb_init(NULL);
}

void
aoa_exit(void) {
    libusb_exit(NULL);
}

bool
aoa_forward_audio(const struct usb_device *usb_device) {
    libusb_device_handle *handle;
    int r = libusb_open(usb_device->device, &handle);
    if (r) {
        log_libusb_error(r);
        return false;
    }

    uint16_t version;
    if (!aoa_get_protocol(handle, &version)) {
        LOGE("Could not get AOA protocol version");
        libusb_close(handle);
        return false;
    }

    LOGD("Device AOA version: %" PRIu16, version);
    if (version < 2) {
        LOGE("Device does not support AOA 2: %" PRIu16, version);
        libusb_close(handle);
        return false;
    }

    if (!set_audio_mode(handle, AUDIO_MODE_S16LSB_STEREO_44100HZ)) {
        LOGE("Could not set audio mode");
        libusb_close(handle);
        return false;
    }

    if (!start_accessory(handle)) {
        LOGE("Could not start accessory");
        libusb_close(handle);
        return false;
    }

    libusb_close(handle);
    return true;
}
