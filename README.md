# electronicLeadScrew
Code for controlling a lathe's lead screw (Z-axis) with a stepper motor.

![Screen shot](https://www.martyndavis.com/wp-content/uploads/2020/10/els2.png "")

Now also will control the X-axis (cross-slide).

Allows for cutting threads, tapers, and general automated operations.

Intended to be an aid for manual machining, rather than full CNC operation.

As this repository contains a submodule now, use `git clone --recurse-submodules <repo URL>` to clone it

You'll need to do the following (on deb-based systems) to install required packages:

    sudo apt install libfmt-dev
    sudo apt install libsfml-dev

The makefile also expects `cppcheck` and `ctags`:

    sudo apt install cppcheck ctags

To make a binary to run on non-Pi hardware, just type `make`. This mocks out the pigpio library usage, and also provides mock objects for all the steppers and rotary encoder, which means you can test it on a PC without needing to be in the workshop.

To make the binary on a Pi, type `make release`.

To run unit tests, `cd` to the `test` directory and type `make`.
