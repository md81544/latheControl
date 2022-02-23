# latheControl
Code for controlling a lathe's lead screw (Z-axis) with a stepper motor.

![Screen shot](https://www.martyndavis.com/wp-content/uploads/2020/10/lc.png "")

Now also will control the X-axis (cross-slide).

Allows for cutting threads, tapers, and general automated operations.

Intended to be an aid for manual machining, rather than full CNC operation.

## Getting the Code

As this repository contains a submodule now, use `git clone --recurse-submodules <repo URL>` to clone it

## Dependencies

You'll need to do the following (on deb-based systems) to install required packages:

    sudo apt install cmake libfmt-dev libsfml-dev cppcheck exuberant-ctags

## Building

To make a binary to run on non-Pi hardware, just type `make`. This mocks out the pigpio library usage, and also provides mock objects for all the steppers and rotary encoder, which means you can test it on a PC without needing to be in the workshop. This build includes debugging symbols.

To make the binary on a Pi, type `make release`. This will fail unless you're on a Pi as the pigpio headers and libraries won't be present.

To run unit tests, `make test`.

## Connecting Hardware
Note that the software will take the pin high then low for each pulse, so the pins you specify in the config file should be connected to the positive inputs for the stepper controller. All the negative input pins should be tied together and connected to the Pi's GND pin.

## Running, and Performance
This is designed to run on a stock Raspberry Pi 3 Model B. Usage of the GPIO pins (via the pigpio library) requires root privileges, so run the program via `sudo`.

This program is very time-critical. You shouldn't need to use `nice`, as, internally, it sets the critical background (motor-related) threads to realtime scheduling. While working on adapting the software to work with my milling machine, I installed it on a different Pi and experienced UI stutters. I then downloaded and installed a fresh PiOS image to the Pi, and rebooted, and everything worked as expected (so, without investigating too far, I assumed I had some extraneous services installed before on that Pi, which were pre-empting the non-realtime UI thread).

**Update on the above** - I've noticed that on a fresh Pi install I get approximately once-per-second noticeable pre-emption of the motor threads (which are meant to be running at highest realtime priority). This exhibits as an obvious "stutter" when the carriage (either axis) is moving. After lots of investigation, I find this is related to the kernel in use. I am not sure why yet. I reverted to an older kernel, (4.19.97, which is quite old, but is the version I was running previously for the lathe) and all works OK again. I will, at some time, attempt to bisect my way up the kernel versions to see where the issue occurs. To revert to a previous kernel on the Pi, follow [these instructions](https://isahatipoglu.com/2015/09/29/how-to-upgrade-or-downgrade-raspberrypis-kernel-servoblaster-problem-raspberry-pi2/).

## Usage
Note: the axes are referred to as Z and X here, but these names are configurable. Z is the primary axis (axis1) and X is the secondary (axis2).

For general **movement** of the carriage, use the arrow keys. Press the same key again (or spacebar) to stop.

Control **speed of motion** with number keys. `1`-`5` controls Z and `6`-`0` controls X. For finer control of speed, use the `-` and `+` (or `=`) keys for the Z axis, and use the leader `x` with the same keys for the X axis (for example, press `x`, then `-`).

"**Nudging**" the axes can be achieved with the `w`, `a`, `s`, and `d` keys. If you're pressing these keys a lot, look at relative motion (below) instead.

There are four **memory locations**, you can select which one is active by means of the `[` and `]` keys. Press `m` to store the current X and Z position in the current memory slot.

Axis "**leader keys**" can be used to store just the X or Z value, for example, pressing `xm` (x followed by m) will memorise just the X value.

To **return to a position**, select the memory slot with `[` or `]` and press `ENTER`. This defaults to the Z axis unless you provide a "leader" key. For example. pressing `x`, then `ENTER` will move to the stored X value.

To move to an **absolute position**, press leader + `g` - for example, `zg` will open a dialog asking for a value for Z to go to.

**Relative motion** can be performed by pressing leader + `r` - for example `xr` will open a dialog asking for an offset for X to go to. Once a relative motion has occurred, it can be repeated by simply pressing `.`

**Zeroing** - press `xz` or `zz` to zero the axis at the current position.

**Retraction** - `r` retracts the X axis a small amount. Press again to unretract. Retraction can be negative for boring operations, see "Special functions" below.

**Fast return** - pressing `f` will rapidly return the Z axis to the current memory slot.

**Quit** - press `Ctrl`+`Q` to exit the application.

**Shutdown** - press shift and `*` (asterisk) to shutdown the computer (prior to power down)

### Special functions
Press the leader `F2` and one of the following:
* `s` - setup
* `t` - threading
* `p` - tapering (negative angles mean the piece gets wider nearer the chuck)
* `r` - set retract mode (retract might need to be inwards rather than outwards if you're boring a hole)
* `o` - radius cutting (this is **EXPERIMENTAL** - use care!)