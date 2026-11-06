# latheControl
Code for controlling a lathe's lead screw (Z-axis) with a stepper motor.

![Screen shot](https://www.martyndavis.com/wp-content/uploads/2020/10/lc.png "")

Now also will control the X-axis (cross-slide).

Allows for cutting threads, tapers, and general automated operations.

Intended to be an aid for manual machining, rather than full CNC operation.

## Getting the Code

Just `git clone <repo URL>`. The stepper motor code lives in a separate repository, [stepperControl](https://github.com/md81544/stepperControl), which CMake downloads automatically when you first build (so the first build needs network access). The version used is pinned by `GIT_TAG` in `CMakeLists.txt`.

If you're changing stepperControl at the same time, clone it alongside and build against your local copy instead:

    make STEPPERCONTROL_DIR=../stepperControl

(or `export STEPPERCONTROL_DIR=../stepperControl` in your shell profile on a development machine).

## Dependencies

You'll need to do the following (on deb-based systems) to install required packages:

    sudo apt install cmake libfmt-dev qt6-base-dev

on a Mac, you can use homebrew (if you've got it installed):

    brew install cmake fmt qt

The user interface uses Qt 6 (Widgets), which is available from the standard repositories on Raspberry Pi OS Bookworm or later.

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

**If a once-per-second stutter does appear**, a likely cause is the kernel's real-time throttling: by default, real-time threads may only use 95% of each second (`cat /proc/sys/kernel/sched_rt_runtime_us` shows `950000`), and a motor thread busy-waiting between steps during a long move can hit that limit, at which point it is paused for the remaining ~50ms. Whether this happens depends on the kernel version and configuration, which would fit the behaviour above. To test the theory, disable throttling with `sudo sysctl kernel.sched_rt_runtime_us=-1` (this lasts until reboot; the Pi has enough cores for this to be safe here) and see whether the stutter goes away. Motor threads sleep when their motor is idle, so this can only affect a motor while it is moving.

## Usage
*Note: the axes are referred to as Z and X here, but these names are configurable. Z is the primary axis (axis1) and X is the secondary (axis2).*

For general **movement** of the carriages, use the arrow keys. Press the same key again (or spacebar) to stop. Pressing `Alt` with an arrow key sets the carriage moving at its maximum speed (useful for moving tools out of the way, not actual cutting).

Control **speed of motion** with number keys. `1`-`5` controls Z and `6`-`0` controls X. For finer control of speed, use the `-` and `+` (or `=`) keys for the Z axis, and use the leader `X` with the same keys for the X axis (for example, press `X`, then `-`).

"**Nudging**" the axes can be achieved with the `W`, `A`, `S`, and `D` keys. Eacn "nudge" is 0.025mm. To move less (0.002mm), press `shift` plus a nudge key. To move more (0.1mm), press `Alt` plus a nudge key. If you're pressing these keys a lot, look at relative motion (below) instead.

There are four **memory locations**, you can select which one is active by means of the `[` and `]` keys. Press `M` to store the current X and Z position in the current memory slot.

Axis "**leader keys**" can be used to store just the X or Z value, for example, pressing `XM` (x followed by m) will memorise just the X value.

To **return to a position**, select the memory slot with `[` or `]` and press `ENTER`. This defaults to the Z axis unless you provide a "leader" key. For example. pressing `X`, then `ENTER` will move to the stored X value.

To move to an **absolute position**, press leader + `G` (for "go to") - for example, `ZG` will open a dialog asking for a value for Z to go to.

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
