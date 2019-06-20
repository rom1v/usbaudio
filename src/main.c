#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aoa.h"
#include "log.h"
#include "pulse.h"

#define DEFAULT_VLC_LIVE_CACHING 50

struct args {
    bool help;
    bool play;
    const char *serial;
    uint16_t vid;
    uint16_t pid;
    uint32_t live_caching;
};

static bool
parse_usb_id(const char *s, uint16_t *result) {
    char *endptr;
    if (*s == '\0') {
        return false;
    }
    long value = strtol(s, &endptr, 16);
    if (*endptr != '\0') {
        return false;
    }
    if (value & ~0xffff) {
        LOGE("Id out of range: %lx", value);
        return false;
    }

    *result = (uint16_t) value;
    return true;
}

static bool
parse_device(const char *s, uint16_t *vid, uint16_t *pid) {
    const char *ptr = strchr(s, ':');
    if (!ptr) {
        goto error;
    }
    size_t index = ptr - s;
    size_t len = strlen(s);
    if (index == 0 || index > 4 || index + 1 > len - 1 || index + 5 < len) {
        // pid and vid must have between 1 and 4 chars
        goto error;
    }

    char id[5];

    memcpy(id, s, index);
    id[index] = '\0';
    if (!parse_usb_id(id, vid)) {
        LOGE("Could not parse vid: %s", id);
        goto error;
    }

    memcpy(id, &s[index + 1], len - index - 1);
    id[index] = '\0';
    if (!parse_usb_id(id, pid)) {
        LOGE("Could not parse pid: %s", id);
        goto error;
    }

    return true;

error:
    LOGE("Invalid device format (expected vid:pid): %s", s);
    return false;
}

static bool
parse_live_caching(const char *s, uint32_t *result) {
    char *endptr;
    if (*s == '\0') {
        return false;
    }
    long value = strtol(s, &endptr, 10);
    if (*endptr != '\0') {
        return false;
    }
    if (value & ~0xfffffffff) {
        LOGE("Id out of range: %lx", value);
        return false;
    }

    *result = (uint32_t) value;
    return true;
}

static bool
parse_args(struct args *args, int argc, char *argv[]) {
#define OPT_LIVE_CACHING 1000
    static const struct option long_opts[] = {
        {"device",       required_argument, NULL, 'd'},
        {"help",         no_argument,       NULL, 'h'},
        {"live-caching", required_argument, NULL, OPT_LIVE_CACHING},
        {"no-play",      no_argument,       NULL, 'n'},
        {"serial",       required_argument, NULL, 's'},
    };
    int c;
    while ((c = getopt_long(argc, argv, "d:hns:", long_opts, NULL)) != -1) {
        switch (c) {
            case 'd':
                if (!parse_device(optarg, &args->vid, &args->pid)) {
                    return false;
                }
                break;
            case 'h':
                args->help = true;
                break;
            case 'n':
                args->play = false;
                break;
            case 's':
                args->serial = optarg;
                break;
            case OPT_LIVE_CACHING:
                if (!parse_live_caching(optarg, &args->live_caching)) {
                    return false;
                }
                break;
            default:
                // getopt prints the error message on stderr
                return false;
        }
    }

    if (optind < argc) {
        LOGE("Unexpected additional argument: %s", argv[optind]);
        return false;
    }

    return true;
}

static void usage(const char *arg0) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "\n"
        "    -d, --device pid:vid\n"
        "        Lookup the USB device by pid:vid.\n"
        "\n"
        "    -h, --help\n"
        "        Print this help.\n"
        "\n"
        "    --live-caching ms\n"
        "        Forward the option to VLC. Default is %dms.\n"
        "\n"
        "    -n, --no-play\n"
        "        Do not play the input source matching the device.\n"
        "\n"
        "    -s, --serial serial\n"
        "        Lookup the USB device by serial.\n"
        "\n", arg0, DEFAULT_VLC_LIVE_CACHING);
}

static inline const char *
get_vlc_command(void) {
    const char *vlc = getenv("VLC");
    if (!vlc) {
        vlc = "vlc";
    }
    return vlc;
}

int main(int argc, char *argv[]) {
    struct args args = {
        .help = false,
        .play = true,
        .serial = NULL,
        .vid = 0,
        .pid = 0,
        .live_caching = DEFAULT_VLC_LIVE_CACHING,
    };

    if (!parse_args(&args, argc, argv)) {
        return 1;
    }

    if (args.help) {
        usage(argv[0]);
        return 0;
    }

    if (args.serial && (args.vid || args.pid)) {
        LOGE("Could not provide device and serial simultaneously");
        return 1;
    }

    if (!aoa_init()) {
        LOGE("Could not initialize AOA");
        return 1;
    }

    struct lookup lookup;
    if (args.serial) {
        lookup.type = LOOKUP_BY_SERIAL;
        lookup.serial = args.serial;
    } else if (args.vid || args.pid) {
        lookup.type = LOOKUP_BY_VID_PID;
        lookup.vid = args.vid;
        lookup.pid = args.pid;
    } else {
        lookup.type = LOOKUP_BY_ADB_INTERFACE;
    }

    struct usb_device devices[32];
    ssize_t ndevices = aoa_find_devices(&lookup, devices, 32);
    if (ndevices < 0) {
        LOGE("Could not get USB devices");
        return 1;
    }

    if (ndevices == 0) {
        LOGE("Could not find device");
        return 1;
    }

    if (ndevices > 1) {
        LOGE("Several devices found:");
        for (size_t i = 0; i < ndevices; ++i) {
            struct usb_device *d = &devices[i];
            LOGE("   [%04x:%04x] %s", d->vid, d->pid, d->serial);
            aoa_destroy_device(&devices[i]);
        }
        return 1;
    }

    struct usb_device *device = &devices[0];

    LOGI("Device: [%04x:%04x] %s", device->vid, device->pid, device->serial);

    if (!aoa_forward_audio(device)) {
        LOGE("Could not forward audio");
        aoa_destroy_device(device);
        return 1;
    }

    LOGI("Audio forwarding enabled");

    aoa_exit();

    if (!args.play) {
        // nothing more to do
        aoa_destroy_device(device);
        return 0;
    }

    if (device->pid < 0x2D02 || device->pid > 0x2D05) {
        // the AOA audio was already enabled, no need to wait
        // <https://source.android.com/devices/accessories/aoa2>
        LOGI("Waiting for input source...");
        sleep(2);
    }

    int nr = pulse_get_device_number(device->serial);
    aoa_destroy_device(device);
    if (nr < 0) {
        LOGE("Could not find matching PulseAudio input source");
        return 1;
    }

    char url[20];
    snprintf(url, sizeof(url), "pulse://%d", nr);

    LOGI("Playing %s", url);

    char caching[32];
    snprintf(caching, sizeof(caching), "--live-caching=%d", args.live_caching);

    const char *vlc = get_vlc_command();

    // let's become VLC
    execlp(vlc, vlc, "-Idummy", caching, "--play-and-exit", url, NULL);

    LOGE("Could not start VLC: %s", vlc);
    return 1;
}
