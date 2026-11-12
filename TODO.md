# TODO

## User interface

### Make the interface less cryptic
- [ ] A modification to recent changes:
        Keep Tab key as the menu key, but also use a tap on the meta key to do the 
        same thing.
        - F1 should be re-purposed to a proper "help" button which could bring up
          a small pop-up dialog giving help in certain places. Not intended as a
          full instruction manual, just some aides-memoire (e.g. setting up a
          feature).
- [x] A key that brings up all the options available *now*
    - **Done:** `Tab` opens a menu of every command, with its shortcut, built
      from one table of commands (commands.cpp) so it can't drift out of date.
      Commands not available at the moment (e.g. in Setup mode, or on a
      locked axis) are greyed out. It never blocks: motors keep running,
      Space still stops everything, and rapids and their dead-man stops still
      work. Choosing a command acts exactly as its shortcut does. Rapids are
      deliberately not in the menu, as they rely on holding the keys down
    - **Done:** open the menu from the gamepad with a button chord
      (`JoystickMenuChord`, default 4,6: previous + next memory slot).
      Tried on the machine: works well
    - **Done:** hotkey letters for the categories, Modes, System and
      axis locks, highlighted in yellow and underlined. Not given to the
      many paired or small commands, which have quick shortcuts already
    - **Done:** half a second after a leader key (`Z`, `X`, `\`) is
      pressed, a panel shows the keys which can follow and what they do,
      from the same command table
    - **Done:** F1 opens the menu (which shows every shortcut) rather than a
      separate, hand-written help page
    - While the menu is open, memory slots 5 and 6 are hidden behind it
      (this is fine, I think, although maybe if that ever was a problem
      a dedicated button could perhaps move the menu to the left/right)
- [x] It must be usable without a mouse, which is awkward at the machine.
      Everything works from the keyboard, and the menu from the gamepad too.
      The gamepad isn't read while a dialog is open; low priority, as it sits
      next to the keyboard and is for the basics of machine control
- [ ] Proper menus and dialogs, now that Qt makes them possible. This replaces
      the old "implement a proper menu/dialog (see level_designer)" and
      "move all input to the new modal dialog system" items
- [ ] Richer dialogs, with several inputs, like the taper dialog (presets,
      angle, direction, and a diagram). The other dialogs are fine for now
- [ ] Make the display more "modular": display elements in definable boxes,
      perhaps a class which encapsulates a display element. This will make the
      layout easier to change
- [x] **Drop the `F2` leader key.** In practice the menu (e.g. `Tab` `M` `T`
      for threading) had taken over from `F2`, which was a second route to
      the same commands
    - **Done:** `F2` (and `:`) no longer lead to anything, so the modes
      (threading, taper, radius, multi-pass, retraction direction, setup) are
      chosen from the menu's Modes, and show no shortcut there. `F2` `H`
      (help) and `F2` `Q` / `:q` (quit) went too: F1 and Ctrl+Q do the same.
      The `f2*` key codes stay as the internal command IDs the menu
      dispatches; later, perhaps rename them to something like
      `cmd::threading`, so they don't suggest a key that no longer exists
    - Frees the top-level letters, e.g. `T` for "choose tool" (see "Current
      tool and a tool database")
- [x] **Turn `ThreadingAutoRetract` on and off from the menu** (2026-11-11).
      Testing threading with the cross-slide locked by hand meant editing
      lc.cfg between tests, as auto-retract is only read from the config.
      A menu-only toggle (no shortcut, as it's rarely changed), perhaps
      under Modes or Threading, starting from the config's value at
      start-up and not saved back. Show its state in the menu text (e.g.
      "Auto-retract after a threading pass: on"), and somewhere on screen
      in threading mode, so it's not forgotten. (With X locked in software,
      the auto-retract is now refused with "Unlock X first", so it does no
      harm, but turning it off avoids the warning)
    - **Done** (2026-11-11), in the mock build: Modes, "Auto-retract is ON
      (threading)", or OFF (hotkey `A`, no shortcut). "Auto-retract after a
      threading pass: on" didn't fit the menu, and both it and "Turn
      auto-retract off" could be read as the state or as what choosing it
      does; "is ON" can only be the state. `A` (or Right) toggles it and
      leaves the menu open, so its new wording shows the state; `Enter`
      closes the menu, leaving it as it is. It can be
      changed at any time, even during threading passes (which don't use
      it) or a limit switch stop, as it moves nothing. The threading
      display's third line says "X retracts outwards; auto-retract on". Not
      yet tried on the machine
- [ ] Make use of the keys on the right (#, Home, PgUp, PgDn, End)?
        - Home could be used to return to the current feature's zEnd/diameter?
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
- [x] Try the rewritten multi-pass on the machine.
    - **Done:** tried on aluminium, without pausing: it worked, finishing
      within a few hundredths of a mm of the target diameter (the machine's
      repeatability). Two things came of it:
    - **Done:** Enter during a pass (when not pausing anyway) pauses once
      it's done, e.g. to clear stringy swarf wrapping around the chuck;
      Enter again cancels the request. Used on the machine (2026-11-09) and
      works as expected
    - **Done:** the main window takes the focus back when a dialog closes.
      Sometimes after the multi-pass dialog (and another), Pi OS's panel
      appeared and keys were lost until the window was clicked. No focus
      problems seen on the machine since (2026-11-09)
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
    - **Done:** multi-pass mode (`Tab` `M` `M`) uses it (originally from Mem 1 to
      Mem 2; now as set in its dialog, below). `Enter` starts it once at the
      start, and continues after a pause. While
      it runs, keys which move an axis are ignored; `Space` (or any stop,
      such as a fault, the spindle stopping or leaving the mode) abandons
      it. The display shows the pass, what it's doing, and how far X has
      stepped over
    - Cuts are at the Z speed when each cut starts (so it can be changed
      during the cut, for the next ones); the usual reset of speeds above
      `Axis1SpeedResetAbove` / `Axis2SpeedResetAbove` is skipped meanwhile
    - **Done:** a multi-pass dialog (`Tab` `M` `M`) replaces "Mem 1 to Mem 2". The
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
      move. Used on the machine (2026-11-09) and works well
    - **Done:** the first pass is one step in from the start's X (it was at
      the start, so starting from where the tool touched the work, it was a
      pass of nothing). So the passes are the infeed / step-over, rounded
      up. With a step-over of zero, it's still at the start; with a manual
      feed, it pauses first for the user to feed in. Used on the machine
      (2026-11-09)
    - The mill: its axis 2 (Y) has no motor yet, so a multi-pass there would
      need the engine's manual feed ("feed Y by n mm, then press Enter"),
      and the dialog and the move to the start would need to allow for it
- [x] Retract only on the last passes (suggested 2026-11-10): the roughing
      passes needn't retract, as a faint spiral left by the fast return
      doesn't matter; only the finishing ones do. That's quicker, and still
      leaves a good finish
    - **Done:** the dialog's `R` cycles the retract through No, Every pass
      and Last 2 passes (2 because the last pass may be a tiny one; set by
      `MultiPassRetractLastPasses`). The engine's plan has
      `retractLastPasses` (0 for every pass), and works out for each pass
      whether it's one of the last ones, so a nudge while paused that
      saves a pass is allowed for. With a step-over of zero there's no last
      pass, so every pass retracts. Ordinary multi-pass only: threading
      passes always retract. Tested in the unit and UI tests; not yet tried
      on the machine
- [x] Retract by a safe amount, not an arbitrary one (noticed 2026-11-10,
      testing a female thread in debug mode): X retracted a fixed 2 mm
      (hard-coded). In a bore only just big enough for the boring bar, an
      inward retract that size would move the back of the bar into the far
      side.
    - **Done (1):** the fixed distance is `Axis2RetractionDistance` in the
      config (2 mm unless set), for `R` and `ThreadingAutoRetract`.
    - **Done (2):** a multi-pass, threading passes and spring passes
      retract to the start (where the tool touched the work) plus
      `MultiPassRetractClearance` (0.25 mm unless set), and so does the
      retract before moving to their start. That's safe by construction:
      the start is clear of all the work however deep the passes go (for a
      thread, the crests are there), and in a bore, the back of the bar is
      only the clearance nearer the far side than when you touched off. It
      always moves at least the clearance, e.g. if X is already further out
      than the start. It's usually less movement than 2 mm, too (a thread is
      seldom that deep), though deep turning passes now retract further.
      **Tried on the machine (2026-11-10):** an internal thread, 30 mm long
      in a bore, with the threading passes. The retracts worked as
      intended. The thread itself didn't come out well, but for another
      reason: with about 40 mm of stick-out on the carbide internal
      threading tool, the carriage visibly shifted with each cut (see the
      rigidity item below).
    - Whether 0.25 mm clears the burr thrown up on an internal thread's
      crests is still open, as the test cut wasn't good enough to tell. If
      a later one rubs on the return, raise `MultiPassRetractClearance`, or
      give threading its own.
    - (3) Not yet: `R` and `ThreadingAutoRetract` could use the current
      feature's surface the same way when it's set (e.g. X set as a
      diameter on the bore), falling back to the fixed distance otherwise.
      The program can't know the boring bar's width, but retracting only
      just clear of the surface is the least that can hit anything.
- [ ] Internal threading and the carriage's rigidity (2026-11-10): with
      about 40 mm of stick-out (for a 30 mm long thread), the leverage
      visibly shifted the carriage on each cut. For now, internal threading
      probably isn't a good idea on this lathe.
      **Found (2026-11-11):** it's the whole saddle rocking on the bed, not
      the cross-slide. A DTI on the saddle's rear face showed about 0.2 mm
      of movement from pushing by hand. The saddle's gibs can't be
      tightened further: any tighter and Z's stepper stalls on fast moves
      and when threading. So this is a limit of the hardware, parked for
      now. Operations with a lot of leverage (long stick-out, internal
      threads) need very sharp tools and as little stick-out as possible.
      Things to try:
    - A shorter thread, so less stick-out, with very fine cuts.
    - A sharp HSS tool instead of the carbide insert, which needs less force.
    - Longer term: more powerful steppers, so the gibs can be tightened
      without stalling them. For the saddle that means Z (the rocking
      found above); X's gib is loose for the same reason. (A loose gib
      was also suspected in the M8 male thread's many spring passes,
      2026-11-09: the saddle's rocking may have been the cause there too.)
    - [ ] Cut the far wall of the bore with the spindle in reverse
      (2026-11-11). The idea: the saddle seems to rock more when the
      infeed is towards +X. An ordinary internal thread is cut on the near
      wall with a rake-up tool, feeding in towards +X, so the radial
      cutting force pushes the tool towards the back. A right-hand thread
      can also be cut on the far wall with a rake-up tool that points to
      the back (a mirror-image tool), with the spindle reversed and Z
      feeding away from the chuck (+Z). Handedness depends only on the
      spindle direction and the Z feed direction, so this is still a
      right-hand thread. The infeed is then towards −X, and the radial
      force pushes the tool towards the operator, so the saddle is loaded
      in the same directions as for external threading, which has worked
      well. (Turning an ordinary bar upside down to reach the far wall,
      with the spindle forward, also works, but then the cutting force
      pushes the tool up and tries to lift the saddle, so that's not worth
      trying.) Reverse is fine on this lathe: the chuck is bolted to a
      flange, and the VFD gives the same power both ways.
      **First:** with a DTI on the saddle, push the tool tip by hand
      towards +X and then towards −X with about the same force. Only go
      further if pushing towards +X (as in this setup) moves the saddle
      clearly less. If both are about the same, the rocking more likely
      comes from the bar's stick-out in Z, and this won't help.
      **Then:** grind a mirror-image internal threading tool in HSS, with
      the side clearance on the side that now leads into the cut (+Z), and
      try a cut. Things to check in the program, none of which have been
      checked yet: the multi-pass engine running passes towards +Z, −X
      infeed (and +X retract) for an internal thread, the index sync with
      the spindle in reverse, and reversing the flank infeed's Z offset.
      With a blind bore each pass starts at the bottom and runs out at the
      mouth, so the relief groove has to be long enough for Z to
      accelerate in.
    - [ ] ~~Lock the cross-slide physically during each cut~~ **Parked
      (2026-11-11):** the DTI test showed it's the saddle rocking, which an
      X lock wouldn't help (and Z can't be locked, as it moves during the
      cut). Kept for reference, in case a stiffer saddle ever makes the
      cross-slide the weak point. The original idea: lock it as you would
      by hand. Thought to be the cross-slide that moved, not the
      saddle: its gib was loosened so the small X stepper wouldn't stall,
      and the flex shows most with the tool stuck out far from the slide's
      centre. **First:** check with a dial indicator on the bed which part
      moves (cross-slide against saddle, or saddle against bed), then take
      a cut with X locked by hand, unlocking it at each pause, to see if
      the thread improves. Only build this if it does. The design agreed:
      - A multi-pass dialog option, `L` (free in the dialogs; `L` outside
        them is Last Position, which is why the axis lock is `K`). Every
        pass, not "last N": every internal threading pass flexed.
      - The software lock stands for the physical one. Starting with the
        option on locks X in software. Before any X move (feed in, retract,
        unretract, the move to the start, spring passes) the engine waits,
        showing "Unlock X", until X is unlocked with `XK`; before each cut
        it waits, showing "Lock X", for `Enter` (or `XK`). So per pass:
        cut, unlock, retract, return, unretract, feed in, lock, cut.
      - Why `XK`, not `Enter`, to confirm the unlock: forgetting to lock
        only costs rigidity, but forgetting to unlock stalls X's open-loop
        stepper against the lock without anything noticing, so X loses its
        position and every later pass is wrong. `Enter` gets pressed out of
        habit during a multi-pass; `XK` is deliberate.
      - While locked, the existing lock already refuses the X keys and
        nudges. The pause option adds nothing, as it pauses anyway.
      - In any case, the model should refuse to move X automatically while
        it's locked in software. At the moment only the controller checks
        locks (for keys), so a multi-pass, its move to the start and spring
        passes all move a locked X. A multi-pass started with X locked and
        the option off should say "Unlock X first".
        **Done (2026-11-11), not yet tried on the machine:** the model now
        refuses every move of a locked axis, whatever asks (keys, rapids,
        memory and fast returns, go to previous, repeat last move,
        retracts including ThreadingAutoRetract, the feature go-tos, and
        multi-pass and threading passes, starting or continuing), showing
        "Unlock X first". The controller's own checks went, so a refused
        key now says why instead of doing nothing. In a taper or radius,
        Z is refused while X is locked, as X follows it. An axis can't be
        locked while it's moving or a multi-pass is cutting (it can while
        paused), so the engine never finds its axis locked mid-move; that's
        also why it needed no new waiting states for this.
      - In the engine: two waiting states, and a `feedLocked()` query on
        `IMultiPassMachine`, which fit the non-blocking `update()` loop and
        can be tested with the fake machine.
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
    - **Next (suggested 2026-11-08):** the pieces are in place. The engine
      already does retract, return, unretract, pause and step-in; the
      electronic leadscrew (tested with the M14 thread) starts every pass
      at the same spindle angle; and the threads table knows the depth
      (0.613 × pitch for a male ISO thread), so the infeed can be filled in.
      The work:
        1. **Done:** the engine's cut is a plain feed or a threading pass
           (`MultiPassCut` in the plan; `IMultiPassMachine::cutThreadTo()`).
           Threading needs a retract (the return isn't locked to the
           spindle), and no manual feed or taper correction. In the model,
           a threading pass is as by hand (Z's backlash taken up, then
           locked to the spindle); the return is at full speed, with Z's
           ramping on for it only; ThreadingAutoRetract is left alone; and a
           thread doesn't update the current feature's Ø
        2. **Done:** threading passes run in threading mode (decided: the
           thread, the spindle speed warnings and Z's settings are already
           there). `Tab` `M` `N` opens the multi-pass dialog in a threading
           form: the thread in the title, the infeed starting at the thread's
           depth when the thread has changed, "infeed per pass", and the
           retract always on. Then Enter moves to the start and runs them, as
           in multi-pass mode; the display shows the thread, then the passes.
           Choosing a thread again, or leaving the mode, clears them, so Enter
           goes back to the memory slot. Z ramps for the move to the start
           and the returns (full speed), and not for the passes. Tested with
           the mock encoder (each pass starts at a whole revolution). Not yet
           tried on the machine
           - **Fixed:** tried in a FAKE build (M8, 0.05 per pass), it
             stopped on the last pass with "the feed stopped short". M8's
             depth (0.7668 mm) is 1840.3 of X's 1/2400 mm steps, so X stops
             0.3 of a step from it, more than the engine allowed. The engine
             now counts an axis as there within half a step
             (`IMultiPassMachine::cutStepSize()` / `feedStepSize()`). It
             could also have happened in multi-pass mode, e.g. with a typed
             end diameter
           - **Tried on the machine (2026-11-09):** an M8 male thread. The
             stock was turned down with a multi-pass to a commercial M8
             bolt's OD (7.82 mm) and chamfered in taper mode. The thread ran
             from Mem 2 (just off the end) to Mem 1, at 0.1 mm per pass. A
             lovely-looking thread. It needed about eight spring passes
             (done by hand in threading mode) before a commercial nut went
             on, each taking off a noticeable amount more: possibly the
             cross-slide's gib being a little loose (it can't be much
             tighter, or it stalls X's stepper), or the tool and work being
             pushed away with both flanks cutting at once (a plunge infeed).
             Then a beautifully fitting thread
        3. Carry on past the nominal depth, pass by pass. The M14 and M8
           threads needed several extra passes before the nut fitted. The M8
           ones were done by leaving the threading passes, which a key in
           the passes should save (see below)
           - **Done:** once the passes have finished, Enter takes a spring
             pass at the same depth, then pauses: X can be nudged in first
             (W/S and the fine Shift nudges; not Alt, as releasing it stops
             everything), and Enter takes another, until Space. (As
             multi-pass's step-over of zero with pausing, started from where
             X is.) If the axes aren't at the start of the cut (e.g. stopped
             part way, or retracted), Enter moves back there first, to the
             last spring pass's depth. Starting the whole set again is from
             the dialog. Tested with the mock encoder; not yet tried on the
             machine
           - **Done:** moving the tool away to try the nut on. X shallower
             than the last pass (moved out with the keys rather than
             retracted, even while paused) goes back to the last depth
             before the next pass, rather than cutting air from there. X
             deeper (nudged in) is taken as meant
           - **Done:** a safety stop if the encoder loses pulses during
             the passes (seen as a false alarm in the FAKE build, from the
             Mac throttling the app in the background, which showed the
             passes carried on regardless). Every later pass would cut a
             slightly different thread, so they stop once clear of the work:
             a cut is finished (stopping part way would leave the tool
             cutting a ring), X retracts and Z returns, and X is left
             retracted. Enter won't start passes or spring passes again on
             that thread; choosing the thread again clears it
           - **Done:** Z can move (Left/Right) while paused between spring
             passes, e.g. off the end to try the nut on; Enter moves back
             to the last pass's start first. (Tried in the FAKE build: the
             arrows did nothing while paused, which wasn't expected.) Not
             between the threading passes themselves, nor in multi-pass mode
           - **Watch for (2026-11-10):** on the machine, Right while paused
             between spring passes moved Z a little and stopped, twice,
             then moved on the third press. Not seen again when cutting air,
             nor in the FAKE build. Probably the spindle was switched off
             (or coasting) ready to try the nut on: in threading mode, Left
             and Right are threading moves, with Z following the spindle,
             so Z slows down with it, and stops once it reads stopped. If it's seen
             again, the lc.log lines (each threading move's start and end,
             and why it stopped) will say. Possibly better anyway: plain Z
             moves (not locked to the spindle) while paused, which would
             also work with the spindle stopped. The next pass still lines
             up, as each starts on a whole revolution
        4. **Done:** two options in the threading passes dialog, both on
           the first time (then as last time). Suggested after the M8
           thread's spring passes each took off a noticeable amount, which
           may have been the tool and work being pushed away with both
           flanks cutting. Tested with the mock encoder; not yet tried on
           the machine
           - **Constant area** (`A`): each pass removes about the same area,
             as CNC threading cycles do, so the passes get shallower as the
             tool goes deeper, which is gentler on the tool. What's typed is
             the first pass's infeed; pass n goes to that times the square
             root of n, but at least `ThreadingMinimumInfeed` (0.02 mm)
             further than the pass before
           - **Compound infeed** (`C`): as a topslide set over, each pass
             starts along Z by its depth times tan(half the thread's angle
             less half a degree): 29.5 degrees for 60 degree threads. Decided:
             the leading flank cuts (the start moves the way the cut goes,
             as is usual), so the start must be at least that far off the
             work (the dialog says how far: about 0.57 of the depth, e.g.
             0.43 mm for M8). The cut's end stays put, so it never moves
             towards a shoulder. Z moves to each pass's start at full speed
             after X feeds in. The pass is still locked to the spindle, so
             the shifted start just moves the groove along. Spring passes
             start where the last pass did, with no further shift (nudges
             are straight in). This replaced the unused INFEED / SIDEFEED
             constants
           - **Tried on the machine (2026-11-10):** an M14 male thread,
             on stock turned to 13.8 mm with a multi-pass, both options on.
             The passes were noticeably lighter on the machine, and only the
             leading flank cut, as expected. A great-looking thread: a
             couple of spring passes cleaned it up, then after a test fit,
             a nudge in and a couple more before the nut went on. The
             flanks were clean enough not to need a needle file, which
             they always used to

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
          re-measure it in Setup mode (`Tab` `M` `S`)
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
- [x] I defined some constants for a slight Z infeed when cutting threads
      (see INFEED and SIDEFEED) but never used them. Maybe implement to
      simulate the use of the topslide
    - **Done:** as the threading passes' compound infeed (see Multi-pass);
      the constants have gone
- [ ] A setting for adding a tiny amount of X motion to counteract a lathe
      that cuts a slight taper (taper mode works brilliantly, but this should
      be a config item applied automatically). Idea: calculate the angle
      required from two measurements and a distance
    - Tested (Nov 2026), a 23.4 mm multi-pass with two spring passes, short
      stick-out, measured at the far end (A), middle (B) and chuck end (C):
      unsupported, A 15.315, B 15.298, C 15.277; with a dead centre,
      A 14.727, B 14.700, C 14.685. So about 0.04 mm smaller on diameter
      at the chuck end over the cut (about 0.05 degrees), the same with or
      without the centre, and the second spring pass removed nothing: not
      deflection, but the machine's geometry. The lathe (an old Hobbymat
      MD65) has no obvious way to adjust it out
    - An earlier experiment with a tiny angle in taper mode turned a bar of
      about 150 mm to the same diameter along its length, so correcting it
      in software works
    - **Done (stage 1, experimental):** `TaperCorrection` in the config (an
      angle in degrees, as for taper mode; see lc.cfg for working it out).
      It's added to taper mode's angle, and each multi-pass cut is made as
      a taper of that angle (X following Z as in taper mode, backlash taken
      up first), following back on the return, or, if retracted,
      unretracting to where the cut started. X (and the diameter) is shown
      as if it weren't moving. If X lags Z a little as it starts following,
      it finishes the return's move, so nothing builds up from pass to
      pass
    - Tried on the machine (Nov 2026). It works, and the sign is as in
      lc.cfg (-0.045 makes the work wider towards the chuck), though the
      effect measured was only about 60% of what's expected: worth
      checking X's movement with a dial indicator (e.g. one held in the
      toolpost against a bar held still in the chuck, running the same Z
      travel with the correction at 0 and then on: the difference is the
      correction). But it turns out not to be needed:
        - A 135 mm aluminium bar with a live centre was 0.30 *larger* at
          the chuck end, curved rather than straight: the tailstock
          pushing the bar off line
        - A 40 mm steel bar, 100 mm long, unsupported, spring passed until
          nothing more came off: A 39.54, B 39.57, C 39.55. No overall
          taper, but a slight bulge in the middle: the carriage's path
          along the bed isn't quite straight (bed wear?). The first 23 mm
          test, near the chuck, measured the slope of this curve there
      So TaperCorrection is best left at 0. It's left in, as an experimental
      option, in case a straight taper ever needs correcting
    - (Stage 2, X following Z everywhere, isn't needed: see the "bump map"
      instead)
- [ ] A "bump map" mode, for a finishing cut where a piece must be as
      accurate as possible: correcting a lathe whose cut isn't straight
      along its length (here, a slight bulge, about 0.02-0.03 mm on
      diameter, from the bed, not a simple taper; see above)
    - After a cut along the work, it asks for the diameter measured every
      10 mm or so (ideally after spring passes, so it's the tool's path,
      not deflection), and uses those for the finishing cut: X follows Z
      through a table of offsets, interpolated between the readings, rather
      than at a fixed angle as TaperCorrection does
    - Could be done per piece (measure, then finish), or saved as the
      machine's map if it proves to be the same from piece to piece (check
      that first: the tailstock, stick-out and material all change it)
    - The multi-pass's X following (TaperCorrection's machinery) is the
      starting point: a function of Z's position in place of a fixed ratio,
      with X shown corrected, and backlash taken up the way X starts moving.
      A map whose slope changes sign along the cut reverses X part way: the
      backlash compensation would have to cope with that, or the cut be
      made in sections
    - The corrections are about the size of the machine's repeatability,
      so only worth it for the odd piece which needs it
- [ ] A "profile" mode: an electronic version of a mechanical follower, for
      turning simple profiles (e.g. a chess piece) from a file. Idea only,
      for discussion, but it shares its machinery with the bump map above:
      X follows a function of Z's position, from a table of points
    - Radius mode already does this for a circle (`synchroniseXForRadius()`
      works out X from Z's absolute position). A profile swaps Pythagoras
      for a lookup in the table. Since X depends only on where Z is, Z can
      be wound back and forth, stopped and restarted anywhere, and the tool
      retraces the profile, just as a mechanical follower would. That suits
      the app as an aid to manual machining: the operator drives Z (keys,
      gamepad, or later a multi-pass cut) and steps the depth in between
      passes as an X offset
    - **Input:** a simple file of `Z, Ø` points, interpolated linearly, in
      the style of threadpitches.cfg, rather than SVG (paths, Béziers,
      transforms and units are a lot to parse in the app). A separate
      script in the repo could turn an SVG path (e.g. from Inkscape) into
      that file: flatten the curves into close points, allow for the tool's
      nose radius (offsetting along the surface's normal, so the file holds
      the tool tip's path), and warn about the steepest slope and any
      undercuts
    - **Motor thread:** precompute X for every Z step when the profile is
      loaded, so the synchronisation callback is just an array index (no
      searching, allocating or locks on the real-time path)
    - **Backlash:** the hardest part. A chess piece's profile reverses X
      many times, and X is open-loop with no scale, so every reversal would
      leave a step of about the backlash in the surface. Either the
      synchronisation adds the backlash as X changes direction (in the
      motor thread, so carefully), or the cut is made in sections, each
      moving X in one direction only. The same problem as the bump map's
    - **Steep slopes:** X has to move at Z's speed times the slope, so on a
      steep rise it lags and the profile comes out wrong. A vertical face
      isn't a function of Z at all. Options: cap Z's speed from the
      steepest slope in the profile (simple, but slow); make a vertical step
      a separate X move with Z waiting; or leave square shoulders to manual
      work. A mechanical follower's stylus angle has the same limit
    - **The tool:** a profile both rising and falling needs a tool that cuts
      both ways (a round nose or a pointed insert), and a falling slope can
      be no steeper than the tool's trailing clearance allows. The operator
      has to judge what will work with the tool in use; the app can only
      warn about the obvious cases
    - **Roughing:** from bar, passes at `X = min(stock Ø, profile +
      allowance)` with the allowance shrinking each pass would fit the
      multi-pass engine, if its X following (`feedFollows`) became a
      function of Z rather than a fixed ratio. Offsetting in X only leaves
      thinner material on steep parts, the safe way to be wrong
    - **Suggested first slice:** profile mode beside taper and radius: load
      a points file, zero at the start as radius mode asks, drive Z by hand
      with X following, and step the depth by hand. Test it on a profile
      whose X only moves one way (e.g. a cone with a radiused end), so
      backlash doesn't come into it yet. Lathe only, and off unless the
      config enables it
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
      and when triggered simply issue a "stop all"
    - **Done (2026-11-11), not yet tried on the machine:** `LimitSwitchPin`
      and `LimitSwitchActiveLevel` (off by default). The pin is watched in
      each motor's thread (stepperControl's `setLimitPin()`, alongside the
      alarm pin, with the same 2ms filter), not the 50ms run loop, so the
      motors stop within a couple of milliseconds, and still would if the
      UI stalled. That's one more pin read per step period, only when
      configured. Every motor is given the same pin, so any switch stops
      them all; the model then stops anything running (e.g. a multi-pass).
    - Once tripped, nothing moves until the stop is cleared (a dialog opens
      for it, or the menu: System), and only keys which don't move anything
      work. Chosen over "let moves carry on while it's pressed" so that
      repeated nudges into the switch can't go unnoticed. After clearing, a
      switch still pressed doesn't trip again until released, so the axis
      can be moved off it; with one pin, the program can't tell which way
      that is.
    - Active high by default, for normally-closed switches in series to
      GND: a broken wire or unplugged socket also stops the motors.
    - Needs stepperControl's next release (with `setLimitPin()`).
    - **To test** on the machine, along with the enable and alarm pins:
      that a switch stops the motors at rapid speed, the dialog, and moving
      off the switch after clearing.
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
## Work geometry and tools (lathe)
Background: "set diameter" currently means only that **X is measured from the
spindle centreline** (`m_xDiameterSet`). The diameter shown is the *tool's*
position (2 × |X|), so it changes whenever X moves. Knowing the centreline is
the valuable part. Once we have it, remembering where the *work* is costs very
little: we store numbers in axis coordinates, which moving the axes doesn't
change.

- [ ] **Current feature: a remembered diameter and end Z.**
    - **Done (first slice):** `CurrentFeature` in the config turns it on.
      The feature Ø (stored as X's position on its surface) and the work's
      end Z, each set and cleared separately: set diameter (`x s`, D) also
      records the Ø, `x d` records it again, `z e` marks the work's end, and
      the menu's Feature category has these and Clear. Cleared as below
      (re-zero, a fault, a position set unless adjusting, X no longer set as
      a diameter); there's no tool change to clear it on yet. A "Current
      feature" box (right of the mode's lines) shows them. The multi-pass
      dialog has a Feature start (the default when set, unless at a memory
      slot): X on the surface, Z `FeatureStartClearance` (0.5 mm) off the
      work's end, away from the cut, with Z's distance from the work's end.
      A multi-pass which cuts from the work's end sets the Ø to its end
      diameter. Used on the machine (2026-11-09) and works well
    - **Decided:** end Z is the work's *free* end (as faced), not any
      feature's end. A Z clearance beyond a shoulder part way along would
      put the tool into the larger diameter beyond it, and the program can't
      know the profile. So a Feature start assumes the Ø runs from the
      work's end; features part way along wait for "Single feature vs many".
      For the same reason, a multi-pass only updates the Ø if its cut
      started at (or off) the work's end
    - **Done:** a re-zero or a position set which would clear any of the
      feature asks first (an OK / Cancel dialog, listing what it would
      clear). Not set as a diameter, as that replaces the Ø rather than
      losing it, nor faults (nothing to ask) or the menu's Clear
    - **Done:** going back to the feature, from the menu's Feature category
      (e.g. after moving away without storing its start in a memory slot):
      to its start (both axes, X retracting before Z moves, as a multi-pass
      moves to its start: the shared `Model::moveToSafely()`), X alone to
      the Ø, or Z alone to the work's end (refused if X is deeper than the
      surface). Z's target is the work's end exactly, or clear of it by
      `FeatureStartClearance`, away from the chuck (`Axis1TowardsChuck`).
      Used on the machine (2026-11-09) and works well
    - [ ] **The feature's other end.** Save where the feature ends along Z
      (e.g. a shoulder), as well as the work's end: set from a key, or by a
      multi-pass from the work's end (its cut's end). Then it could be gone
      to as well, offered as a multi-pass's Z end, and shown in the box. A
      step towards "Single feature vs many" (a list of segments)
    - Still to do from below: the warning when the tool goes inside the Ø
      with the spindle running, tools (and clearing on a tool change), and
      chamfers / radii from the feature Temporary: kept
      for this session only, never saved across a restart. Its purpose is to
      help the operator while they make one particular feature.
    - **Values (each optional, set separately):**
        - *Feature Ø*: the diameter of the surface being worked on. Once set,
          it stays fixed however X is moved
        - *End Z*: the Z position of the feature's end (e.g. after facing)
        - Perhaps later: the original stock Ø as a second value, so it can be
          kept after a shoulder has been turned (see "Single feature vs many")
    - **Setting them:**
        - "Set diameter" could do both jobs at once: set the centreline datum
          *and* record the feature Ø, since the tool is on a surface the
          user has just measured at that moment
        - A "mark end" action after facing records the current Z as end Z
        - Automations update the values themselves when they know the result,
          e.g. a multi-pass "turn to Ø" sets the feature Ø to the target when
          it finishes
    - **Clearing them:**
        - The user can clear the feature at any time (e.g. to move on to a
          shoulder further along the work)
        - Re-zeroing X clears the feature Ø, as it already clears the
          centreline datum. Re-zeroing Z clears end Z
        - A motor fault or lost position on an axis clears that axis's value.
          This matters most for X, which is open-loop, so the remembered
          surface is only as good as X not having lost steps
        - A tool change clears both, unless the tools' offsets are known (see
          "Tool database" below)
    - **Keeping it up to date:** if the spindle is running and the tool moves
      inside the remembered Ø, *warn* that the feature Ø may be out of date.
      Don't try to work out the new value from tool movement, as that's easy
      to get subtly wrong
    - **Display:** a "Current feature" box on screen showing Ø, end Z and the
      tool they were taken with, kept separate from the live tool Ø so the
      two can't be confused. Empty or hidden when no feature is set
    - **Single feature vs many:** start with one "current feature" that the
      user resets. A list of features (`{Z start, Z end, Ø}` segments) amounts
      to a part profile. Appealing, but much more work in the UI and in
      keeping it consistent
    - **Which side of end Z is material?** Usually towards the chuck, but this
      should be explicit (from the config, or worked out from the facing
      direction) so a chamfer or radius never cuts air or goes the wrong way
    - **Lathe only:** only enable when the config has an X axis that can be
      set as a diameter. Keep it optional like other per-axis features
    - **Uses:**
        - Pre-fill multi-pass start positions (Ø and Z) from the feature. A
          cheap, low-risk first use
        - Chamfer "on the end of the piece": from Ø, end Z and a size, the
          program knows where every pass goes. A 45° chamfer is a linear X/Z
          move, which taper mode's X-to-Z synchronisation can already do
        - Radius/fillet (see "radiusing" under generic multi-pass): start near
          Ø/2, step inwards, finish at a given end X (> 0 for a fillet). Arcs
          aren't a fixed X:Z ratio, so this needs either a chain of short
          tapers (`MotionSequence` could run them) or proper arc
          synchronisation. Tool nose radius starts to matter here too
    - **Suggested first slice:** feature Ø and end Z with set and clear, their
      own display box, cleared on re-zero, fault or tool change, and used to
      pre-fill multi-pass

- [ ] **Current tool and a tool database.** The program has no idea when the
      tool is changed, but the feature values above belong to the tool that
      touched off. A different tool will be out by its offset.
    - **Current tool:** the user picks the tool in use (number and
      description), and it's shown prominently on screen. Changing tool
      must be very easy: a single keypress, not a trip through menus. `T`
      might do (free at the top level now `F2` has gone). Selecting a
      different tool counts as a tool change, so the current feature is
      cleared (or shifted, once offsets are known, below)
    - **Tool database:** a config file of predefined tools. Must be quick to
      pick from without a mouse. Possible attributes:
        - Number and description
        - Type (turning, facing, chamfering, parting, grooving, threading,
          boring, button, knurling...)
        - **Tip orientation:** which corner or side of the tool is its
          reference point, like CNC lathes' "tip direction" codes (Fanuc's
          1-9). This one attribute covers left-hand and right-hand turning
          tools, boring bars facing the other way in X, and which side of the
          nose radius a button tool is measured from
        - **Nose radius:** worth storing even before it's used, as radius and
          chamfer accuracy will eventually depend on it (e.g. a button tool,
          whose "point" is a circle of maybe 4 mm diameter)
        - **Point angle**, e.g. 60° threading tool, 90° chamfering tool (see
          "cut knurling")
        - **Width and reference edge**, for parting and grooving tools: a
          thickness rather than a radius, plus which edge is the reference.
          Then a groove or cut-off can be placed relative to the feature's
          end Z from either side
        - **Not applicable:** some tools can't usefully have offsets (e.g. a
          knurling tool, or anything in the tailstock such as a centre drill
          or reamer, which isn't on the carriage at all). Operations that use
          the feature Ø and end Z won't be allowed with these tools
        - X and Z offsets, when each was last measured, and whether it has
          been invalidated (see below)
    - **Tool offsets (later):** if each tool's X and Z offsets relative to a
      reference tool are known, a tool change can shift the datums instead
      of clearing them, so the feature Ø and end Z stay valid across a swap.
        - **Reference tool:** tool 1, the most-used tool (a right-hand turning
          tool). All other offsets are measured relative to it
        - **Calibrating with a dial indicator** (the planned first method),
          once for Z and again for X:
            1. With tool 1, approach a dial indicator fixed to the bed (e.g. on
               a magnetic base) and press its plunger in about half-way. Zero
               the indicator; the program records tool 1's position
            2. Change to the new tool and nudge it up to within about 50 µm of
               the indicator's zero
            3. The program takes up backlash in the approach direction (X is
               open-loop, so this matters most there), then either:
                - feeds extremely slowly and the user presses a key when the
                  needle reaches zero (at, say, 2 µm/s, a 0.2 s reaction time
                  costs only about 0.4 µm), or
                - steps 1 µm per keypress until the needle reads zero, which
                  avoids reaction time completely, as the reading is taken
                  with nothing moving
            4. The difference from tool 1's position is stored as the offset
        - **Notes on calibrating:**
            - Every tool must approach the indicator from the same direction
              as tool 1 did, so backlash doesn't creep in
            - The indicator mustn't move between tool 1's reading and the
              other tool's reading
            - Some tools' points are hard to get an indicator square on, e.g.
              a threading tool's small point, which faces along X. The
              indicator can be set at an angle. Cosine error doesn't matter,
              because we're only matching tool 1's position, not measuring a
              distance
        - **Other ways to measure offsets:** touching each tool off on the
          same known surface (a turned diameter for X, a faced end for Z);
          probing (see "Probing" above) could automate this later
    - **Repeatability and invalidation:** offsets are only as good as the
      toolpost's repeatability. A QCTP (always used) should be reasonably
      good, but things can change:
        - **The QCTP is moved or rotated on the carriage** (e.g. to fit a
          longer tool): moving it without rotating would shift every tool
          equally, but rotating it wouldn't, so the user needs a "toolpost
          moved" action that invalidates *every* tool's offset (and the
          current feature)
        - **A single tool is adjusted in its holder** (e.g. a boring bar
          extended for reach): the user marks that tool's offset invalid
        - **Tool 1 itself is adjusted:** every other offset becomes invalid
          at once, so this needs handling as a separate case
        - An invalidated tool can't be used for operations that need the
          feature Ø and end Z until it's calibrated against tool 1 again.
          The UI shows when each tool's offset was last measured and whether
          it's been invalidated

## Structure and refactoring
- [ ] Incorporate electronicRotaryTable repo
    - Pull functionality of ERT into this repo. ERT is designed specifically
      for cutting gears, so we could have a mode for gear cutting.
    - The old ERT was a text/ncurses-based UI so will need updating to Qt.
    - This would require a set-up screen to ensure the user has connected a
      rotary table, replacing one axis.
    - This would mean we could also incorporate the repo stepperControl as it
      is only shared by latheControl and ERT (which would then be redundant)
    - Gear cutting mode should be disabled by default, and only enabled by a
      setting in lc_mill.cfg (the milling machine currently only has a stepper
      on its X axis)
    - lc_rt.cfg is redundant and should be removed.
    - As part of this change, maybe the config file should have an "axis[n]Capabilities"
      config item, to allow the program to determine which features are available for
      an axis (e.g. retract, or, in the case of a rotary table, gear cutting and so on.
- [x] **Unit tests which fail now and then** (2026-11-11). On neutron
      (Linux, 8 cores, a busy KDE desktop), 1 to 3 tests failed on every run
      of `unit_test`, mostly "threading passes stop if a pass can't start"
      (waiting 5 s for the encoder's speed to pass 1,000 rpm), and sometimes
      "spring passes after the threading passes" and "stopping a multi-pass".
      Unchanged main failed the same way. They pass on m4pro (macOS).
    - **Why:** Linux's timing, in two ways. A sleep overshoots by its timer
      slack (50 us by default): a 50 us sleep took 114 us on average, and
      300 us took 397 us. And on a busy desktop a thread waking from a
      sleep can wait about 3 ms (a scheduler time slice) before it runs.
      The mock encoder slept between pulses and gave each pulse the time it
      was sent as its tick, so its spindle ran up to half as fast as asked
      (never reaching 1,000 rpm), and a late wake-up looked like a gap in
      the pulses, i.e. lost pulses, which stopped threading passes
    - **Fixed** in stepperControl's `MockGpio` (needs a release, v1.5.1):
      each pulse is due a delay after the one before was due, and is given
      that time as its tick, however late the thread sends it, as pigpio's
      callbacks pass each edge's sampled time. A late pulse is followed at
      once by any others due, so the average speed is the one asked for.
      "Stopping a multi-pass" checked that X was still moving just after
      starting a 0.01 mm feed, which a few ms' delay could miss: X is now
      slow enough there for the feed to take 0.6 s (and X goes back to the
      start with Z afterwards, as it's now stopped part way in)
    - (Separately, the debug build on neutron had been configured with
      FAKE=OFF, and the Makefile doesn't reconfigure an existing tree, so
      `make test` wouldn't build until it was reconfigured by hand)
- [ ] Lots of refactoring still needed, as this grew a bit organically
- [ ] (undecided) Replace all "Axis1..." and "Axis2..." references with an array, to
      allow for future expansion. Might be difficult owing to the axes'
      different behaviours
- [ ] Support three axes, to enable X, Y and a rotary table (or Z, X, and
      manual rotation of the chuck on the lathe)
- [x] `axis.cpp` / `axis.h` are compiled but unused, and their `mgo::Axis`
      class clashes in name with `enum class mgo::Axis` in model.h
    - **Done:** removed (they're in git's history, if the "array of axes"
      idea below wants them)
- [x] Set the initial X-axis retraction direction from the config file
    - **Done:** `Axis2RetractionDirection` (outwards or inwards) in lc.cfg

## Miscellaneous
- [ ] **New mode: "cut knurling".** A cut (rather than pressed) knurl, made
      with a single-point tool as two sets of multi-start "threads" of
      opposite hand.
    - **First set:** a series of thread-like cuts at evenly spaced starting
      angles, giving several helical grooves along the piece. The helix
      angle (the grooves' angle to the axis, on the work's surface) defaults
      to 45°
    - **Second set, opposite hand:** cut with the *same tool*, spindle still
      running forwards, but with Z moving the other way (outwards from the
      chuck, towards the tailstock). The electronic leadscrew makes this
      easy. No tool change means no offsets to worry about, and both sets
      line up with each other. These cuts start at the chuck end, so that
      end needs a relief groove or runout to start in
    - **Tool:** a 90° point (e.g. an HSS chamfering tool) gives the usual
      90° knurl form; a 60° threading tool would also work, giving a slightly
      different profile
    - **Lead depends on diameter:** for a helix angle θ, the lead (Z travel
      per spindle revolution, per start) is π × Ø / tan θ, which for 45° is
      π × Ø. On a 10 mm bar that's about 31 mm per revolution, so Z has to
      move very fast for the spindle speed. The spindle will need to run
      very slowly to stay within Z's speed and acceleration limits; the mode
      should work out and show the maximum spindle speed allowed. The Ø
      comes straight from the current feature (see "Work geometry and
      tools"), or the user enters it
    - **Number of starts:** sets the diamond size (diamond pitch about
      π × Ø ÷ N). The mode could ask for a target pitch (fine, medium,
      coarse) and choose N to suit the diameter
    - **Starting angles:** each start is the previous one plus 360° ÷ N of
      spindle rotation, using the encoder, like a multi-start thread. Any
      multi-start threading support would share this
    - **Passes:** each start probably needs several passes in X to reach full
      depth, so this fits the generic multi-pass engine: for each pass
      depth, cut every start (or every start to full depth in turn)
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
- [x] Start a fresh log each session: at start-up, before `INIT_MGOLOG("lc.log")`
      in main.cpp, rename any existing lc.log to lc.log.previous (replacing an
      older one), so there's always this session's log and the last one's. The
      log grows without limit now that threading passes, retracts, spindle
      readings and lost pulses are all logged, and the session in question
      after a workshop test is hard to find in it. (`std::filesystem::rename`;
      ignore a failure, e.g. no log yet, rather than refusing to start)
    - **Done:** as described. Not yet tried on the Pi
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
            - Maybe radius should rely on diameter being set and the the work faced off;
              if a diameter is set after turning down bar stock to a known diameter, then facing
              the end, the program would know where (a) the centre line of the stock is, and
              (b) that the tool is at the end of the piece (z current value), then doing a
              radius operation from that point doesn't need to rely on the operator for anything
              else. The radiusing operation could move X to near the diameter of the stock, take
              a small radius cut, and repeat, gradually moving z in/down until the cut's start
              position (in x) reaches zero. (That's assuming a full ball end is required -
              will need to specify x end point so if the user requires just a "fillet" on the
              stock's end they could specify an end x value of greater than zero).
                - This prompts an interesting idea - maybe diameter, once set, should be
                  a constant value, i.e. the current diameter of the stock, and moving x in or out
                  manually shouldn't change that. This might be useful for the program to
                  remember; although it would of course go out of date if the user made further
                  manual turning down operations (e.g. a shoulder - what would the diameter
                  refer to? Maybe the current "feature"? To consider further.)
                  **See "Work geometry and tools" for this worked out further.**
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
    - The sender's sketch and schematic are now in `hardware/touchdro-sender/`
      (see its README). The Arduino reads the iGaging scales and writes plain
      text from its serial port at 9600 baud, about 24 times a second:
      `X1234;Y-567;Z89;`, in raw counts (about 100.8 per mm, so about 10 µm),
      smoothed by the sketch
    - What the box turned out to be, on opening it (10 Nov 2026): the scales'
      data lines go to D5, D6 and D7, which matches the sketch, not the
      schematic. The board has no USB port and is powered on its RAW pin from
      a mains adapter through a jack on the box, so it's almost certainly a
      Pro Mini (or a clone), not a Nano. It must have been programmed through
      an FTDI adapter. So reading it "over the Nano's USB port", the earlier
      plan, isn't possible
    - **Decided approach: listen to the Arduino's TX line with a USB-serial
      adapter.** TX already goes to the HC-06; it can drive a second input as
      well. Connect only the adapter's RX to the Arduino's TX, and GND to GND,
      ideally through the Pro Mini's 6-pin FTDI header if it's fitted (TXO and
      GND). The Pi then gets `/dev/ttyUSB0` carrying exactly what TouchDRO
      gets. No firmware change, and TouchDRO carries on as before.
      **Tested (10 Nov 2026)** with the old FTDI adapter (3.3 V) on the
      workshop computer: a steady stream such as `X250383;Y-553984;Z131;`,
      with TouchDRO still connected and working. The large values are just
      the scales' arbitrary counter offsets; zeroing deals with them. (For
      that test the adapter was plugged onto the whole header and powered
      the box; the permanent connection shouldn't do that.)
      `hardware/touchdro-sender/watch_scales.py` prints a line only when a
      reading changes, naming the axis, which makes the stream readable.
      Notes, and things still to check:
        - Don't connect the adapter's VCC (the box has its own supply) or DTR
          (DTR resets the Arduino whenever the port is opened)
        - Logic levels: the crystal is marked "08", which suggests the
          3.3 V/8 MHz Pro Mini, though the schematic's dividers suggest 5 V.
          Confirm with a multimeter between VCC and GND with the box on. At
          3.3 V, an adapter set to 3.3 V is right and the Pi is safe either way
        - The adapter only listens, so it can't upset the HC-06
        - The box stays on its own adapter. Everything at the mill draws power
          from one mains socket, switched by a Zigbee button, so it all comes
          on at once anyway
        - Which pin is the clock. The sketch says D9; the schematic says D2
        - Axes and signs, checked with `watch_scales.py` (10 Nov 2026): each
          letter is the axis it should be, so the sketch matches and the
          box's socket labels (which suggested X and Y were swapped) are
          probably wrong. Counts go up when the table moves right (X),
          the table moves in towards the column (Y), and the head comes down
          (Z). That's the opposite of the usual machine convention (tool
          relative to the work) on all three. latheControl will need a sign
          per scale anyway, to match its own direction for each axis
    - Rejected:
        - Listening to the Bluetooth link: the HC-06 uses classic Bluetooth
          serial, which is one-to-one and frequency-hopping, so it can't be
          passively sniffed. The Pi could connect to it and relay to the
          tablet, but that's fragile and gains nothing over a wire
        - Wiring TX straight to the Pi's own UART (GPIO 15): possible, with a
          divider for 5 V, but on the Pi 3B the good UART belongs to its
          Bluetooth, and a USB adapter is less fuss
        - Changing the sketch to send a second copy: only needed if the
          stream lacks something (e.g. unsmoothed values or a checksum)
        - Reading the scales directly from the Pi: only one device can drive
          the scales' clock, and bit-banging the 21-bit protocol would load
          a Pi whose cores are reserved for the motor threads
    - What it's good for: at about 40 ms between smoothed readings it can't
      close the loop on motion, but it can check and correct when stopped:
      lost steps on the open-loop mill axes, re-basing after a stall (as for
      the lathe's Z scale above), and measuring the real backlash (mill X has
      300 steps of compensation configured)
    - Possible design: a serial reader thread, not real-time and not on the
      motor cores, parsing the tokens into atomics, with a reading treated as
      stale after about 0.5 s with no update. Config keys per axis, off by
      default: serial device, scale letter, counts per mm and sign. A common
      "scale position" interface shared with the quadrature `LinearScale`,
      so the lost-step and re-basing features work with either. A fake
      source for FAKE builds and the tests