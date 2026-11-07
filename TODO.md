# TODO

## User interface

### Make the interface less cryptic
- [ ] A key that brings up all the options available *now*
    - **Done:** `Tab` opens a menu of every command, with its shortcut, built
      from one table of commands (commands.cpp) so it can't drift out of date.
      Commands not available at the moment (e.g. in Setup mode, or on a
      locked axis) are greyed out. It never blocks: motors keep running,
      Space still stops everything, and rapids and their dead-man stops still
      work. Choosing a command acts exactly as its shortcut does. Rapids are
      deliberately not in the menu, as they rely on holding the keys down
    - **Done:** open the menu from the gamepad with a button chord
      (`JoystickMenuChord`, default 4,6: previous + next memory slot). Not
      yet tried on the machine
    - **Done:** hotkey letters for the categories, Modes (as F2), System and
      axis locks, highlighted in yellow and underlined. Not given to the
      many paired or small commands, which have quick shortcuts already
    - **Done:** half a second after a leader key (`Z`, `X`, `\`, `F2`) is
      pressed, a panel shows the keys which can follow and what they do,
      from the same command table
    - **Done:** F1 opens the menu (which shows every shortcut) rather than a
      separate, hand-written help page
    - While the menu is open, memory slots 5 and 6 are hidden behind it
- [ ] It must be usable without a mouse, which is awkward at the machine.
      Everything works from the keyboard, and the menu from the gamepad too.
      The gamepad isn't read while a dialog is open; low priority, as it sits
      next to the keyboard and is for the basics
- [ ] Proper menus and dialogs, now that Qt makes them possible. This replaces
      the old "implement a proper menu/dialog (see level_designer)" and
      "move all input to the new modal dialog system" items
- [ ] Richer dialogs, with several inputs, like the taper dialog (presets,
      angle, direction, and a diagram). The other dialogs are fine for now
- [ ] Make the display more "modular": display elements in definable boxes,
      perhaps a class which encapsulates a display element. This will make the
      layout easier to change
- [ ] Make use of the keys on the right (#, Home, PgUp, PgDn, End)?
- [ ] Remove Alt-Arrows for rapids once controller set up for mill?
      Now that they behave more safely, maybe not
- [ ] When there's a larger screen (the current one is small): touchscreen
      support, and make dialogs scale with the main display (which is scaled
      to fit the screen, whereas dialogs stay the same size, so would look
      small)

### Controller structure
- [x] Move the controller onto Qt's event loop (e.g. a timer driving each
      update) instead of its own polling loop. At the moment the UI thread
      blocks in places: `wait()` on motors, `waitForAxisToStop()`, and
      `callbackAtZeroDegrees()`, which can spin for up to a spindle revolution
    - **Done:** `Controller::run()` runs Qt's event loop (via the view's
      `runEventLoop()`), with a 50ms timer calling `Controller::tick()`.
      Behaviour is unchanged: a tick that blocks (a dialog, or a wait for
      the motors) still blocks, and the timer's calls meanwhile are skipped
    - **Done:** moves which waited for a motor to finish (Z and X nudges,
      and the backlash take-up on both axes before a taper or radius move)
      are now `MotionSequence`s, which the model's `checkStatus()` steps
      through on each tick. While one runs, only the safety checks (faults,
      limit switch, spindle) apply, and the controller holds keys back until
      it's done (as they waited before), except Space, which stops it. The
      other `wait()`s follow a `stop()`, which the motor thread acknowledges
      at once, so don't block. Not yet tried on the machine
    - **Note:** deceleration ramping would make `stop()` take time, so the
      `stop()` + `wait()` pairs would then need to be sequence steps too
    - **Done:** multi-pass rewritten as an engine the model moves on each
      tick (see "Multi-pass" below), so the controller's
      `waitForAxisToStop()` and modal "press a key" between passes are gone.
      `callbackAtZeroDegrees()` is left, as the electronic leadscrew work
      will replace it
    - **Done:** dialogs don't block. They're window modal (`QDialog::open()`),
      and call back with the result when closed, so the controller's ticks
      (the model's checks, e.g. for a driver alarm, and the display) carry
      on while one is open. Motors are still stopped when a dialog opens.
      Gamepad keys are ignored meanwhile, as before; closing the window
      cancels the dialog and quits. Nothing in a tick processes Qt's events
      any more. Not yet tried on the machine
    - The one remaining block in a tick was `callbackAtZeroDegrees()`, now
      gone with the electronic leadscrew work, so nothing in a tick blocks
    - **Used on the machine (2026-11-07):** turning, dialogs, threading, and
      a 45 degree chamfer (so a taper's backlash take-up as a sequence) all
      worked. Not specifically tried yet: holding keys back during a nudge
      (and Space stopping one), and closing the window with a dialog open
- [x] Address the "Mode" concept, which provides pseudo-modal "dialogs"
      interleaved into program flow and is difficult to maintain. If all
      motors are stopped during a dialog, they can all go on a separate flow
    - **Done:** the dialogs are on a separate flow (above), and the mode is
      now one value in the model (`getMode()`), only set by `changeMode()`.
      That fixed quick chamfer (H), which set the mode without
      `changeMode()`, so didn't turn ramping off (X is synchronised to Z in
      a taper) or save X's speed to restore afterwards; and taper and radius
      modes left the previous mode's text on screen. Leaving threading
      resets Z's speed in `changeMode()`, however it's left (was
      `processModeInputKeys()`, now removed)

## Multi-pass (experimental)
- [ ] Try the rewritten multi-pass on the machine.
    - **Done:** rewritten as a generic engine (`MultiPass`, in multipass.h),
      replacing the old, broken code. It's a state machine moved on by the
      model's `checkStatus()`, and works through an `IMultiPassMachine`
      (the model; the unit tests use a fake). A plan is: cut from a start to
      an end on the cut axis (axis 1), then optionally retract, return at
      full speed, unretract, optionally pause (Enter continues), and feed
      on (axis 2) by a step towards its end position, until a cut has been
      made there. The last step may be smaller, to end exactly there. A
      feed of zero repeats the same cut until stopped. The feed can also be
      manual (it pauses each pass, saying how far to feed), for an axis
      without a motor, though no mode uses that yet
    - **Done:** multi-pass mode (`F2` `M`) uses it (originally from Mem 1 to
      Mem 2; now as set in its dialog, below). `Enter` starts it once at the
      start, and continues after a pause. While
      it runs, keys which move an axis are ignored; `Space` (or any stop,
      such as a fault, the spindle stopping or leaving the mode) abandons
      it. The display shows the pass, what it's doing, and how far X has
      stepped over
    - Cuts are at the Z speed when each cut starts (so it can be changed
      during the cut, for the next ones); the usual reset of speeds above
      `Axis1SpeedResetAbove` / `Axis2SpeedResetAbove` is skipped meanwhile
    - **Done:** a multi-pass dialog (`F2` `M`) replaces "Mem 1 to Mem 2". The
      start is where the axes are now, or a memory slot; each axis's end is
      separately a distance (Z, signed) or infeed (X, into the work, the
      opposite way to the retraction direction), or a memory slot (e.g. Z
      to a shoulder, X in by 0.5). With X set as a diameter, the end
      diameter can be typed instead of the infeed. Left/Right choose where
      each comes from; choosing the start as the slot an end was at swaps
      them (Mem 2 to Mem 1 becomes Mem 1 to Mem 2). It shows the positions
      these come to, and the number of passes, in larger text than the other
      dialogs. It starts at a memory slot if the tool is at one, ending at
      the other; the distances and end slots used last time are kept. The
      positions are fixed when it closes (`resolveMultiPass()`, in
      multipass.h), so memories aren't needed, or changed
    - **Done:** in the mode, Enter away from the start moves there (as a
      `MotionSequence`: retract X, Z at full speed, then X to the start; or,
      without the retract, X moves away from the work before Z, and towards
      it after), then waits; Enter again starts cutting. "Retract before
      moving to the start" is on each time the dialog opens. Space stops the
      move. Not yet tried on the machine
    - The first pass is at the start's X (as before), so starting from where
      the tool touched the work, it's a pass of nothing. Should the first
      pass be one step in?
    - The mill: its axis 2 (Y) has no motor yet, so a multi-pass there would
      need the engine's manual feed ("feed Y by n mm, then press Enter"),
      and the dialog and the move to the start would need to allow for it
- [ ] Work on a single line rather than a plane, i.e. if mem0.x == mem1.x,
      interpret it as (say) slot cutting and repeat until the user cancels.
      Could also work if only one axis is defined in config (the engine's
      manual feed, or a feed of zero, would do the sequencing). At the
      moment Mem 1 and Mem 2 with the same X give a single pass
- [x] Automation of a repeated cut: cut from Mem 1 to Mem 2, fast return,
      wait for a key press, and repeat until cancelled (to make repeated cuts
      down to depth easier)
    - **Done:** multi-pass with a step-over of zero and "pause". While it's
      paused, X can be nudged (W/S, not the Alt nudges, as releasing Alt
      stops everything), and the next pass cuts from there; with a
      step-over, the next pass steps over from there. X is now unretracted
      before the pause (at the start of the cut, clear of the work), so it
      shows the position cut at, and can be nudged
- [ ] The same for threading: at the moment the operator has to (a) move to
      a location, (b) fast return to a second location, (c) nudge in a
      little and repeat. This could be automated to simply (a) nudge and
      press a key, and it repeats the cut and return. Each pass starts at
      the same spindle angle now (the electronic leadscrew), so a threading
      multi-pass could use the multi-pass engine, with the threading pass as
      its cut

## Motion and timing (mostly in stepperControl)
- [ ] Deceleration ramping. At the moment there's only acceleration, so
      stops (especially from rapids) are abrupt. Needs tuning on the machine,
      and changes where moves end. Also, the ramp rate is per loop, so depends
      on the step rate
    - **Only for moves to a known target** (go to, memory returns, fast
      returns, multi-pass returns), planned to end exactly on the target.
      Stops the operator makes (releasing a rapid, Space, pressing a move's
      key again) must stay instant: a ramped stop there runs on after the key
      is released (speed² ÷ (2 × deceleration), e.g. about 0.5mm for X at
      1000 mm/min), which would overshoot when rapiding up to the work.
      Configurable per axis, off by default
    - **First, a physical test (to do), to see whether it's needed at all:**
      a dial indicator on the cross slide, reading X's movement. Compare it
      with the display after each of: nudges in and out; retract and
      unretract; a rapid away then a return to a memory slot; a fast return
      (X F); and a long move at full speed. Repeat each a few times, and
      approach the same point from both directions
        - The indicator always agrees with the display: X's positioning is
          fine, and the differences when sizing work come from cutting
          (deflection / spring). Ramping isn't worth doing
        - Out by a consistent amount after a change of direction: X's
          backlash compensation (`Axis2BacklashCompensationSteps`) is off;
          re-measure it in Setup mode (F2 S)
        - Wanders after fast or long moves: steps are being lost, so ramping
          (or a lower maximum X speed) is worth doing
    - Related idea, for sizing: after setting the measured diameter, type the
      target diameter and X moves straight to it
- [ ] Sleep between steps at slow speeds, rather than busy-waiting. Needs the
      wake-up latency measuring on the Pi first, as thread pitch depends on
      step timing. (Idle motors can sleep, but on the lathe they're set to
      spin, each pinned to its own CPU core: `Axis1CpuCore`, `Axis2CpuCore`,
      `MotorThreadsSpinWhenIdle`. Without that, a motor thread waking up
      starved pigpio's callback thread, losing encoder pulses)
- [ ] Consider a real-time priority below 99, which is shared with critical
      kernel threads. Test on the machine
- [ ] If a once-per-second stutter reappears, check real-time throttling
      (see the README)
- [x] **Electronic leadscrew: lock Z's *position* to the spindle, not its
      speed.** At the moment, threading sets Z's speed from the spindle's RPM,
      averaged over the last revolution and applied every UI loop (~50ms).
      Anything that makes Z's speed differ from the spindle's, even briefly,
      becomes a permanent error for the rest of the pass:
    - The spindle slowing under the cutting load gives pitch error until the
      average catches up
    - A misread RPM shifts the helix. Seen on 2026-11-06: a revolution
      averaged across a gap in the encoder pulses read low, Z ran slow and
      fell about 0.1s behind, and the pass cut a second thread
    - **Approach:** when the pass starts, note the encoder's pulse count.
      Z's target is then the start position + (pulses since the start ÷
      pulses per revolution) × pitch, and the motor thread steps towards it
      continually. The existing `synchronise()` in stepperControl already
      follows *another motor's* position in the same way; it could follow an
      encoder-based position source instead
    - **Starting:** Z can't jump to spindle speed. Either start locked from
      standstill and let Z catch up (the lag is the same every pass, so the
      passes still line up, as long as the spindle speed is similar), or
      lock on a little before the start of the thread. Z's maximum speed
      still limits how fast the spindle can turn (keep the existing check)
    - **Resolution:** 700 pulses per spindle revolution is 2.5µm of Z per
      pulse at 1.75mm pitch, against Z's 1µm steps. Interpolating between
      pulses by time (from the current pulse rate) would smooth this out
    - **Latency:** pigpio delivers encoder callbacks in batches about every
      millisecond. Z then lags by a near-constant time, which only gives a
      constant phase offset, harmless while the spindle speed is the same
      for every pass
    - This also replaces `callbackAtZeroDegrees()` and its busy-wait: a pass
      just starts locking on when the count reaches the next multiple of a
      revolution. Ties in with moving the controller onto Qt's event loop
    - Lost encoder pulses would still lose the position (they're now
      detected, logged and warned about)
    - **Testing:** with the mock encoder, change the spindle speed part way
      through a pass and check that Z's position still tracks pulses × pitch
    - **Done:** implemented as above. stepperControl v1.4.0 adds
      `StepperMotor::followTo()`: the motor thread steps towards a target
      function's result before each step period. The encoder keeps a pulse
      count which never wraps, and `getSpindlePosition()` carries it on past
      the last pulse at the current rate (up to 2ms), which covers both the
      resolution and the batching latency. A pass locks on at the next whole
      revolution of the count (at least an eighth of a revolution ahead), so
      `callbackAtZeroDegrees()` and its busy-wait are gone. Z's speed is now
      only a limit on how fast it can follow: twice what's needed at the
      start of the pass (up to Z's maximum). If Z falls more than a quarter
      of the pitch behind, the pass stops with a warning. Unit tests cover
      the spindle slowing to half speed mid-pass, every pass (both ways)
      starting at a whole revolution, and Z not keeping up
    - **Tested on the machine (2026-11-07):** an M14 coarse thread on 14mm
      bar, with an ISO form tool. Repeated passes all stayed in one groove,
      including after moving the carriage away (to try a commercial M14 nut)
      and back to the start. The finished thread fits the nut
    - Z sounds "stuttery" while following: it steps in short bursts at its
      speed limit (twice what's needed), then waits for the spindle to move
      the target on, rather than stepping evenly. Expected, and harmless
      (the thread is right); a lower limit, or setting the step rate from
      the target's speed, would smooth it if it's ever wanted
- [ ] I defined some constants for a slight Z infeed when cutting threads
      (see INFEED and SIDEFEED) but never used them. Maybe implement to
      simulate the use of the topslide
- [ ] A setting for adding a tiny amount of X motion to counteract a lathe
      that cuts a slight taper (taper mode works brilliantly, but this should
      be a config item applied automatically). Idea: calculate the angle
      required from two measurements and a distance
- [ ] Support the enable pin for each motor
- [ ] **Motor stalls: stop when the driver goes into its error mode.** At the
      moment, if a motor stalls and its driver goes into error mode, the
      driver ignores further step pulses but the program carries on counting
      them, so the position (and memories, and threading start points) end
      up completely out of sync.
    - **Done (detection):** `Axis1AlarmPin` / `Axis1AlarmActiveLevel` (and
      Axis2) in the config file. The motor thread watches the pin (ignoring
      spikes under 2ms), stops at once, and refuses to move; the model stops
      all axes, ends any mode (threading, taper etc.), and shows "Z MOTOR
      FAULT" until the axis is re-zeroed or its position set, which only
      succeeds once the driver's alarm has gone. Not yet tried on the machine
    - **Still to do:** wire the alarm and find its active level (below), then
      recovery without re-zeroing (ENA reset, and re-basing Z from the scale)
    - The Z driver is a Hilitand HSS57 hybrid servo (closed loop) driver.
      At present, once it alarms, the only way to recover is to power it off
      and on again
    - X has an ordinary (open-loop) stepper and no scale, so it can't detect
      a stall at all: it loses steps silently. For X, the options are to make
      stalls less likely (acceleration and deceleration ramping, a
      conservative maximum speed), re-zero or touch off when in doubt, or add
      feedback (a closed-loop driver like Z's, or a scale on X) so the same
      detection and recovery can be used
    - Wire the driver's alarm output to a spare GPIO. HSS57 drivers
      normally have ALM+ / ALM− terminals (an opto-isolated transistor
      output): ALM− to the Pi's GND, ALM+ to the GPIO with a pull-up to
      **3.3V** (the internal pull-up will do; never 5V, as the Pi's GPIOs
      aren't 5V tolerant). Check the driver's manual, as clones differ
    - Find whether the pin reads high or low in alarm (e.g. with a meter, or
      by logging it in normal running and in alarm), and set
      `Axis1AlarmActiveLevel` to match
    - Recovery: on Z, with the linear scale, re-base the motor's position
      from the scale once the fault is cleared (option 2 of "Linear scale"
      under Sensors), so there's no need to re-zero. On X, without a scale,
      treat the position as unknown until re-zeroed or touched off
    - Many drivers clear a position-error alarm when ENA is toggled
      (disable, then enable), though some faults need a power cycle. Test
      this on the HSS57: wire ENA like PUL and DIR, and see whether
      disabling and re-enabling clears the alarm LED. If so, the program can
      offer "reset Z driver" after a fault, then re-base Z from the scale, so
      a stall needs neither a power cycle nor re-zeroing. Ties in with
      "Support the enable pin for each motor"
    - The linear scale check (option 1 under Sensors) is a backstop: it
      catches stalls even without the alarm wired, and with ordinary
      open-loop drivers, which lose steps silently
    - Add a simulated alarm pin to the mock GPIO, to test all this without
      stalling the lathe
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
- [ ] **Linear scale: detecting lost steps, and absolute position.** The
      scale is read and displayed ("Z Scale"), but positions still come from
      "dead reckoning" (the motor's step count), which is wrong from the
      moment a step is lost. Should position come solely from the scale, or
      should the scale correct the expected position? Thoughts so far:
    - **Things that shape the answer** (figures from lc.cfg):
        - The scale (200 counts/mm, so 5µm) is *coarser* than the motor
          (1µm per step on Z). So the scale can't control the motor step by
          step; it can check and correct to within a count or so
        - The scale measures the *carriage*; the step count measures the
          *screw*. They legitimately differ by up to the backlash (380 steps,
          about 0.38mm, on Z) whenever the direction changes, until the slack
          is taken up. So any comparison must allow for backlash (only compare
          once the slack is taken up, or model it as the backlash compensation
          already does). Nice side effect: the scale could *measure* the
          backlash automatically, rather than it being found with a dial gauge
        - Scale readings arrive with latency (pigpio delivers callbacks in
          batches, roughly every millisecond), so they lag the motor while
          moving. Compare at rest, or allow a lag proportional to speed
        - The scale also captures leadscrew pitch error and thermal growth,
          which dead reckoning can't
    - **Keep the two coordinate systems separate.** The motor's steps
      (commanded) and the scale's counts (measured) should never be mixed.
      Keep an explicit offset between them, set when zeroing; the
      "discrepancy" is then (scale position − motor position − offset).
      Note the current code *does* mix them: with `Axis1UseLinearScale` on,
      `getAxis1MotorCurrentStep()` returns a step derived from the scale,
      which is passed to the motor's `goToStep()`. Once scale and motor drift
      apart, moves (e.g. a nudge) are out by the whole drift. **Fix this
      before enabling `Axis1UseLinearScale`**
    - **Options, roughly in order of difficulty:**
        1. *Detect only:* watch the discrepancy, and if it exceeds a
           threshold (a few scale counts beyond the backlash allowance), stop
           the axis and warn "lost steps". Simple, and catches the dangerous
           case. Could be done in the model's `checkStatus()`
        2. *Correct at rest:* when a move finishes (slack taken up), compare
           and either re-base the motor's position (`setPosition()`) so the
           display and memories reflect reality, or make a small corrective
           move. The latter is the "go close, then nudge while watching the
           scale" idea noted in `Model::axis1GoToPosition()`
        3. *Closed loop:* targets in scale units, with the motor continually
           steered by the scale. The most accurate, but hard: the scale is
           coarser and lags, and threading needs a smooth, steady speed rather
           than constant small corrections. Possibly worth it for positioning
           moves, but probably not for threading
    - **Decide which frame memories are stored in.** If in scale units, a
      "go to memory" becomes: move by dead reckoning, then correct (option 2)
    - **Causes of lost steps:** mainly the motor stalling under load, when
      the driver goes into error mode (see "Motor stalls" under Motion and
      timing, which covers using the driver's alarm output to detect it).
      Sudden starts and stops may also lose steps, which deceleration
      ramping would help with
    - **Option 2 is the preferred approach** (as it also gives a way to
      recover after a stall without re-zeroing)
    - A scale on X would need a mod to the sender unit
    - **Scale reliability:** a jump of two phases means an edge was missed,
      and its direction can't be known. Count these and report them, as a
      sign of interference or the scale being read too slowly
    - **Testing:** the mock GPIO already simulates the scale moving with the
      motor, so it could simulate lost steps too (motor steps that the scale
      doesn't see), to test detection and correction without the machine
- [ ] Probing, which would also be useful as a way to touch off
    - X probing: would need an insulated known-diameter bar held in the spindle
    - Z probing: would need an insulated known-length bar, which could be
      held on the end of a workpiece
    - Will need a dedicated pin on the Pi for monitoring
- [ ] Tool numbers with both Z and X offsets, for repeatable positioning
      after a tool swap? Not sure how realistic this is. Maybe some form of
      probing like on the CNC?

## Structure and refactoring
- [ ] Incorporate electronicRotaryTable repo
    - Pull functionality of ERT into this repo. ERT is designed specifically
      for cutting gears, so we could have a mode for gear cutting.
    - This would require a set-up screen to ensure the user has connected a
      rotary table, replacing one axis.
    - This would mean we could also incorporate the repo stepperControl as it
      is only shared by latheControl and ERT (which would then be redundant)
    - Gear cutting mode should be disabled by default, and only enabled by a
      setting in lc_mill.cfg (the milling machine currently only has a stepper
      on its X axis)
    - lc_rt.cfg is redundant and should be removed.
- [ ] Lots of refactoring still needed, as this grew a bit organically
- [ ] Replace all "Axis1..." and "Axis2..." references with an array, to
      allow for future expansion. Might be difficult owing to the axes'
      different behaviours
- [ ] Support three axes, to enable X, Y and a rotary table (or Z, X, and
      manual rotation of the chuck on the lathe)
    - axes should just be a collection of axis types
    - specific axes which require specialised behaviour can have attributes,
      perhaps (e.g. retraction). Determine which behaviour needs extracting
- [x] `axis.cpp` / `axis.h` are compiled but unused, and their `mgo::Axis`
      class clashes in name with `enum class mgo::Axis` in model.h
    - **Done:** removed (they're in git's history, if the "array of axes"
      idea below wants them)
- [x] Set the initial X-axis retraction direction from the config file
    - **Done:** `Axis2RetractionDirection` (outwards or inwards) in lc.cfg

## Miscellaneous
- [x] Add ability to add new thread pitches without recompilation. Move the
      definitions in threadpitches.h into separate config file (threadpitches.cfg)
      which is loaded at program start.
      - The thread pitches dialog should be multi-tier: the user
        initially selects, e.g. "metric coarse", "metric fine", "BSW", "BSF", "UNC"
        etc, then is presented with the relevant pitches for that category.
      - The dialog could present a diagram showing the salient thread data
      - The dialog should allow the user to select male or female (the diagram should
        change to reflect this)
      - Expand the new config file with all common thread pitch data up to, say, 25mm/1"
      - **Done:** threadpitches.cfg, read at start-up (`ThreadsFile` in lc.cfg),
        with metric coarse and fine, BSW, BSF, BSP, UNC, UNF and BA up to 25mm / 1".
        Each line is name, major diameter, pitch (mm or inches; mm or tpi); the
        thread form (ISO, UN, Whitworth, BA) gives the depths and the bore. The
        dialog has two levels (type, then size), M for male / female, the figures,
        and a diagram of the profile. Choosing female sets X to retract inwards.
        Used on the machine (2026-11-07) for an external M14 thread; a female
        thread hasn't been tried yet
      - To fit a commercial M14 nut, a few passes beyond the 0.613 x pitch depth
        were needed: commercial bolts are made a little under nominal (about
        13.8 - 13.97mm for M14), and the bar was turned to 14.00. Treat the depth
        as where to start trying the nut
      - NB internal metric/UN threads now use the ISO depth of 0.541 x pitch; the old
        table had 0.469 x pitch (and M18 had 1.072 where M20 had 1.172). Change
        femaleDepth in threadpitches.cpp's FORMS if 0.469 was deliberate
- [ ] Add some form of generic multi-pass functionality (defining a start position,
      a required end position, and how axes should change per pass -- e.g. for a lathe
      operation, we'd define a z start, a z end, and an x infeed per pass).
      To allow the user to "visually" set parameters, it could offer the ability to read
      stored memory slots, for example for threading a user might define z start and z end
      by lining the tool up to where they want the thread to end and storing the z value
      in a memory slot, then moving z out to just past the end of the workpiece and storing
      that z value in a separate memory slot.
      - **Done:** the engine (`MultiPass`, see "Multi-pass" above): a plan of cut start
        and end, feed start and end, feed per pass, retract, pause, and manual feed.
        Multi-pass mode builds one from Mem 1 and Mem 2. New modes (e.g. threading,
        or facing on the mill) need a plan built from their own inputs, and the
        mode's display
- [ ] It should not require the user to reset an axis value
      to keep track of (for example) the current thread depth being cut - instead maybe it
      displays a box showing how much the X axis has changed since the first "scratch pass"
      was taken in the current mode. The axis being displayed should be configurable.
      - **Partly done:** multi-pass mode shows how far X has stepped over since the
        first pass (and of how far in all). Not configurable yet, and not in other modes
- [ ] Once this "generic multi-pass" functionality is available, it could be used for:
        - threading on the lathe
        - turning down to a workpiece to a set diameter (lathe)
        - radiusing (lathe)
        - chamfering (lathe)
        - facing an entire workpiece (mill) i.e. the X axis does a pass, and each pass
          the user is prompted to feed the Y (manual) axis a set amount. When the process reaches
          the target Y value, the cutter could return to the beginning, and the user might be
          prompted to feed down on the Z (again, currently manual on the mill) and repeat.
        - using the rotary table (mill) to turn a set amount, then take a cut along the x axis, 
          and repeat. The user could be prompted to feed the Y axis a set amount per full
          revolution of the C axis. This would allow, for example, a hexagonal shape to be cut.
        - etc
       It should also have functionality to allow calling functions to:
        - Pause between each pass (with a message, e.g. "move y n units" - the mill, for
          example, doesn't have a stepper on Y or Z currently so would require manual moves)
        - Or perform each pass automatically (e.g. on the lathe where both Z and X have
          steppers)
        It should also have an attribute to determine whether a retract is required
        between each pass. For example on the lathe in threading mode the sequence would be:
            - cut from z start to z end
            - retract (could be retract out, for male threads, or in for female)
            - return to z start
            - unretract
            - feed in/out x by the specified change per pass
            - optional pause message
            - repeat until x delta equals thread pitch cut depth
- [ ] Investigate how to interface with the mill's linear scales
    - The milling machine has three linear scales (X,Y,Z), all of which interface with TouchDRO
      (an Android app which displays DRO data on a separate tablet) via a bluetooth connection.
      We need to determine how to get this data into latheControl too for linear scale functionality mentioned elsewhere in this document.