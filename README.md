# latheControl
Code for controlling a lathe's lead screw (Z-axis) with a stepper motor.

![Screen shot](https://www.martyndavis.com/wp-content/uploads/2020/10/els2.png "")

Now also will control the X-axis (cross-slide).

Allows for cutting threads, tapers, and general automated operations.

Intended to be an aid for manual machining, rather than full CNC operation.

## Getting the Code

As this repository contains a submodule now, use `git clone --recurse-submodules <repo URL>` to clone it

## Dependencies

You'll need to do the following (on deb-based systems) to install required packages:

    sudo apt install libfmt-dev
    sudo apt install libsfml-dev

The makefile also expects `cppcheck` and `ctags`:

    sudo apt install cppcheck ctags

## Building

To make a binary to run on non-Pi hardware, just type `make`. This mocks out the pigpio library usage, and also provides mock objects for all the steppers and rotary encoder, which means you can test it on a PC without needing to be in the workshop. This build includes debugging symbols.

To make the binary on a Pi, type `make release`. This expects real hardware to be present.

To run unit tests, `cd` to the `test` directory and type `make`.

## Connecting Hardware
Note that the software will take the pin high then low for each pulse, so the pins you specify in the config file should be connected to the positive inputs for the stepper controller. All the negative input pins should be tied together and connected to the Pi's GND pin.

## Running, and Performance
This is designed to run on a Raspberry Pi Model B. Usage of the GPIO pins (via the pigpio library) requires root privileges, so run the program via sudo.

This program is very time-critical. You shouldn't need to use `nice`, as internally, it sets most of the critical background (motor-related) threads to realtime scheduling. While working on adapting the software to work with my milling machine, I installed it on a different Pi and experienced UI stutters. I then downloaded and installed a fresh PiOS image to the Pi, and rebooted, and everything worked as expected (so, without investigating too far, I assumed I had some extraneous services installed before on that Pi, which were pre-empting the non-realtime UI thread).

