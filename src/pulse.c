#include "pulse.h"

#include <assert.h>
#include <pulse/pulseaudio.h>
#include <stdbool.h>
#include <string.h>

#include "log.h"

#define DEVICE_NOT_FOUND_YET -1
#define DEVICE_NOT_FOUND -2

struct pulse_device_data {
    const char *req_serial;
    size_t req_serial_len;
    int index;
};

static void
pulse_state_cb(pa_context *ctx, void *userdata) {
    pa_context_state_t *state = userdata;
    *state = pa_context_get_state(ctx);
}

static void
pulse_sourcelist_cb(pa_context *ctx, const pa_source_info *info, int eol,
                    void *userdata) {
    struct pulse_device_data *device = userdata;
    if (eol) {
        if (device->index == DEVICE_NOT_FOUND_YET) {
            device->index = DEVICE_NOT_FOUND;
        }
        return;
    }

    // The PulseAudio serial is not exactly the same as the USB serial,
    // it follows the pattern: "manufacturer_model_serial".
    // To find a matching device, we check it ends with "_serial".
    const char *serial =
        pa_proplist_gets(info->proplist, PA_PROP_DEVICE_SERIAL);
    if (!serial) {
        return;
    }
    LOGD("%s ? %s", device->req_serial, serial);
    size_t len = strlen(serial);
    if (len < device->req_serial_len + 1) { // +1 for '_'
        return;
    }
    if (serial[len - device->req_serial_len - 1] != '_') {
        // it may not match "_serial" if there is no '_'
        return;
    }
    if (!memcmp(device->req_serial,
                &serial[len - device->req_serial_len],
                device->req_serial_len)) {
        // the serial matches
        device->index = (int) info->index;
        LOGI("Matching PulseAudio input source found: %d (%s:%s) %s",
             device->index,
             pa_proplist_gets(info->proplist, PA_PROP_DEVICE_VENDOR_ID),
             pa_proplist_gets(info->proplist, PA_PROP_DEVICE_PRODUCT_ID),
             serial);
    }
}

static bool
pulse_wait_ready(pa_context *ctx, pa_mainloop *ml) {
    pa_context_state_t state = PA_CONTEXT_UNCONNECTED;
    pa_context_set_state_callback(ctx, pulse_state_cb, &state);

    // state will be set during pa_mainloop_iterate()
    bool ready = false;
    for (;;) {
        int r = pa_mainloop_iterate(ml, 1, NULL);
        if (r <= 0) {
            LOGE("Could not iterate on main loop");
            break;
        }

        if (state == PA_CONTEXT_READY) {
            ready = true;
            break;
        }

        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
            LOGE("Connection to PulseAudio server terminated");
            break;
        }
    }

    pa_context_set_state_callback(ctx, NULL, NULL);
    return ready;
}

int
pulse_get_device_number(const char *serial) {
    pa_mainloop *ml = pa_mainloop_new();
    if (!ml) {
        LOGE("Could not create PulseAudio main loop");
        return -1;
    }

    int ret = -1;

    pa_mainloop_api *mlapi = pa_mainloop_get_api(ml);
    assert(mlapi);

    pa_context *ctx = pa_context_new(mlapi, "usbaudio");
    if (!ctx) {
        LOGE("Could not create PulseAudio context");
        goto finally_ml_free;
    }

    int r = pa_context_connect(ctx, NULL, 0, NULL);
    if (r < 0) {
        LOGE("Could not connect to PulseAudio server");
        goto finally_ctx_unref;
    }

    bool ready = pulse_wait_ready(ctx, ml);
    if (!ready) {
        goto finally_ctx_disconnect;
    }

    struct pulse_device_data device = {
        .req_serial = serial,
        .req_serial_len = strlen(serial),
        .index = DEVICE_NOT_FOUND_YET,
    };
    pa_operation *op =
        pa_context_get_source_info_list(ctx, pulse_sourcelist_cb, &device);
    do {
        int r = pa_mainloop_iterate(ml, 1, NULL);
        if (r <= 0) {
            LOGE("Could not iterate on main loop");
            goto finally_ctx_disconnect;
        }
    } while (device.index == DEVICE_NOT_FOUND_YET);
    if (device.index >= 0) {
        // we don't need to receive further callbacks
        pa_operation_cancel(op);
    }
    pa_operation_unref(op);

    if (device.index >= 0) {
        ret = device.index;
    }

finally_ctx_disconnect:
    pa_context_disconnect(ctx);
finally_ctx_unref:
    pa_context_unref(ctx);
finally_ml_free:
    pa_mainloop_free(ml);

    return ret;
}
