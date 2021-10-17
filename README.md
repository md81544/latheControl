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

To make the binary on a Pi, type `make release`. This expects real hardware to be present.

To run unit tests, `cd` to the `test` directory and type `make`.

### CMake Building

While I'm testing CMake building, you can do the above OR use cmake. To build with CMake, from the
project root, issue the following commands:

    mkdir build
    cd build
    cmake ..
    make debug (or make release)

`make debug` is the same as just `make` with the Makefile, and `make release` should be built
on a real Pi. Only `make debug` includes the building of the unit tests. To run the unit tests,
you can just run `make test`.

## Connecting Hardware
Note that the software will take the pin high then low for each pulse, so the pins you specify in the config file should be connected to the positive inputs for the stepper controller. All the negative input pins should be tied together and connected to the Pi's GND pin.

## Running, and Performance
This is designed to run on a stock Raspberry Pi 3 Model B. Usage of the GPIO pins (via the pigpio library) requires root privileges, so run the program via `sudo`.

This program is very time-critical. You shouldn't need to use `nice`, as, internally, it sets the critical background (motor-related) threads to realtime scheduling. While working on adapting the software to work with my milling machine, I installed it on a different Pi and experienced UI stutters. I then downloaded and installed a fresh PiOS image to the Pi, and rebooted, and everything worked as expected (so, without investigating too far, I assumed I had some extraneous services installed before on that Pi, which were pre-empting the non-realtime UI thread).

**Update on the above** - I've noticed that on a fresh Pi install I get approximately once-per-second noticeable pre-emption of the motor threads (which are meant to be running at highest realtime priority). This exhibits as an obvious "stutter" when the carriage (either axis) is moving. After lots of investigation, I find this is related to the kernel in use. I am not sure why yet. I reverted to an older kernel, (4.19.97, which is quite old, but is the version I was running previously for the lathe) and all works OK again. I will, at some time, attempt to bisect my way up the kernel versions to see where the issue occurs. To revert to a previous kernel on the Pi, follow [these instructions](https://isahatipoglu.com/2015/09/29/how-to-upgrade-or-downgrade-raspberrypis-kernel-servoblaster-problem-raspberry-pi2/).

