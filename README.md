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

    sudo apt install cmake libfmt-dev libsfml-dev

on a Mac, you can use homebrew (if you've got it installed):

    brew install cmake fmt sfml

### Note!
SFML **3.x** is now required (owing to SFML 2.x failing to compile on Mac owing to the latest clang++ failing to cope with `std::basic_string<int32>` used in SFML 2.x). At the time of writing, SFML 3.0 is not available on the latest Raspberry Pi repos - so you'll need to check out the sources, install SFML's dependencies, build (use `cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON` to force dynamic libraries), and install manually if you want to build on Pi hardware. See https://www.sfml-dev.org/tutorials/3.0/getting-started/build-from-source/ for more information.

## Building

To make a binary to run on non-Pi hardware, just type `make`. This mocks out the pigpio library usage, and also provides mock objects for all the steppers and rotary encoder, which means you can test it on a PC without needing to be in the workshop. This build includes debugging symbols.

To make the binary on a Pi, type `make release`. This will fail unless you're on a Pi as the pigpio headers and libraries won't be present.

To run unit tests, `make test`.

## Connecting Stepper Motors
Note that the software will take the pin high then low for each pulse to the stepper motor, so the pins you specify in the config file should be connected to the positive inputs for the stepper controller. All the negative input pins should be tied together and connected to the Pi's GND pin.

## Running, and Performance
This is designed to run on a stock Raspberry Pi 3 Model B. Usage of the GPIO pins (via the pigpio library) requires root privileges, so run the program via `sudo`.

This program is very time-critical. You shouldn't need to use `nice`, as, internally, it sets the critical background (motor-related) threads to realtime scheduling.

**Note** While working on adapting the software to work with my milling machine, I installed it on a different Pi and experienced issues with motor motion. I see approximately once-per-second noticeable pre-emption of the motor threads (which are meant to be running at highest realtime priority). This exhibits as an obvious "stutter" when the carriage (either axis) is moving. After lots of investigation, I find this is related to the kernel in use. I am not sure why yet. I reverted to an older kernel, (4.19.97, which is quite old, but is the version I was running previously for the lathe) and all works OK again. I will, at some time, attempt to bisect my way up the kernel versions to see where the issue occurs. To revert to a previous kernel on the Pi, follow [these instructions](https://isahatipoglu.com/2015/09/29/how-to-upgrade-or-downgrade-raspberrypis-kernel-servoblaster-problem-raspberry-pi2/).

**Update to the above** - the problem described above does not seem to happen in more recent kernels.

## Usage
*Note: the axes are referred to as Z and X here, but these names are configurable. Z is the primary axis (axis1) and X is the secondary (axis2).*

For general **movement** of the carriages, use the arrow keys. Press the same key again (or spacebar) to stop.

Control **speed of motion** with number keys. `1`-`5` controls Z and `6`-`0` controls X. For finer control of speed, use the `-` and `+` (or `=`) keys for the Z axis, and use the leader `X` with the same keys for the X axis (for example, press `X`, then `-`).

"**Nudging**" the axes can be achieved with the `W`, `A`, `S`, and `D` keys. If you're pressing these keys a lot, look at relative motion (below) instead.

There are four **memory locations**, you can select which one is active by means of the `[` and `]` keys. Press `M` to store the current X and Z position in the current memory slot.

Axis "**leader keys**" can be used to store just the X or Z value, for example, pressing `XM` (x followed by m) will memorise just the X value.

To **return to a position**, select the memory slot with `[` or `]` and press `ENTER`. This defaults to the Z axis unless you provide a "leader" key. For example. pressing `X`, then `ENTER` will move to the stored X value.

To move to an **absolute position**, press leader + `G` - for example, `ZG` will open a dialog asking for a value for Z to go to.

**Relative motion** can be performed by pressing leader + `R` - for example `XR` will open a dialog asking for an offset for X to go to. Once a relative motion has occurred, it can be repeated by simply pressing `.`

**Zeroing** - press `XZ` or `ZZ` to zero the axis at the current position. The "axis leader" key `\` (backslash) selects both axes, so `\z` will zero out both axes at once.

**Setting a position** - press "axis leader" then `S` - for example `ZS` - then enter a value to change what the current position is. With the X axis, you can enter the value as a **diameter value** by entering a value and then pressing `D` instead of `ENTER`. This sets the X value to half of the diameter and displays the actual diameter in the status line at the bottom of the screen.

**Retraction** - `R` retracts the X axis a small amount. Press again to unretract. Retraction can be negative for boring operations, see "Special functions" below.

**Fast return** - pressing `F` will rapidly return the Z axis to the current memory slot.

**Last Position** This is **EXPERIMENTAL** - press `L` to return the Z axis (only) to its last position. This works with multiple positions, so if you go from 0.0mm to, say, 5.0mm then 7.0mm, pressing `L` once will return to 5.0mm, and pressing it again will return to 0.0mm. If there are no positions in the stack then pressing `L` does nothing.

**Quit** - press `Ctrl`+`Q` to exit the application.

**Shutdown** - press shift and `*` (asterisk) to shutdown the computer (prior to power down)

### Special functions
Press the leader `F2` and one of the following:
* `S` - setup
* `T` - threading
* `P` - tapering (negative angles mean the piece gets wider nearer the chuck)
* `R` - set retract mode (retract might need to be inwards rather than outwards if you're boring a hole)
* `O` - radius cutting (this is **EXPERIMENTAL** - use care!)
