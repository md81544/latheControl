# electronicLeadScrew
Code for controlling a lathe's lead screw (Z-axis) with a stepper motor.

Now also will control the X-axis (cross-slide).

Allows for cutting threads, tapers, and general automated operations.

Intended to be an aid for manual machining, rather than full CNC operation. 

As this repository contains a submodule now, use `git clone --recurse-submodules <repo URL>` to clone it

## Running the program
Run the program with real-time scheduling, for example:

    sudo chrt --rr 1 ./els
    
Don't use too high a value for the priority or the UI gets starved.

Running like this sometimes results in a black screen - this is to be investigated.
