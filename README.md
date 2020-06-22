# electronicLeadScrew
Code for controlling a lathe's lead screw with a stepper motor

As this repository contains a submodule now, use `git clone --recurse-submodules <repo URL>` to clone it

## Running the program
Run the program with real-time scheduling, for example:

    sudo chrt --rr 1 ./els
    
Don't use too high a value for the priority or the UI gets starved.
