# TODO

## User interface

### Finish the move to Qt (`qt-ui` branch)
- [ ] Try it on the Pi (`make release`): fullscreen display and scaling,
      keyboard (including Alt and Right Alt / AltGr rapids, and that releasing
      them stops), dialogs on the Pi's desktop
- [ ] Check the gamepad's axes and buttons map as they did with SFML. If a
      stick or the D-pad acts on the wrong axis or direction, the mapping is in
      `Joystick::tryOpen()` (joystick.cpp)
- [ ] Merge `qt-ui` into `main`

### Make the interface less cryptic
- [ ] A key that brings up all the options available *now*. It should depend on
      context: after an axis leader key (e.g. `Z`), show what each key does
      with that axis; in threading/taper/radius mode, show what's allowed there
- [ ] It must be usable entirely from the keyboard or the gamepad, since a
      mouse is awkward at the machine. Every menu and dialog needs to be
      navigable with keys and with the gamepad (e.g. D-pad to move, buttons to
      select and go back). Note the gamepad isn't currently read while a
      dialog is open
- [ ] Proper menus and dialogs, now that Qt makes them possible. This replaces
      the old "implement a proper menu/dialog (see level_designer)" and
      "move all input to the new modal dialog system" items
- [ ] Expand use of dialogs. For instance the help screen could be much
      larger, which needs a resizable (or dynamically expanding) box. May want
      multiple inputs within one dialog
- [ ] Make the display more "modular": display elements in definable boxes,
      perhaps a class which encapsulates a display element. This will make the
      layout easier to change
- [ ] Dialogs don't scale with the main display (which is scaled to fit the
      screen), so look small on a large screen
- [ ] The nudge amounts in the README and the F1 help (0.025mm) don't match
      the code (0.01, 0.05 and 0.1mm)
- [ ] Make use of the keys on the right (#, Home, PgUp, PgDn, End)?
- [ ] Remove Alt-Arrows for rapids once controller set up for mill?
      Now that they behave more safely, maybe not
- [ ] Touchscreen support (not that I have touch screens yet)

### New displays
- [ ] Display Vc (surface speed in m/min) if diameter is set. Note the SI
      unit is metres per minute, not mm, so `Vc = (M_PI * D * n) / 1000.0;`
- [ ] Support display in thousandths of an inch as an option. Should be
      simple to do in the view, keeping everything metric underneath

### Controller structure
- [ ] Move the controller onto Qt's event loop (e.g. a timer driving each
      update) instead of its own polling loop. At the moment the UI thread
      blocks in places: `wait()` on motors, `waitForAxisToStop()`, and
      `callbackAtZeroDegrees()`, which can spin for up to a spindle revolution
- [ ] Address the "Mode" concept, which provides pseudo-modal "dialogs"
      interleaved into program flow and is difficult to maintain. If all
      motors are stopped during a dialog, they can all go on a separate flow

## Multi-pass (experimental; leave until the controller is event-driven)
- [ ] It doesn't work correctly. The cause, at least with "retract between
      passes" on:
    - `multiPassEndCut()` retracts and returns `WaitForMotors` *before*
      calling `axis1FastReturn()`, so the fast return never happens
    - `Controller::run()` then forces the stage to `StepOver` whatever stage
      the model was in, so X steps over while still retracted
    - `multiPassNextCut()` then un-retracts to the position saved *before*
      the step-over, undoing it, and the cycle repeats
- [ ] Rewrite the sequencing as a state machine in the model, advanced by
      motor-stopped events, with the model owning its stage (the UI mustn't
      override it). "Pause between passes" becomes a state waiting for a
      "continue" command rather than a blocking dialog. Unit test the sequence
      of positions with the mock GPIO
- [ ] If using "press a key" between cuts, the display should be updated
      before "press a key" happens
- [ ] Work on a single line rather than a plane, i.e. if mem0.x == mem1.x,
      interpret it as (say) slot cutting and repeat until the user cancels.
      Could also work if only one axis is defined in config
- [ ] Automation of a repeated cut: cut from Mem 1 to Mem 2, fast return,
      wait for a key press, and repeat until cancelled (to make repeated cuts
      down to depth easier). Could just be multi-pass with "press any key"
      between passes

## Motion and timing (mostly in stepperControl)
- [ ] Deceleration ramping. At the moment there's only acceleration, so
      stops (especially from rapids) are abrupt. Needs tuning on the machine,
      and changes where moves end. Also, the ramp rate is per loop, so depends
      on the step rate
- [ ] Sleep between steps at slow speeds, rather than busy-waiting (idle
      motors already sleep). Needs the wake-up latency measuring on the Pi
      first, as thread pitch depends on step timing
- [ ] Consider a real-time priority below 99, which is shared with critical
      kernel threads. Test on the machine
- [ ] If a once-per-second stutter reappears, check real-time throttling
      (see the README)
- [ ] Threading currently matches the carriage's *speed* to the averaged
      spindle RPM, so any change in spindle speed under load becomes pitch
      error. A true electronic leadscrew would lock the carriage's *position*
      to the encoder count
- [ ] I defined some constants for a slight Z infeed when cutting threads
      (see INFEED and SIDEFEED) but never used them. Maybe implement to
      simulate the use of the topslide
- [ ] A setting for adding a tiny amount of X motion to counteract a lathe
      that cuts a slight taper (taper mode works brilliantly, but this should
      be a config item applied automatically). Idea: calculate the angle
      required from two measurements and a distance
- [ ] Support the enable pin for each motor
- [ ] Need a way to detect loss of steps. Probably not possible without
      hooking into linear scales (possible mod to sender unit)
    - on the lathe, the Z axis controller has pins to detect loss of steps
      directly. If this is triggered, we could stop the Z axis. Not sure how
      accurate the position will be after this. To test once wired up
- [ ] Support limit switches. Could have multiple switches all on one pin,
      and when triggered simply issue a "stop all". Partially implemented
      (see `limitSwitchTriggered()`)
- [ ] Consider some form of switch or sensor to prevent carriage movement
      outside specific points (or mandate position checks at start-up to do
      this in software only?)
    - Display a warning if the chuck is running in reverse (at the moment the
      `n:` label turns red)

## Sensors
- [ ] Rotary encoder: only rising edges on pin A are counted, so it relies on
      ignoring repeated edges on the same pin to cope with dithering. Full
      quadrature decoding would be more robust (but gives four times the
      pulses per revolution, which affects the RPM and angle calculations)
- [ ] RPM on real hardware seems erratic past about 1200 rpm. Cannot
      reproduce using mocks, so maybe we're hitting a limitation of the rotary
      encoder? (Not relevant currently now I've lowered the gearing on the
      lathe's spindle)
- [ ] Maybe allow zeroing of chuck angle? (use offset in reporting)
- [ ] Linear scale: decide how best to use it. Should position come solely
      from it, rather than "dead reckoning"? Or should it just correct the
      expected position (e.g. periodically calling `setPosition()` on axis1's
      motor, or giving the motor a callback to determine position)?
    - **Before enabling `Axis1UseLinearScale`:** `getAxis1MotorCurrentStep()`
      then returns a step derived from the scale, which is passed to the
      motor's `goToStep()`. Once scale and motor drift apart, moves (e.g. a
      nudge) are out by the whole drift
- [ ] Probing, which would also be useful as a way to touch off
    - X probing: would need an insulated known-diameter bar held in the spindle
    - Z probing: would need an insulated known-length bar, which could be
      held on the end of a workpiece
    - Will need a dedicated pin on the Pi for monitoring
- [ ] Tool numbers with both Z and X offsets, for repeatable positioning
      after a tool swap? Not sure how realistic this is. Maybe some form of
      probing like on the CNC?

## Structure and refactoring
- [ ] Lots of refactoring still needed, as this grew a bit organically
- [ ] Replace all "Axis1..." and "Axis2..." references with an array, to
      allow for future expansion. Might be difficult owing to the axes'
      different behaviours
- [ ] Support three axes, to enable X, Y and a rotary table (or Z, X, and
      manual rotation of the chuck on the lathe)
    - axes should just be a collection of axis types
    - specific axes which require specialised behaviour can have attributes,
      perhaps (e.g. retraction). Determine which behaviour needs extracting
- [ ] `axis.cpp` / `axis.h` are compiled but unused, and their `mgo::Axis`
      class clashes in name with `enum class mgo::Axis` in model.h
- [ ] Set the initial X-axis retraction direction from the config file

## Housekeeping
- [ ] stepperControl: delete `push.sh` (only needed for pushing from a
      submodule's detached HEAD)
- [ ] stepperControl on GitHub: the default branch is a stale `main`; make
      `master` the default and delete `main`
