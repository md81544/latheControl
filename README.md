# electronicLeadScrew
Code for controlling a lathe's lead screw (Z-axis) with a stepper motor.

Now also will control the X-axis (cross-slide).

Allows for cutting threads, tapers, and general automated operations.

Intended to be an aid for manual machining, rather than full CNC operation. 

As this repository contains a submodule now, use `git clone --recurse-submodules <repo URL>` to clone it

You'll need to do the following (on deb-based systems) to install required packages:

    sudo apt install libfmt-dev
    sudo apt install libsfml-dev
    sudo apt install libncurses-dev
    
(ncurses was already installed on my fresh Kubuntu 20.04 install)

The makefile also expects `cppcheck` and `ctags`:

    sudo apt install cppcheck ctags
