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

**The menu** - press `Tab` or `F1` (or, on the gamepad, the previous and next memory slot buttons together; see `JoystickMenuChord` in lc.cfg) to open a menu of everything you can do, with each command's keyboard shortcut alongside. Use the arrow keys (or the gamepad's D-pad, and its Enter button) to move around it (`Up` from the top goes round to the bottom, and `Down` from the bottom to the top), `Enter` or `Right` to choose, `Left` or `Esc` to go back, and `Tab` again to close it. Categories, and the less frequent commands, have a hotkey letter (shown yellow and underlined): for example `Tab`, `M`, `T` for Modes, Threading. While the menu is open, letters typed are only menu hotkeys, not their usual shortcuts. The menu doesn't stop anything: motors keep running while it's open, `Space` still stops all motors, and rapids (`Alt` + arrow) still work. Commands which can't be used at the moment (e.g. moving a locked axis) are greyed out.

**Leader key hints** - if you pause for half a second after pressing a leader key (`Z`, `X` or `\`), a panel lists the keys which can follow it, and what each does. If you type the whole shortcut quickly, it doesn't appear.

For general **movement** of the carriages, use the arrow keys. Press the same key again (or spacebar) to stop. Pressing `Alt` with an arrow key sets the carriage moving at its maximum speed (useful for moving tools out of the way, not actual cutting).

Control **speed of motion** with number keys. `1`-`5` controls Z and `6`-`0` controls X. For finer control of speed, use the `-` and `+` (or `=`) keys for the Z axis, and use the leader `X` with the same keys for the X axis (for example, press `X`, then `-`).

"**Nudging**" the axes can be achieved with the `W`, `A`, `S`, and `D` keys. Each "nudge" is 0.05mm. To move less (0.01mm), press `shift` plus a nudge key. To move more (0.1mm), press `Alt` plus a nudge key. If you're pressing these keys a lot, look at relative motion (below) instead.

There are six **memory locations**, you can select which one is active by means of the `[` and `]` keys. Press `M` to store the current X and Z position in the current memory slot.

Axis "**leader keys**" can be used to store just the X or Z value, for example, pressing `XM` (x followed by m) will memorise just the X value.

To **return to a position**, select the memory slot with `[` or `]` and press `ENTER`. This defaults to the Z axis unless you provide a "leader" key. For example. pressing `X`, then `ENTER` will move to the stored X value.

To move to an **absolute position**, press leader + `G` (for "go to") - for example, `ZG` will open a dialog asking for a value for Z to go to.

**Relative motion** can be performed by pressing leader + `R` - for example `XR` will open a dialog asking for an offset for X to go to. Once a relative motion has occurred, it can be repeated by simply pressing `.`

**Zeroing** - press `XZ` or `ZZ` to zero the axis at the current position. The "axis leader" key `\` (backslash) selects both axes, so `\z` will zero out both axes at once.

**Setting a position** - press "axis leader" then `S` - for example `ZS` - then enter a value to change what the current position is. With the X axis, you can enter the value as a **diameter value** by also pressing `D` (which ticks the "set as diameter" option) before `ENTER`. This sets the X value to half of the diameter and displays the actual diameter in the status line at the bottom of the screen. Once a diameter has been set, the **surface speed** (Vc, in m/min) is also shown, beside the spindle speed. Both last until X is zeroed, or its position is set again (other than as a diameter).

**Current feature** (lathe; `CurrentFeature` in lc.cfg) - the program remembers the diameter of the surface you're working on and the Z of the work's end, shown in a "Current feature" box. Unlike the diameter in the status line, the feature's diameter doesn't change as X moves. Setting X as a diameter (above) records it, as the tool is on the surface you've just measured; `XD` records the diameter at the tool again at any time (X must have been set as a diameter). After facing, `ZE` marks Z here as the work's end: the free end of the work, not a shoulder part way along. Both are also in the menu, under Feature, along with clearing them. They're kept for this session only, and each goes when its axis's position does: a re-zero, a motor fault, or setting the position (unless adjusting); the diameter also goes when X is no longer set as a diameter. Before a re-zero or a position set would clear any of it, a dialog asks first (`Enter` or `Y` to go ahead, `Esc` or `N` to cancel). Setting X as a diameter doesn't ask, as it replaces the diameter with the one just measured. A multi-pass which cuts from the work's end sets the diameter to the one it finished at. The menu's Feature category can also go back to it: to its start (X on the surface and Z at the work's end, either exactly or clear of it by `FeatureStartClearance`, away from the chuck as set by `Axis1TowardsChuck`), with X retracting before Z moves and coming in after; X alone to the surface; or Z alone to the work's end (exactly or clear of it), which is refused if X is deeper into the work than the feature's surface. Not while threading, tapering or during a multi-pass.

**Limit switches** (optional; `LimitSwitchPin` in lc.cfg) - any limit switch stops all the motors at once, and ends whatever was running (e.g. a multi-pass). A message flashes, and a dialog opens: until you clear the stop (`Enter` or `Y` in the dialog, or later from the menu, under System), nothing will move, and only keys which don't move anything work (`Space`, `Esc`, `U`, quitting). This is so that, for example, repeated nudges into a switch can't go unnoticed. Once cleared, move off the switch carefully: all the switches share one pin, so the program can't tell which one it is, and won't stop you moving further into it. While a switch is still pressed, the message says so; it stops the motors again only after it has been released and pressed again. Normally-closed switches wired in series are recommended (see lc.cfg), so that a broken wire or an unplugged socket stops the motors too. The position isn't lost, so nothing needs re-zeroing.

**Retraction** - `R` retracts the X axis by `Axis2RetractionDistance` in lc.cfg (2 mm unless set; of X travel, not on the diameter). Press again to unretract. Retraction can be inwards for boring operations, see "Special functions" below. When boring a small hole, check that the back of the boring bar has room for it. A multi-pass (and threading passes) retracts differently: just clear of where it started, see below.

**Locking an axis** - `ZK` or `XK` locks the axis in software, or unlocks it again; locked, its label is grey. Use it when the axis is locked physically (e.g. the cross-slide's lock), so that nothing tries to drive the stepper against the lock: X's open-loop stepper would lose its position without anything noticing. Nothing moves a locked axis: not the keys, memory returns, fast returns, retracts (including `ThreadingAutoRetract`), the current feature's go-tos, or a multi-pass. Each is refused, with a message to unlock it first. In a taper or radius, X follows Z, so Z doesn't move while X is locked either. An axis can't be locked while it's moving, or while a multi-pass is cutting; one that's paused can be, and doesn't carry on until it's unlocked.

**Fast return** - pressing `Z` then `F` will rapidly return the Z axis to the current memory slot (`X` then `F` for the X axis; `\` then `F` for both).

**Last Position** This is **EXPERIMENTAL** - press `L` to return the Z axis (only) to its last position. This works with multiple positions, so if you go from 0.0mm to, say, 5.0mm then 7.0mm, pressing `L` once will return to 5.0mm, and pressing it again will return to 0.0mm. If there are no positions in the stack then pressing `L` does nothing.

**Inches** - press `U` to switch the display between mm and inches (also in the menu, under System). In inches, positions are shown to four decimal places, feeds in in/min and the surface speed in SFM, and values you type into dialogs (e.g. go to, offset, set position) are in inches too: each dialog says which units it expects. Axes measured in degrees (e.g. a rotary table) aren't affected. Everything is still metric internally, and the program always starts in mm.

**Quit** - press `Ctrl`+`Q` to exit the application.

**Shutdown** - press shift and `*` (asterisk) to shutdown the computer (prior to power down)

### Special functions
Open the menu's Modes (`Tab` then `M`) and press one of the following:
* `S` - setup
* `T` - threading: choose a type of thread (e.g. metric coarse, BSW), then a size, and press `M` for male (external) or female (internal). The dialog shows the diameter to turn or bore, and the depth to cut, with a diagram of the profile; female threads also set X to retract inwards. The sizes come from `threadpitches.cfg` (or the file set by `ThreadsFile` in lc.cfg), which can be edited to add more: see the comments at its top. In this mode, each Z move (e.g. to a memory slot) is a threading pass, with Z locked to the spindle's position as if geared to it: it moves the pitch for every revolution, from its start, however the spindle's speed varies. Every pass starts at the same angle of the spindle, so passes follow the same thread. (This needs a whole number of encoder pulses per spindle revolution; `RotaryEncoderPulsesPerRev` x the gearing.) With `ThreadingAutoRetract`, X retracts at the end of a cut and comes back in at the end of the return. The menu's Modes turns this on and off ("Auto-retract is ON" or "OFF"): `A` toggles it, and the menu stays open so you can see which way it's now set; `Enter` closes the menu. It starts as the config says and isn't saved, and the threading display shows whether it's on: e.g. turn it off while X is locked, rather than having each pass warn that X is locked. If the spindle speeds up beyond what Z can follow, the pass stops with a warning.
  **Threading passes** (**EXPERIMENTAL**; `Tab`, `M`, `N` in threading mode) cut the thread pass by pass, as a multi-pass (below) does: the same dialog, with X's infeed starting at the thread's depth (for a new thread; otherwise as last time) and the step-over being the infeed per pass. X always retracts between passes, as the return isn't locked to the spindle: to `MultiPassRetractClearance` beyond the start (where the tool touched the work, so also where the thread's crests are), as a multi-pass does. For a female thread, this keeps the boring bar from moving across the bore by a fixed 2 mm. Spring passes retract to the same place. Then `Enter` moves to the start, and `Enter` again starts the passes: each is a threading pass, locked to the spindle and starting at the same angle; X retracts, Z returns at full speed, and X comes back in and feeds in for the next. Pausing, nudging X while paused, and `Enter` during a pass to pause after it all work as for a multi-pass. Once the passes have finished, `Enter` takes a **spring pass** at the same depth and pauses: nudge X in first if you like (`W`/`S`, or `Shift` for the fine nudge; not the `Alt` nudges, as releasing `Alt` stops everything), and `Enter` takes another, until the nut fits; `Space` ends them. To try the nut on, move the tool out of the way: between spring passes, X can be moved out (`S`/`s`) and Z along (`Left`/`Right`, locked to the spindle as any Z move in threading mode is; again, or `Space`, to stop); or press `Space` first to end them, after which anything goes (e.g. retracting with `R`). `Enter` first moves back to the start of the last pass, at its depth (saying "Stop the motors first" if Z is still moving), and `Enter` again takes the pass. (Stay in threading mode meanwhile: leaving it ends the spring passes. Z only moves by key in threading mode with the spindle running, but the move back doesn't need it to be.) X deeper than the last pass (nudged in) is taken as meant, and the pass is cut there; X shallower (moved out) always goes back first, even while paused. To run all the passes again, set them up again from the menu. Two more options in the dialog are on unless turned off (they're remembered): **constant area** (`A`) makes each pass remove about the same area, so the passes get shallower as they go deeper (what you type is then the first pass's infeed; pass n goes in to that times the square root of n, but at least `ThreadingMinimumInfeed`, 0.02 mm, further than the pass before), and **compound infeed** (`C`) moves each pass's start along Z by its depth times tan 29.5° (for a 60° thread; Whitworth's is 27°), as a topslide set over would, so only the leading flank cuts. With the compound infeed, the start moves the way the cut goes, by up to about 0.57 of the depth (the dialog says how far), so start at least that far off the work. The cut's end doesn't move. `ThreadingAutoRetract` isn't used meanwhile. If the encoder loses pulses during the passes (see the threading warning above), every later pass would cut a slightly different thread, so they stop: during a cut, once it's finished and Z has returned, with X left retracted (stopping part way would leave the tool cutting a ring in the thread). Neither the passes nor spring passes will start again on that thread; choosing the thread again (e.g. for new work) clears it. While they're set up, `Enter` runs them rather than going to the memory slot (`ZEnter` still does); choosing a thread again, or leaving threading mode, goes back to passes by hand
* `P` - tapering: choose a preset (Morse tapers MT0-MT6, 45 and 30 degree chamfers) or type an angle, and press `I` to choose whether the tool moves in or out as it approaches the chuck. A diagram shows the tool's path (exaggerated for small angles). Presets are in `tapers.h`
* `R` - set retract mode (retract might need to be inwards rather than outwards if you're boring a hole). The direction at start-up is set by `Axis2RetractionDirection` in lc.cfg
* `O` - radius cutting (this is **EXPERIMENTAL** - use care!)
* `M` - multi-pass (**EXPERIMENTAL**): repeated cuts along Z, stepping X over between them. First store the start of the cut (Z and X) in Mem 1, and the end in Mem 2. Enter the X step-over per pass, and optionally press `P` to pause between passes and/or `R` to retract X before each return. Each retract is to `MultiPassRetractClearance` (0.25 mm unless set) beyond the start, where the tool touched the work, rather than a fixed distance: that's clear of the work however deep the passes have gone, and in a bore, the back of the boring bar only comes that much nearer the far side than it was when you touched off. (So does the retract before moving to the start.) `R` cycles through No, Every pass, and Last 2 passes: the roughing passes then return without retracting, which is quicker, and only the finishing ones retract, so they don't leave a spiral. (How many is `MultiPassRetractLastPasses` in lc.cfg. With a step-over of zero, every pass retracts.) Then go to Mem 1 (e.g. `ZEnter` and `XEnter`) and press `Enter` to start. The first pass steps X in from the start (where the tool touched the work), so e.g. 0.1 mm in steps of 0.1 mm is one pass. Each pass cuts from Mem 1's Z to Mem 2's at the Z speed set (which you can change during the cut, for the passes after), retracts if chosen, returns at full speed, and steps X over towards Mem 2's X (the last step may be smaller, to finish exactly there). If pausing, X is brought back in (if retracted) at the start of the cut, and you press `Enter` for each pass; meanwhile, X can be nudged (`W`/`S`, not the `Alt` nudges), and the next pass steps over from there. Without pausing, pressing `Enter` during a pass pauses once it's done (e.g. to clear swarf; `Enter` again cancels that), then `Enter` continues as usual. A step-over of zero repeats the same cut until stopped: with pausing, nudge in and press `Enter` for each cut. With the current feature set (both its diameter and the work's end), the dialog's start can be **Feature** (its default, unless the tool is at a memory slot): X on the feature's surface, and Z just off the work's end (by `FeatureStartClearance`, 0.5 mm unless set), away from the cut, with Z's distance measured from the work's end. This assumes the feature's diameter runs from the work's end, as when turning down from the end. With `TaperCorrection` set in the config (**EXPERIMENTAL**: see lc.cfg), each cut is made as a tiny taper, to cancel out a taper the lathe turns itself; X is shown as if it weren't moving. While it runs, only `Space` (which stops it), `Esc`, the speed keys and `U` work
