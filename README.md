# USBaudio

This tool forwards audio from an Android device to the computer over USB. It
works on _Linux_ with _PulseAudio_.

The purpose is to enable [audio forwarding][issue14] while mirroring with
[scrcpy]. However, it can be used independently, and does not require USB
debugging enabled.

_Note that AOA audio, the feature used by USBaudio, is [deprecated] since
Android 8.0. For Android 10, use [sndcpy] instead._

[deprecated]: https://source.android.com/devices/accessories/aoa2

[issue14]: https://github.com/Genymobile/scrcpy/issues/14
[scrcpy]: https://github.com/Genymobile/scrcpy
[sndcpy]: https://github.com/rom1v/sndcpy

## Build

Install the following packages (on _Debian_):

    sudo apt install gcc git meson vlc libpulse-dev libusb-1.0-0-dev

Then build:

    git clone https://github.com/rom1v/usbaudio
    cd usbaudio
    meson x --buildtype=release
    cd x
    ninja

To install it:

    sudo ninja install


## Run

Plug an Android device.

If USB debugging is enabled, just execute:

```
usbaudio
```

You can specify a device by _serial_ or by _vendor id_ and _product id_:


```bash
# the serial can be found via "adb device" or "lsusb -v"
usbaudio -s 0123456789abcdef

# the vid:pid is printed by "lsusb"
usbaudio -d 18d1:4ee2
```

To stop playing, press Ctrl+C.

To stop forwarding, unplug the device (and maybe restart your current audio
application).

To only enable audio accessory without playing, use:

```bash
usbaudio -n
```

## Blog post

 - [Introducing USBaudio][blogpost]

[blogpost]: https://blog.rom1v.com/2019/06/introducing-usbaudio/
