# TODO

Two parts. **Outstanding** is what's still to do, one or two lines each,
with a link (→) to its notes where there's history. **Notes** is the
design log: what was tried on the machines, what was decided and why.
When an item is done it leaves the list, and what was done goes in its
notes.

## Outstanding

### To try on the machine

#### Either machine
- An axis locked in software refuses every move, saying "Unlock X first".
  → [Internal threading and saddle rigidity](#internal-threading-and-saddle-rigidity)
- Keys held back during a nudge (and Space stopping one); closing the
  window with a dialog open. → [Controller on the event loop](#controller-on-the-event-loop)
- At start-up each axis moves by its backlash, and the first move after
  starting is right. → [Using the mill scales](#using-the-mill-scales)

#### Lathe
- The Z scale is solid with the spindle running (glitch filter, 23 Nov
  2026); check its 200 counts per mm and settle time, before turning on
  its re-base and checks. → [The lathe Z scale](#the-lathe-z-scale)
- The auto-retract toggle in the menu. → [Auto-retract from the menu](#auto-retract-from-the-menu)
- Retracting only on the last 2 passes. → [Retracting only on the last passes](#retracting-only-on-the-last-passes)
- A female thread from the thread dialog; and whether 0.25 mm of retract
  clearance clears an internal thread's burr.
  → [Thread pitches file](#thread-pitches-file), [Retracting by a safe amount](#retracting-by-a-safe-amount)
- A taper's X backlash take-up direction, with a dial indicator (it may
  explain `TaperCorrection`'s shortfall). → [Radius mode](#radius-mode)
- Radius mode's full ball end. → [Radius mode](#radius-mode)
- A filleted shoulder with a nose radius typed in (the last pass's move up
  the face). → [Filleted shoulder](#filleted-shoulder)
- Knurling: the spindle direction taken over a tenth of a second, and
  starts cut in whichever order comes round first. → [Cut knurling](#cut-knurling)
- The unknown tool (`T?`) and changing back to a tool in the library.
  → [Tool library](#tool-library)

#### Mill
- New Mode: Bolt Hole. Give option for using A axis (in which case it's a simple
  rotation of the rotary table per bolt hole) or Y axis, in which a series of
  coordinates should be calculated. The rest of the mode would consist of a go to
  next hole / previous hole with an optional manual go-to depth on the Z.
- `XB` (re-base by hand), at rest and with a multi-pass paused.
  → [Re-basing an axis by hand](#re-basing-an-axis-by-hand)
- Re-basing between passes in a run without pauses; the "waiting for X's
  scale" box. → [Multi-pass drift on the mill](#multi-pass-drift-on-the-mill)
- After a multi-pass finishes, X isn't re-based at rest until the mode is
  left: check that's acceptable. → [Multi-pass drift on the mill](#multi-pass-drift-on-the-mill)
- The 0.35 mm approach: no more "went past its target" warnings, and
  whether the dwell before a real cut's end leaves a mark.
  → [An array of axes](#an-array-of-axes)
- `W` turns A the same way as Up. → [An array of axes](#an-array-of-axes)
- Memory slots on Y and Z, and going to them by hand; halving (`YH`)
  since the leader-key fix; typing a slot's value in (`YI`, `ZI`).
  → [An array of axes](#an-array-of-axes)
- `Esc` asks before leaving a mode, and a multi-pass carries on while
  it asks. → [An array of axes](#an-array-of-axes)
- `YG` and `ZG` (go to a position, wound by hand), and the go-to line's
  "fine feed CW" on Z.
  → [An array of axes](#an-array-of-axes)
- Gear cutting: the first tooth lands with the others (A's backlash and
  the 1° back-off), indexing at full speed with the blank on, and whether
  dragging the cutter back marks the slot.
  → [Gear cutting and stepperControl](#gear-cutting-and-steppercontrol)
- The scale's checks while moving: if they keep holding at 3 mm, bring
  both limits down (to 2, say). → [X stalls near the ends of the table](#x-stalls-near-the-ends-of-the-table)
- Keep watching for lock-ups on the Pi 4 over longer runs (a serial
  console on GPIO 14/15 if one comes back). → [Using the mill scales](#using-the-mill-scales)
- Creep at rest (2 counts every few minutes): real, or the scale? Watch
  with a dial gauge. → [The lathe Z scale](#the-lathe-z-scale)
- Optional: survey the lead screw's error along the table (a slow move end
  to end, the scale's difference every 10 mm). → [An array of axes](#an-array-of-axes)

### User interface
- Tab only for menu: F1 becomes a small help
  pop-up for where you are (aides-memoire, not the manual).
- Go to the feature's end (e.g. `ZGE`), once the feature has an end along
  Z (see Work geometry below).
- Proper menus and dialogs now Qt allows them; richer dialogs with several
  inputs, like the taper dialog.
- A more modular display: elements in definable boxes, so the layout is
  easier to change.
- Use the keys on the right (#, Home, PgUp, PgDn, End)? Home could go back
  to the feature's end Z or diameter.
- With a larger screen: dialogs that scale with the main display.

### Multi-pass
- **Slot and face/pocket modes for the mill**, then turn the generic
  multi-pass off in lc_mill.cfg. Needs stage 3 of the array of axes (the
  multi-pass dialog using the manual axes' slots).
  → [Slot and face or pocket modes](#slot-and-face-or-pocket-modes)
- Repeat a cut along a single line (Mem 1 and Mem 2 with the same X give
  one pass now), e.g. for a slot, until cancelled.
- `R` and `ThreadingAutoRetract` retract just clear of the feature's
  surface when one is set. → [Retracting by a safe amount](#retracting-by-a-safe-amount)
- Internal threads: cut the far wall with the spindle in reverse (DTI push
  test on the saddle first). → [Internal threading and saddle rigidity](#internal-threading-and-saddle-rigidity)
- Plain Z moves (not locked to the spindle) while paused between spring
  passes. → [Threading passes](#threading-passes)
- Multi-start threads in the threading passes dialog (the engine can do
  them). → [Cut knurling](#cut-knurling)
- Radii on the inside (a bore's mouth). → [Radius mode](#radius-mode)
- On the mill, a Y feed per turn of A (e.g. a hexagon), as gear cutting
  does for the depth. → [Generic multi-pass origins](#generic-multi-pass-origins)
- Gear cutting: step back a tooth to re-cut it; a pause between teeth as a
  dialog option. → [Gear cutting and stepperControl](#gear-cutting-and-steppercontrol)
- Show how far X has moved since the first pass in other modes too, and
  make the axis shown configurable (multi-pass mode does it already).

### Motion and timing
- X stalls near the mill table's ends: adjust the gibs; if that doesn't
  cure it, a lower rapid speed for X.
  → [X stalls near the ends of the table](#x-stalls-near-the-ends-of-the-table)
- Rapids and returns use the maximum rpm as mm/min: fix it, trying it on
  the mill first (A would go five times faster).
  → [Rapid speed units](#rapid-speed-units)
- **Ask before the start-up backlash take-up**, with a take-up direction
  per axis in the config. → [Start-up backlash take-up](#start-up-backlash-take-up)
- Drive the motors' enable pins; an empty setting means no pin, so a
  config left at `0` can't drive GPIO0. → [Enable pins](#enable-pins)
- Driver alarms: wire the HSS57's ALM, find its active level, recover by
  toggling ENA then re-basing from the scale; a simulated alarm pin in the
  mock GPIO. → [Driver alarms](#driver-alarms)
- Limit switches: wire them on the mill (GPIO2) and test at rapid speed,
  the dialog, and moving off a switch. → [Limit switches](#limit-switches)
- Something to keep the carriage within set points (switches, or position
  checks in software); a warning when the chuck runs in reverse (now only
  the `n:` label turns red).
- Sleep between steps at slow speeds rather than busy-waiting (measure the
  Pi's wake-up latency first; idle motors must keep spinning on their
  pinned cores).
- A real-time priority below 99 (shared with critical kernel threads):
  test on the machine. If a once-per-second stutter comes back, check
  real-time throttling (see the README).
- Low priority: a ramped start for knurling passes, so Z can follow a
  longer lead. → [Cut knurling](#cut-knurling)

### Sensors and scales
- A scale on the lathe's X (waiting for the 5 V to 3.3 V boards and for Z's
  scale to be proven); the code is ready. → [An array of axes](#an-array-of-axes)
- The finishing moves' "the scale's reading didn't settle" (it flickers a
  count). → [The lathe Z scale](#the-lathe-z-scale)
- Show the position as unconfirmed while a scale has no reading.
  → [Using the mill scales](#using-the-mill-scales)
- Two quadrature scales share `Gpio`'s one set of scale pins, so the first
  scale destroyed clears the other's callbacks and leaves its own (the
  mock only simulates axis 1's). Needed before the lathe's X scale.
  → [The lathe Z scale](#the-lathe-z-scale)
- The backlash measurement logs the scale's raw count and says where on
  the table it measured (for a survey). → [Using the mill scales](#using-the-mill-scales)
- Full quadrature decoding for the spindle encoder (only A's rising edges
  are counted now; four times the pulses affects the rpm and angle sums).
- Zeroing the chuck's angle (an offset in what's shown).
- Probing and edge finding: an insulated block or bar of known size on a
  pin, the tool to GND, and a creep until it touches. Also a way to touch
  off. Keep the Pi's GND off the machine's frame (an isolated sense
  circuit). → [Probing and edge finding](#probing-and-edge-finding)
- (Low: the rpm read erratic past about 1200 rpm, not seen with the mocks;
  not relevant since the spindle was geared down.)

### Work geometry and tools (lathe)
- The feature's other end along Z (e.g. a shoulder): set by a key or a
  multi-pass, then gone to, offered as a multi-pass end, and shown.
  → [Current feature](#current-feature)
- Warn when the tool goes inside the feature's Ø with the spindle running.
  → [Current feature](#current-feature)
- Chamfers from the feature. → [Current feature](#current-feature)
- Later: several features (a list of segments, a part profile).
  → [Current feature](#current-feature)
- Tool library: tip orientation, tool types, widths for parting and
  grooving, invalidating offsets (toolpost moved, a tool adjusted),
  deleting a tool, boring bars; the nose radius correcting tapers and
  chamfers. → [Tool library](#tool-library)
- Knurling: take the Ø from the feature rather than X; allow for a tool
  other than 90° in the starts suggested. → [Cut knurling](#cut-knurling)

### Structure and housekeeping
- Try the latest Pi OS with libgpiod instead of pigpio, on a spare SD card,
  the mill first. → [libgpiod and a newer Pi OS](#libgpiod-and-a-newer-pi-os)
- General refactoring: much of it grew organically.
- Check `femaleDepth` in threadpitches.cpp: the old table used 0.469 ×
  pitch, now ISO's 0.541. → [Thread pitches file](#thread-pitches-file)
- Fit the lathe's Pi with an active heatsink (the mill's has one).

### Ideas, not yet planned
- A "bump map" for an accurate finishing cut, from measured diameters along
  the work. → [Bump map](#bump-map)
- A "profile" mode: X follows a table of points as Z moves, like a
  mechanical follower. → [Profile mode](#profile-mode)
- (Very low: reflash the mill's DRO sender for lc alone if TouchDRO goes,
  with its smoothing off and a faster rate, to shorten the settle; back it up
  first.) → [Interfacing with the mill scales](#interfacing-with-the-mill-scales)

## Notes

### User interface

#### Calculator in numeric entries

- The idea (21 Nov 2026, reaching for a calculator at the lathe):
      any numeric entry takes a sum typed after `=`, worked out when Enter
      is pressed. See README.md. Decided with the user:
    - Only `+ - * /`, brackets and a leading minus. No trig: the bolt hole
      mode covers what trig would mostly be for.
    - Only `*` multiplies, not `x`, as `x` would clash with memory slot
      names (`m1x`).
    - Memory slots are always written in full, `m1x` (the slot and the
      axis). A shorter `m1` meaning the entry's own axis was considered,
      and left out to keep it simple.
    - `p` is the entry's value before the `=` was typed ("previous").
    - `pi`, and `in` / `mm` suffixes, converted to the entry's units. They
      apply to the product so far, so `=3/8in` is three-eighths of an inch.
      They're refused in entries which aren't lengths, so `in` can't
      silently mean 25.4 in e.g. an angle.
    - The first Enter puts the value in, and the second accepts it, so the
      number is seen before it moves anything (accuracy over speed). The
      value is shown live meanwhile, and a value the entry's own pattern
      wouldn't accept (e.g. negative) is refused with a reason.
    - No spaces, so Space can't end up typed into a sum.
    - lc.log records each sum and its value.
- Tried on the mill (22 Nov 2026), and used for real there.
- In the go-to and position set dialogs, `p` was 0 (their default
      entry), so it wasn't much use there. It's now where the axis is, as
      shown (the user agreed, 21 Nov 2026). Not in the relative go-to (an
      offset), nor the memory value entry. A manual axis without a reading
      has no `p`, rather than `p` silently being 0.
- `"` is inches, as `in` is (21 Nov 2026).

#### Re-basing an axis by hand

- A command to line an axis's position up with its scale at any time
      (the idea, 18 Nov 2026), rather than only when the mismatch
      check stops everything and its dialog offers it, or when a move
      re-bases at rest. The user suggested `x` then `s`, but on the mill
      that's "Set X position..." (`x` is axis 1's leader there). The letters
      free after an axis leader include `b` (re-base) and `y` (sync).
      Wait for the scale to settle first (or refuse while it's still
      changing, or while the axis is moving), log the difference it took
      up as the other re-bases do, and add it to the command table, so it's
      in the menu (Position, or System beside "Finishing on the scale").
      Only for axes with a scale
    - **Done (19 Nov 2026), not yet tried on the mill:** axis 1's leader
      then `b` (the choice; `XB` on the mill), and Position in the
      menu, only with `Axis1Scale` set. Axis 1 is the only axis that can
      have a scale for its motor; a manual axis's position is its scale
      already. Allowed when the re-bases at rest are (no multi-pass, motion
      sequence, finishing move, backlash measurement or lathe mode), with
      the axis stopped and the reading steady; otherwise it warns why.
      Logged as "re-based by hand". **Not yet while a multi-pass is
      paused**, though that's where it would help most (see the drift under
      Multi-pass): the engine's start and end positions are in the motor's
      frame, so re-basing between passes should correct the next pass, but
      how the engine copes with the cut axis not being where it left it
      wants checking first
    - **Done (19 Nov 2026), not yet tried on the mill:** allowed while a
      multi-pass or gear cutting is paused too. The engine copes as hoped:
      on continuing, a cut axis that isn't at the pass's start is moved
      there first (at full speed, as it's clear of the work), and the cut
      still ends at the cut's end (unit tested). See "Re-basing between
      passes" under Multi-pass
    - **On the mill (19 Nov 2026):** not needed in a two or three hour
      session: the automatic re-bases (at rest and between passes) covered
      everything, including several stalls. So `XB` is still untried

#### Zeroing all the axes

- `\` then `z` ("Zero X and A" on the mill) only zeroes the two
      motorised axes. The user would like it to zero every axis, including
      the manual ones (the mill's Y and Z, axes 3 and 4), as each can be
      zeroed on its own with its leader then `z` (18 Nov 2026). Its name in
      the command table would change to suit (e.g. "Zero all axes" when
      there are manual axes)
    - **Done (18 Nov 2026), and tried on the mill (19 Nov 2026):** all the
      axes, Y and Z included, zero with `\z`. `Model::zeroAllAxes()`;
      it's "Zero all axes" in the menu when there are manual axes, and
      still "Zero Z and X" on the lathe. A manual axis with no reading
      warns, as zeroing it alone does, and the rest are still zeroed. No
      other `\` command applies to the manual axes: the rest are moves and
      memory slots, which they don't have (zero, set and halve are all they
      have, and halving several axes at once wouldn't make sense)

#### Sound

- Still getting the first sound failing - maybe the monitor's audio is
      also in idle - maybe play the alert sound on program start to kick it
      into life?
    - **Tried on the lathe (2026-11-20)** with a test key (F12 plays the
      alert; it was test code, taken out once this was settled). The delay between the key and the sound is too
      small to notice. A sound 30 minutes after the last one played as
      usual, so it isn't the output (or the monitor) going back to sleep.
      It's only the first sound after a reboot that's lost, and lost
      entirely: `aplay alarm.wav` by hand after a reboot played nothing,
      and after that, lc's first F12 played
    - **Done (2026-11-20):** lc plays the alert once at start-up to wake
      the output. Not config-driven: it's usually not heard, and it's
      harmless if it is
    - **Tried on the machine (2026-11-20):** after a reboot, and after a
      full shutdown and power-off (so the monitor was off too), the
      start-up sound wasn't heard, as expected, and the first F12 played
      both times
- Could be useful for warnings and prompts.
    - **Done (2026-11-16), not yet tried on the machine:** `SoundAlert` and
      `SoundAlarm` in the config (sounds/alert.wav and sounds/alarm.wav, on
      for both machines). SoundCues (soundcues.*) decides from the model's
      state each tick: the alert when a multi-pass pauses, finishes or
      arrives at its start; the alarm for a motor fault, a limit switch, a
      scale mismatch, or a warning which appears while checking the status
      (not one replying to a key, which the user is already looking at). The
      same warning sounds at most every 10 s, as some (the spindle too fast
      for a thread) can flicker on and off. The controller runs the player
      (`aplay -q` by default) detached, with QProcess, so nothing waits
    - lc runs as root (sudo, for pigpio), and root's processes can't find
      the PipeWire session, so the player runs as `sudo -u
      $SUDO_USER env XDG_RUNTIME_DIR=/run/user/$SUDO_UID aplay -q <file>`.
      Untested on the Pi: if no sound plays, try that command by hand from
      a root shell, and check lc.log for "Couldn't run the sound player"
    - A sharper sound for warnings, and a gentler one when waiting for the
      user (e.g. Enter between passes). Config-driven and off by default, and
      played from the UI side, never from the motor or run-loop paths
    - **HDMI works (2026-11-16):** Pi OS is set to send sound over HDMI, and a
      generated tone played with aplay is nice and loud from the monitors on
      both machines
    - **The output goes to sleep:** after a pause, the start of a sound was
      sometimes lost while the output woke up. On the lathe, `pactl list
      sinks` showed why: WirePlumber suspends the HDMI sinks (there are two)
      within about 10 s of the last sound, and the next sound wakes them
      from cold
    - **Fixed on the lathe (2026-11-16)** by turning WirePlumber's suspend
      off for the ALSA outputs. The sinks then stay IDLE, and a sound played
      a minute after the last one was heard in full. The file is
      `~/.config/wireplumber/main.lua.d/51-disable-suspension.lua`, then
      `systemctl --user restart wireplumber`:

            rule = {
              matches = {
                { { "node.name", "matches", "alsa_output.*" } },
              },
              apply_properties = {
                ["session.suspend-timeout-seconds"] = 0,
              },
            }
            table.insert(alsa_monitor.rules, rule)

        - This is WirePlumber 0.4's format (Bookworm). 0.5 (Trixie) uses
          `.conf` files instead, so it needs redoing after an OS upgrade.
          It should go in the README's setup notes once sound is in lc
        - A sound played 30 minutes after the last one was also heard in
          full, so the monitor doesn't mute its own audio while the sink is
          idle. lc can just play its sounds when needed, with no stream
          held open
        - The same config is on the mill too (done by the user, noted
          2026-11-20)

#### Ticks nearing a target

- **The idea (from TouchDRO):** while the user winds a manual axis towards
      a go-to's target, a short tick plays, faster as the axis gets closer.
      For example, the rate doubles each time the distance halves: 1 a
      second at 10 mm, 2 at 5 mm, 4 at 2.5 mm, 8 at 1.25 mm, 16 at 0.625 mm.
      Closer than that, a steady tone, then silence (or an "arrived" sound)
      within the go-to's tolerance. Config-driven and off by default
- **Can the current player keep up? (22 Nov 2026, from the code; not yet
      measured on the Pi):** each sound is a new process, `sudo -u <user> env
      XDG_RUNTIME_DIR=... aplay -q file.wav`, and each one starts sudo, then
      aplay, which opens a new stream through ALSA to PipeWire. That's
      perhaps tens of milliseconds, maybe over 100, and it varies
    - A few ticks a second (the outer stages) should be fine, as each tick
      is late by about the same amount, so the rhythm stays even
    - At 10-20 ticks a second the start-up time is about as long as the gap
      between ticks, so they'd bunch up and the rhythm would wobble, and
      players could overlap. The speed-up is the whole point, so that
      defeats it. It would also be about 20 sudo and aplay start-ups a
      second while the user is winding, and every sudo run goes in the auth
      log
    - The run loop ticks every 50 ms (`Controller::TICK_INTERVAL`), so ticks
      timed from there top out at 20 a second anyway: another reason for a
      steady tone in the last stage
- **The alternative: one stream held open.** Start one player for the
      session, through the same sudo wrapper, with QProcess (not detached):
      `aplay -q -t raw -f S16_LE -r 48000 -c 1 -`, with a small
      `--buffer-time`
    - A small sound thread writes blocks of a few milliseconds to its stdin:
      silence, or a click when a tick is due. The pipe holds the thread to
      the playback rate, so it uses almost no CPU, and the ticks land where
      they're written. The delay is only aplay's buffer
    - The tick rate is one number, set by the UI thread from the distance to
      go and read by the sound thread as an atomic, with no locks. The thread
      inherits the UI thread's cores, so it stays off the motor cores
    - The click can be generated in code (a decaying sine burst of a few
      milliseconds), so there's no WAV file. The alert and alarm could be
      played down the same stream
    - With the stream open all the time, the output never sleeps, so the
      first sound after a reboot would be the only one lost, as now
- **To test on the Pi before deciding** (from a root shell; `tick.wav` is
      any short click):

            time sudo -u mcd env XDG_RUNTIME_DIR=/run/user/1000 aplay -q sounds/alert.wav
            for i in $(seq 20); do sudo -u mcd env XDG_RUNTIME_DIR=/run/user/1000 aplay -q tick.wav & sleep 0.08; done

    - The first times one start-up. The second plays about 12 ticks a
      second, the way lc plays sounds now: if they sound uneven, hold a
      stream open. If they're steady, the current player could do, at least
      for the slower stages
    - **Tried on the Pi (22 Nov 2026):** with a 45 ms mono 48 kHz tick at
      1200 Hz, the loop at about 12 a second sounded even. So the current
      player (a new aplay for each tick) looks good enough, and the stream
      can wait unless ticks from lc itself sound uneven (it starts the
      players while it's also redrawing the display). At 20 a second
      (`sleep 0.05`) it sounded fine too
- **Decided (22 Nov 2026):** the first version uses aplay as it is now, with
      a new process for each tick, at up to 20 ticks a second (the run
      loop's limit). Once the axis is close, the ticks stop and the alert
      plays (alert.wav). The ticks are only there to tell the user that they
      ARE close: from then on they watch the scale's figures for the
      accuracy, so ticks closer together than that would be overkill. The
      stream held open is only needed if ticks from lc itself turn out
      uneven
- **Ticks from the time to go, as well as the distance (22 Nov 2026, not
      yet decided in detail):** winding the handle fast should start the
      ticks earlier, and creeping should start them later. So a projected
      time to go (the distance over the approach speed) decides when they
      start, e.g. at one second to go, doubling at half a second, a quarter
      and so on
    - Time alone isn't enough: the user slows down near the target, so
      ticks from the time to go would slow down just as the axis gets
      close, which sounds like it's getting further away. Stopping to check
      the figures makes the time to go infinite, and the ticks would stop
      even 0.1 mm away
    - So the rate is whichever is faster: from the time to go, or from the
      distance. Winding fast, the time starts them early. Creeping or
      stopped, the distance takes over, so the ticks never slow down while
      getting closer. The distance stages can start nearer than 10 mm, as
      the time gives the early warning (e.g. 2, 1, 0.5 mm)
    - The alert still plays at a fixed distance, whatever the speed: it's
      the signal to read the figures. Moving away, or going past, gives no
      ticks from the time (going past could have its own sound later)
    - The speed comes from the scale's readings, which arrive in counts at
      the DRO sender's rate, and winding by hand is jerky, so it needs
      smoothing over about a quarter to half a second, or the ticks will
      stutter
    - The time and distance stages and the alert's distance go in the
      config
- **Done (22 Nov 2026), not yet tried on the mill:** ApproachTicks
      (approachticks.*), one for each manual axis, run by the controller
      each tick alongside SoundCues; the alarm, then the alert, then a tick
      wins when more than one is due. `SoundTick` (sounds/tick.wav, on in
      lc_mill.cfg; only commented in lc.cfg, as the lathe has no manual
      axes), `SoundTickStartMm` (2), `SoundTickStartSeconds` (1),
      `SoundTickNearMm` (0.2) and `SoundTickSlowestMs` (500)
    - The interval is in proportion rather than in stages: the slowest
      interval at the start distance (or time), times the fraction of it
      left, so it still halves each time the distance halves, but speeds up
      smoothly rather than in steps. The shorter of the two (distance and
      time) is used, and never under 50 ms (one run loop tick)
    - The speed is the change in the distance to go over the last 400 ms,
      and only counts once there's at least 200 ms of readings, so a
      go-to's first moments don't give a wild one. Moving away gives no
      ticks from the time, but the distance still gives them within 2 mm
    - Within the near distance, or once past the target (even if it went
      past within one tick), the alert plays once, and that go-to has no
      more sounds. A go-to which starts that close has none, as the user is
      already there. The go-to itself still ends as before, once the
      reading settles within two counts
    - Still to see on the mill: whether ticks from lc are as even as the
      test loop's (it starts each player while redrawing the display), and
      whether the DRO sender's averaging makes the speed lag, so the ticks
      from the time start late
- **Tried on the mill (22 Nov 2026):** all working well. The only change:
      the alert was too far out at 0.2 mm, so `SoundTickNearMm`'s default
      is now 0.1 mm
- **Sound files:** WAV only (aplay can't decode FLAC or Ogg), mono, 16-bit,
      48 kHz (PipeWire runs at 48 kHz, so it doesn't resample them), with no
      silence at the start, as that adds to the delay. The user is
      converting alert.wav and alarm.wav to match

#### The menu and leader key hints

- A key that brings up all the options available *now*
    - **Done:** `Tab` opens a menu of every command, with its shortcut, built
      from one table of commands (commands.cpp) so it can't drift out of date.
      Commands not available at the moment (e.g. threading passes outside
      threading mode, or on a locked axis) are greyed out. It never blocks: motors keep running,
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
- It must be usable without a mouse, which is awkward at the machine.
      Everything works from the keyboard, and the menu from the gamepad too.
      The gamepad isn't read while a dialog is open; low priority, as it sits
      next to the keyboard and is for the basics of machine control.
      The mouse pointer is hidden in the full-screen (Pi) build

#### Dropping the F2 leader key

- **Drop the `F2` leader key.** In practice the menu (e.g. `Tab` `M` `T`
      for threading) had taken over from `F2`, which was a second route to
      the same commands
    - **Done:** `F2` (and `:`) no longer lead to anything, so the modes
      (threading, taper, radius, multi-pass, retraction direction, setup) are
      chosen from the menu's Modes, and show no shortcut there. `F2` `H`
      (help) and `F2` `Q` / `:q` (quit) went too: F1 and Ctrl+Q do the same.
      The `f2*` key codes stay as the internal command IDs the menu
      dispatches; later, perhaps rename them to something like
      `cmd::threading`, so they don't suggest a key that no longer exists
    - **Done** (2026-11-22): renamed to `key::threadingMode`, `taperMode`,
      `multiPassMode`, `knurlingMode`, `gearCuttingMode`, `radiusEndMode`,
      `radiusShoulderMode` and `retractionSetup`, kept in `key::` with the
      other menu-only commands (e.g. `clearFeature`), and with the same values
    - Frees the top-level letters, e.g. `T` for "choose tool" (see "Current
      tool and a tool database")

#### Auto-retract from the menu

- **Turn `ThreadingAutoRetract` on and off from the menu** (2026-11-11).
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

#### Controller on the event loop

- Move the controller onto Qt's event loop (e.g. a timer driving each
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
- Address the "Mode" concept, which provides pseudo-modal "dialogs"
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

### Multi-pass

#### Multi-pass drift on the mill

    - **Why (18 Nov 2026):** repeating a slot cut on the mill with a pause
      between passes, X drifted from its scale by up to several hundred
      microns over several passes. During a multi-pass, X is never re-based
      from its scale (only once it's finished), nor finished on the scale.
      Only the mismatch check runs, and only where X rests long enough for
      the scale to settle (the pauses, not the quick reversal after a cut).
      Its limit is `Axis1ScaleWarnMm` (0.2 mm), plus the backlash setting
      (820 steps, 0.256 mm) when the table is ahead of the motor the way it
      last moved, so up to about 0.46 mm that way passes unnoticed. Two
      possible causes, not yet told apart: lost steps (they would build up
      from pass to pass; X was seen stalling at the start of multi-pass
      returns on 17 Nov 2026, perhaps in the backlash take-up),
      or climb cuts pulling the table into the slack (about the same size
      each time, and depending on direction). lc.log's differences at each
      settle should tell which
    - **Seen on the mill (18 Nov 2026, still the Pi 3):** a 5 mm
      multi-pass repeated for about 15 passes stalled on the fast return
      quite a few times, and X was over 20 mm out from its scale when it
      was stopped. So it's lost steps, at least mostly, and **re-basing
      from the scale during a multi-pass is needed**, whatever mode it
      ends up in. Also worth knowing: whether the mismatch check warned
      before then, since a stall that size should have shown at the next
      pause. To repeat on the Pi 4 before changing anything
    - **Found (18 Nov 2026, on the Pi 4): the stalls come from the backlash
      take-up on a reversal.** They went on after the move to the Pi 4,
      with a "clunk" as the return started. A plain reversal by hand (jog
      X slowly, stop, wait a few seconds, then Alt+arrow back) stalled
      about one time in six too, so it isn't the multi-pass, nor the
      table's momentum (it had stopped). The cause: the 820 take-up steps
      went at the move's speed, starting at the ramp's 40% (400 mm/min on
      X) and ramping up as they went, with nothing on the motor, so the
      nut then picked the table up all at once at about 440 mm/min. Rapids
      that don't reverse start at the same speed but with the nut engaged,
      and don't stall. **Fixed, not yet tried on the mill:**
      `Axis1BacklashTakeUpSpeed` (and Axis2), in mm/min, off unless set. A
      move takes up the backlash at that speed (or its own, if slower),
      with the ramp held, then ramps up from its starting speed as from
      rest. It's in the motor thread, so it covers every reversal (jogs,
      rapids, go-tos, multi-pass returns), but not following (threading)
      or synchronised moves (tapers). Set to 100 mm/min for the mill's X:
      the 0.26 mm take-up then adds about 0.15 s to each reversal. **To
      try:** the reversal by hand, then the 5 mm multi-pass, and whether
      the clunk has gone. If it still stalls, try a lower take-up speed.
      Re-basing during a multi-pass is still wanted, as a backstop
    - **Tried on the mill (18 Nov 2026): it works.** 40 multi-pass passes
      at 100 mm/min take-up with no stalls, where five or more would have
      been expected before. But X drifted from its scale, steadily: lc.log
      (5 mm cuts at 100 mm/min, returns at 800 mm/min) shows the table
      9-11 counts (about 0.1 mm) further positive than the motor after
      every return, so the 0.456 mm check stopped the run every five passes
      (+0.089, +0.179, +0.288, +0.387, +0.497 mm). A wrong backlash setting
      or conversion can't do that, as they cancel over a round trip: it has
      to be one-way. The suspect is the return's dead stop from full speed
      (cf. "stops at speed overshoot", under Miscellaneous), with the motor
      dragged on by the leadscrew's momentum, or X's pulley slipping on its
      shaft again. **To try:** each leg by hand (5 mm at 100 mm/min, wait
      for the settle in lc.log, then back at full speed and wait), to see
      which leg gains; a run with `Axis1MaxMotorRpm` at 400, to see whether
      the drift shrinks; and a line scribed across the pulley and shaft. If
      it's the stop, "Deceleration ramping" (Motion and timing) is the fix;
      re-basing during a multi-pass would only hide it
    - **Second run (18 Nov 2026):** 6 mm cuts at 140 mm/min, returns at
      800 mm/min, about 39 passes with no pauses: the table ended 4.51 mm
      ahead of the motor in the return's direction (about 0.115 mm a pass).
      There's an audible "clunk" at each return's dead stop, and the pulley
      is ruled out (the grub screw was lined up on the shaft's flat, and
      was still on it afterwards). So the stop from full speed is the prime
      suspect. **And nothing caught it during the run:** with no pauses, X
      never rested long enough for the scale to settle, so the mismatch
      check never ran, and the 4.5 mm only showed after Space. So a check
      (or re-basing) during a multi-pass is needed as a backstop, even once
      the cause is fixed
    - **Confirmed (18 Nov 2026): it's the dead stop from full speed.** The
      same run with `Axis1MaxMotorRpm` lowered from 800 to 400 (returns at
      400 mm/min): about 40 passes, and the scale and motor ended 0.001 mm
      apart (0.1 counts), against 4.5 mm at 800. So X can stop dead from
      400 mm/min without being carried on, but not from 800. (NB rapids
      and returns go at `Axis1MaxMotorRpm` taken as mm/min: see "Rapids
      use the maximum rpm as mm/min", under Motion and timing.)
      **Fixed by deceleration ramping** (see under Motion and timing), on
      for the mill's X. **Tried on the mill (18 Nov 2026):** a large set of
      passes at 800 rpm again, and it worked perfectly: no drift
    - **Re-basing between passes: done (19 Nov 2026), not yet tried on the
      mill.** `Axis1ScaleRebaseBetweenPasses` (off unless set; on for the
      mill), separate from `Axis1ScaleRebase`, as the lathe's Z scale is new.
      In a plain multi-pass or gear cutting, before each cut, the engine asks
      the machine whether the cut axis is ready (`cutReady()`, a new state
      `Settling`, shown as "waiting for X's scale to settle"): the model says
      no until axis 1's scale has settled after the return, then re-bases it
      (logged as "re-based between passes"), and the engine moves the axis
      to the pass's start from there. Paused or not, so runs with no pauses
      (where the 4.5 mm above went unseen) are covered too, and the mismatch
      check now runs at every pass. The cost is a settle per pass (about
      2 s on the mill, as `Axis1ScaleSettleMs` is 1500). While paused, it
      also re-bases each time the reading settles, so the position shown is
      right. Not for threading, knurling or a radius, which haven't been
      tried with it. First done as Enter waiting at a pause, the same day,
      then moved into the engine to cover both. Also noticed: the re-base
      at rest is off in multi-pass mode altogether (not just during a run),
      so after a multi-pass finishes, X isn't re-based until the mode is
      left. That may be fine, but worth checking on the mill. **To try on
      the mill:** a multi-pass with pauses (pressing Enter straight after
      each return) and one without, watching lc.log for the re-bases, and
      whether the wait per pass is bearable. If it isn't, a shorter settle
      for this, or only every few passes
    - **Tried on the mill (19 Nov 2026), paused only:** two or three hours
      machining a mounting bracket (for the lathe's Z scale), feeding Z down
      by hand at each pause. No lost position: the automatic re-bases
      covered every stall. The wait wasn't intrusive (the user prefers
      accuracy to speed anyway), but a couple of times a key did nothing
      because it was still settling, which wasn't obvious from the status
      line. **Wanted:** while `Settling`, a large overlay saying it's waiting
      for X's scale (not a modal dialog: Space must still stop everything,
      and it goes by itself once settled), and perhaps a keypress refused
      meanwhile could say why rather than doing nothing. Not yet tried
      without pauses
    - **Done (19 Nov 2026), not yet seen on the mill:** a box in the
      middle of the display while `Settling` ("Waiting for X's scale / to
      settle before the next cut / (Space stops)"), drawn over the display
      rather than as a dialog, so keys still go through and it goes by
      itself. Refused keys still do nothing, without saying so: the box
      should be explanation enough

#### Slot and face or pocket modes

- **Slot and face/pocket modes for the mill.** Rather than the generic
      multi-pass, the mill gets modes for what it's actually used for, built
      on the multi-pass engine with extra options. Once they work, the
      generic multi-pass mode is turned off in lc_mill.cfg (the lathe keeps
      it, as it's really turning and boring between two memory slots).
    - **Slot:** cut along X, pause (re-basing X), the user feeds Z down,
      cut back at cutting speed, pause again (re-basing X), and repeat until
      the full depth. Each end of the slot finishes on X's scale (backing
      off along the slot and approaching again if it went past), so its
      length is right. The pause shows the depth from Z's scale (e.g.
      "Z: 0.15 of 0.20 mm this pass, 1.35 of 2.00 total")
    - **Face/pocket:** start and end off the work (facing) or at the
      pocket's walls (a pocket, where finishing on the scale matters). X
      cuts both ways, pausing at each end for the user to step Y over, until
      Y reaches Y2; then the user feeds Z down, and it repeats back across
      in Y (zig-zag in Y as well as X, to save winding Y back for every
      layer), until the final depth. Y's steps are the user's choice at
      each pause; the mode follows Y's and Z's scales rather than a fixed
      plan, so it only needs Y2 and the final Z (typed, or from where the
      axis is now)
    - **Climb cut warning:** at each pause, the mode tells from what
      changed what kind of pass comes next: Z down is full width (no
      warning); Y only is a side cut, with the material on the side Y moved
      towards; nothing changed is a spring pass. The spindle always turns
      clockwise, seen from above, for anything lc does (the user only runs
      it backwards to back taps out), so a side cut climbs when the material
      is on the right of the cutter's travel relative to the work. Config
      says which way the cutter moves relative to the work for positive X
      and Y (on the mill the table moves, and X's sign is already flipped,
      so it mustn't be guessed). The warning is on the pause's prompt line
      (e.g. "CLIMB: ..."), in a highlight colour, updating live as the user
      winds Y and Z; no sound, and no second Enter to confirm, as the user
      will often intend it (e.g. a spring pass back instead, or only a
      small change of Y)
    - **Enter waits for the scales to settle** (the prompt says "settling"
      meanwhile), so the re-base and the climb warning are from settled
      readings, not stale ones
    - **Engine options needed** (off by default, with unit tests on the
      fake machine): cut both ways (`cutBack` for Feed cuts, not just
      threading), pause at both ends (it only pauses before a pass now),
      re-base the cut axis at a pause once its scale has settled, and
      finish each end on the scale. The engine's `feedManual` becomes "the
      feed axis is a manual axis", which can now show the distance to go
    - Depends on the array of axes with manual Y and Z (see "Structure and
      refactoring")

#### The multi-pass engine

- Try the rewritten multi-pass on the machine.
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

#### Retracting only on the last passes

- Retract only on the last passes (suggested 2026-11-10): the roughing
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

#### Retracting by a safe amount

- Retract by a safe amount, not an arbitrary one (noticed 2026-11-10,
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

#### Internal threading and saddle rigidity

- Internal threading and the carriage's rigidity (2026-11-10): with
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
    - Cut the far wall of the bore with the spindle in reverse
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
    - ~~Lock the cross-slide physically during each cut~~ **Parked
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

#### Repeated cuts

- Automation of a repeated cut: cut from Mem 1 to Mem 2, fast return,
      wait for a key press, and repeat until cancelled (to make repeated cuts
      down to depth easier)
    - **Done:** multi-pass with a step-over of zero and "pause". While it's
      paused, X can be nudged (W/S, not the Alt nudges, as releasing Alt
      stops everything), and the next pass cuts from there; with a
      step-over, the next pass steps over from there. X is now unretracted
      before the pause (at the start of the cut, clear of the work), so it
      shows the position cut at, and can be nudged

#### Threading passes

- The same for threading: at the moment the operator has to (a) move to
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

#### Radius mode

- **Radius mode: a radius on the work's end, as passes of short tapers.**
      Replaces the old experimental radius mode, which synchronised X to
      Z's absolute position with Pythagoras, needed both axes zeroed on the
      radius's apex, and left the passes to the user (cut, return, nudge,
      re-zero). Its X also fell behind near the end face: the motor's
      synchronisation sets the follower's speed from the *average* ratio
      since the sync started, not the slope where it is, and an arc's slope
      goes towards vertical there, so X lagged and cut too deep.
    - **Done (2026-11-13):** radius mode (menu
      Modes, `O`; only with `CurrentFeature`) starts from the current
      feature: face the end, mark it (`ZE`), set X as a diameter, and the
      program knows where the corner is. The dialog asks for the radius (up
      to half the diameter, a full ball end), the depth per pass, and the
      tool's nose radius. radius.cpp works out the passes: concentric arcs
      about the finished radius's centre, the first the largest that still
      touches the corner (so it takes off no more than the depth per pass),
      stepping in to the radius. Each is clipped to the work, from the end
      face round to the diameter, so the first passes are short, nearly
      straight cuts across the corner, and every pass removes about the
      same depth. Each arc is a series of short tapers, within ±0.005 mm of
      the arc (the points are moved out by half the chord's sag, so the
      tapers fall either side of it), at most 10° each
    - **The engine:** `MultiPassPlan::paths` gives each pass its own start X
      and list of points; the cut goes from point to point, X following Z
      at a fixed ratio on each (the ratio worked out from where the axes
      actually are, so a step of rounding isn't carried on), then Z returns
      straight back along the diameter, which is clear of the work once the
      pass has cut. No retract: during a cut Z only moves towards the chuck
      and X only outwards, so neither reverses part way. Pausing, `Enter`
      during a pass and Space work as for any multi-pass
    - **X's speed:** near the end face X must move many times as fast as
      Z, so each taper caps Z's speed so that X needs no more than 80% of
      the speed it can *start* at (40% of its maximum, where ramping
      starts). Above that, X ramps up slowly (from 40%), and on a short
      taper never gets there: in the tests it lagged by 0.05 mm until this
      was capped. On the lathe that's about 43 mm/min of X, so the last
      pass slows Z a lot for its first few tapers (for a 5 mm radius, to
      under 3 mm/min on the steepest, for about a second)
    - **Nose radius:** the tool's tip as touched off (on the diameter and
      the end face, as a right-hand turning tool) follows an arc of the
      cut's radius plus the nose radius, moved towards the chuck and the
      centre by the nose radius. Without it, a 0.4 mm nose leaves the
      radius about 0.17 mm proud at 45°. Typed in the dialog each time
      until there's a tool database (see "Current tool and a tool
      database")
    - **Also fixed:** the backlash take-up before X follows Z (taper mode,
      and `TaperCorrection`'s multi-pass cuts) took X's backlash up by the
      sign of the taper's ratio and Z's direction in steps, which on a
      machine whose axes' conversions have the same sign (as lc.cfg's both
      are negative) is the opposite way to the way X then moves. It's now
      worked out from both conversion factors. Worth checking on the
      machine with a dial indicator at the start of a taper; it might
      explain some of `TaperCorrection`'s shortfall (about 60% of the
      expected effect)
    - **To try on the machine:** whether Z's stop at each taper's end (it
      stops while X catches up and is re-synchronised) leaves marks; if so,
      a single X = f(Z) synchronisation with the speed from the local slope
      would avoid the stops (the "profile" idea below needs that anyway).
      Whether X keeps up at 80% of its start speed. The full ball end's last
      pass crosses the centre line by the nose radius. Radii on the inside
      (a bore's mouth) and concave fillets at a shoulder aren't done
    - **Tried on the machine (2026-11-13):** a 5 mm radius on aluminium
      turned to about 15.8 mm, at 0.1 or 0.2 mm per pass. The result was
      very good and usable straight off the machine. There was slight
      banding, which a lick of emery cloth would take out. It's most
      likely the tapers' flat facets (the ±0.005 mm chords) rather than
      Z's stops at each taper's end; a finer tolerance on the last pass
      would confirm it, but the finish is good enough not to bother. X
      kept up, with no sign of it cutting deep near the end face. The
      full ball end hasn't been tried. Considered done; inside radii and
      concave fillets would be new items (fillets: see the next item)

#### Filleted shoulder

- **A filleted shoulder: a step turned down, with a concave radius in the
      corner.** A fillet is material that's left, not taken off, so it can't
      be added to a shoulder already turned square: the mode turns the step
      with it. Arcs about a centre outside the work, cut as chains of tapers
      as for a radius on the end: along a concave fillet towards the chuck,
      X still only moves outwards and Z only towards the chuck, so the same
      approach works, the steep end being at the shoulder rather than at
      the start
    - **Done (2026-11-16):** a second kind of
      radius mode (`RadiusKind::Shoulder`; menu Modes, `F`, "Filleted
      shoulder"; only with `CurrentFeature`), sharing radius mode's model
      state, display and arc-to-tapers code rather than being a separate
      mode. The dialog asks for the finished diameter, the length to the
      shoulder (from the end face), the fillet's radius, the depth per pass
      and the nose radius. The passes: straight passes down to the fillet's
      top, right up to the shoulder; straight passes down to the finished
      diameter, stopping the radius short of the shoulder; then arcs about
      the corner of the square block that leaves, growing by the depth per
      pass up to the radius (concentric, so each takes off the same depth,
      as the convex radius's shrinking arcs do). Once finished, the
      feature's diameter is the finished diameter
    - **Two engine changes:** a path may have its own start on Z
      (`MultiPassPath::cutStart`): the arcs start just short of the block,
      which may be a long way from the end, and Z goes there at full speed
      rather than feeding through air. And a path's point may move X alone
      (its Z the same as the last's): with a nose radius, the straight
      passes to the shoulder leave the nose's shape in the corner at the
      fillet's top, above where the last arc ends (square to the face, the
      nose's centre the nose radius below the top), so the last pass goes
      up the face to the top. That's at X's speed, which can't be set for
      the move: it's only the nose radius long, taking off a sliver
    - **Nose radius:** the nose's centre follows an arc the nose radius
      *smaller* than the cut (on the inside of the curve), so the nose can't
      be larger than the fillet. Arcs no larger than the nose aren't cut
      (the nose would fill them); the first one that is takes them off too.
      A nose the fillet's size makes the last pass just a move on to the
      corner and up the face
    - **The tool** must be able to face the shoulder (e.g. a 93° or 95°
      approach angle), as it ends moving X outwards with Z still
    - **To try on the machine:** whether the arcs' start, just short of the
      block (by `FeatureStartClearance`), and the moves between passes there
      are clear in practice; the finish at the fillet's top, where it meets
      the face; and whether X keeps up near the shoulder, as it did on the
      end. The return after each arc is along the height it ended at (for
      the last pass, the fillet's top, which the straight passes cut), so
      the tool rubs along a cut surface, as on the end
    - **Tried on the machine:** a 2 mm fillet with a sharp right-hand
      turning tool (nose radius 0) came out just as expected. With no nose
      radius, the last pass's move up the face (X alone) wasn't used, so
      that's still to try, with a tool whose nose radius is typed in

### Motion and timing

#### Deceleration ramping

- Deceleration ramping. At the moment there's only acceleration, so
      stops (especially from rapids) are abrupt. Needs tuning on the machine,
      and changes where moves end. Also, the ramp rate is per loop, so depends
      on the step rate
    - **Done (18 Nov 2026), and tried on the mill the same day:** multi-pass
      at 800 rpm with no drift, and released rapids slowing down felt
      natural and easy to manage when getting close to a position.
      `Axis1DecelerationRamping` (and Axis2), off by default, on for the
      mill's X; `Axis1DecelerationRampingSpeed` sets its rate (0-100, as
      `Axis1RampingSpeed`, which it defaults to). Prompted by the mill (see
      "Multi-pass" above): X drifted about 0.1 mm a pass from its scale,
      stopping dead from 800 mm/min at each return, and not at all from
      400. In the motor thread, mirroring the ramp up: once the steps left
      are no more than it takes to slow to the ramp's start (40% of the
      maximum rpm) at that rate, it slows by the same amount each step, and
      doesn't speed up again. A short move speeds up and slows down with no
      flat part in between. Moves always end exactly on their targets. Not
      while following (threading) or synchronised (tapers), nor without
      acceleration ramping
    - **Decided (18 Nov 2026, the user): the stops the operator makes slow
      down too**, except Space, which still stops everything at once. So a
      released rapid (the arrow, Alt, or the gamepad's stick) and a jog
      stopped by pressing its key again run on while they slow:
      `StepperMotor::decelerateToStop()` moves the target to the nearest step
      it can slow down by. On the mill's X that's about 2.5 mm from
      800 mm/min (about 3.3 mm from letting go, with the 60 ms
      `RAPID_RELEASE_MS`), or about 1 mm (1.8 mm) with the rate at 100. The
      view: when rapiding up to work, they stop well short (say
      10 mm) and approach slowly from there. Pressing the key again while
      it slows stops it at once. A rapid's release is its own key code
      (`key::RAPID_RELEASED`); the controller treats it as Space unless a
      rapid is running, as before. This replaces the earlier plan below,
      which kept the operator's stops instant
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
          re-measure it with a dial indicator (Setup mode, which
          showed the motor steps for this, was removed on 16 Nov 2026)
        - Wanders after fast or long moves: steps are being lost, so ramping
          (or a lower maximum X speed) is worth doing
    - Related idea, for sizing: after setting the measured diameter, type the
      target diameter and X moves straight to it

#### X stalls near the ends of the table

- **X stalls on Alt+arrow rapids near the table's ends (mill, 19 Nov
      2026).** Several times in a session, after moving the head well away
      from the work (to get calipers on it), an Alt+Right rapid back towards
      the start stalled. The scale's re-base caught each one, so no
      position was lost. The user suspects the gibs are tighter towards the
      ends of the table, as the stalls weren't seen in the middle. Not yet
      known: whether it stalls at the start of the rapid (after the
      backlash take-up, if it reversed) or once at speed. lc.log's motor
      lines and the re-base sizes should tell. Ideas: adjust the gibs; a
      lower rapid speed, or a gentler ramp, for X (note that rapids go at
      800 mm/min: see the next item); a separate, slower speed for long
      rapids. Related: "Motor stalls: stop when the driver goes into its
      error mode", as the closed-loop driver would know at once
    - **From lc.log (19 Nov 2026):** six mismatch stops between 15:20 and
      15:34. Five were stalls, the table left behind by 5.5 to 28.8 mm, and
      each one started or ran beyond about X = +50 mm (the table's
      right-hand end), so the gibs theory fits. Two were reversals (so the
      slow backlash take-up didn't prevent them) and one wasn't, stalling
      while running *into* the tight region at 800 mm/min. The clearest:
      a fast return (`x f`) from 65.6 counted 29 mm in 2.45 s while the
      scale moved 0.2 mm, and only Space stopped it. Each was only caught
      once X had stopped and the scale settled. The sixth (15:20:21) had
      the table 5.05 mm *ahead* of the motor after a rapid, which a stall
      can't do: probably a misread (the check for misreads only watched at
      rest), and the -5.51 a minute later its undoing. The user remembers
      seeming to lose position then, and started the pass again. The
      0.05-0.2 mm corrections after reversals are X's real backlash
      (0.31-0.45 mm) against the 0.256 mm setting
    - **Done (19 Nov 2026), not yet tried on the mill: the scale is watched
      while moving.** The reading is compared with where the motor was,
      averaged over `Axis1ScaleLagMs` (the sender's smoothing; 1000 on the
      mill, from its readings settling 0.7-1.4 s after a stop). Further
      behind than `Axis1ScaleStallMm` is a stall: everything stops at once,
      with a warning, and the settle then asks as usual. Changing faster
      than the table can move, or further ahead than
      `Axis1ScaleMovingFaultMm`, is a misread, dealt with as at rest. Through
      a one-second average, a stall shows as 5t² mm behind after t seconds
      at 600 mm/min (6.7t² at 800, so 3 mm in about 0.7 s), against 2.45 s plus a
      settle before. 3 mm for both on the mill is a first guess: the settle
      lines in lc.log now give how far behind and ahead each move's reading
      got, so check those on normal rapids, and tighten them if there's
      room. If the lag model is wrong (the sender isn't a plain one-second
      average), fast moves could stop falsely; the log will show. Still to
      do: the gibs, and a lower rapid speed for X if they don't cure it
    - **Tried on the mill (19 Nov 2026):** at 3 mm, the misread check went
      off on every rapid (the user put it up to 5 to get on). lc.log of four
      rapids at 800 mm/min with it back at 3: each tripped "ahead of the
      motor" after 0.5-0.65 s, with the reading only 1.0-1.6 mm behind the
      motor, where a 1000 ms average expects 4-5 mm. That's a box average of
      150-245 ms, so the lag was wrong, not the scale: 1000 came from the
      reading still changing 0.7-0.9 s after a stop (2.2-2.4 s to settle,
      less the 1.5 s window), which is the last count or two creeping in,
      not the sender smoothing the whole move. `Axis1ScaleLagMs` is now 200
      on the mill (within 0.3 mm of all four), and the limit back at 3.
      Tried on the mill later that day: rapids no longer set it off. If it
      keeps holding, both limits could come down (to 2, say), which would
      catch a stall sooner still
        - The dialog only flashed up: it was "press any key", and the
          rapid's arrow, still held, closed it at once (choosing "keep X's
          position", unseen). Fixed: "press any key" now ignores repeats,
          arrows, and anything with Alt

#### Rapid speed units

- **Rapids use the maximum rpm as mm/min.** `axis1Rapid()` and
      `axis2Rapid()` set the speed to `getMaxRpm()`, and multi-pass returns
      use `cutMaxSpeed()`, which is the same, but `setSpeed()` takes
      units/min. So on the mill's X (1.25 mm a motor turn), 800 rpm gives
      rapids and returns at 800 mm/min (640 rpm), not 1000. Found on
      18 Nov 2026. Harmless as such (the motor's own limit still applies),
      but confusing: lc.log's "at 800/min" is mm/min, and lowering
      `Axis1MaxMotorRpm` to 400 gave returns at 400 mm/min. What fixing it
      would change: the lathe's Z nothing (1 mm a turn); the lathe's X
      nothing (a third of a mm a turn, so it's already at its maximum rpm,
      the motor's limit); the mill's X 1.25 times faster; and the mill's A
      five times faster (5° a turn: 640°/min now, which is only 128 rpm).
      So it wants trying on the mill first

#### Electronic leadscrew

- **Electronic leadscrew: lock Z's *position* to the spindle, not its
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

#### The INFEED and SIDEFEED constants

- I defined some constants for a slight Z infeed when cutting threads
      (see INFEED and SIDEFEED) but never used them. Maybe implement to
      simulate the use of the topslide
    - **Done:** as the threading passes' compound infeed (see Multi-pass);
      the constants have gone

#### Taper correction

- A setting for adding a tiny amount of X motion to counteract a lathe
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

#### Bump map

- A "bump map" mode, for a finishing cut where a piece must be as
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

#### Profile mode

- A "profile" mode: an electronic version of a mechanical follower, for
      turning simple profiles (e.g. a chess piece) from a file. Idea only,
      for discussion, but it shares its machinery with the bump map above:
      X follows a function of Z's position, from a table of points
    - The old radius mode did this for a circle (X worked out from Z's
      absolute position by Pythagoras). A profile swaps Pythagoras for a
      lookup in the table. (Radius mode now cuts arcs as a chain of tapers
      instead: see "Radius mode" under Multi-pass. A profile's roughing
      passes could use the same `MultiPassPlan::paths`.) Since X depends only on where Z is, Z can
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
      a points file, start from the current feature as radius mode does, drive Z by hand
      with X following, and step the depth by hand. Test it on a profile
      whose X only moves one way (e.g. a cone with a radiused end), so
      backlash doesn't come into it yet. Lathe only, and off unless the
      config enables it

#### Enable pins

- Support the enable pin for each motor
    - **Not used yet:** `Axis1GpioEnablePin` and `Axis2GpioEnablePin` are
      read and passed to the GPIO layer, but nothing calls
      `setEnablePin()`, so the pins are never driven. Both configs set them
      to `0`, which is only harmless because of that: GPIO0 is a real pin
      (the HAT ID EEPROM's data line). When this is done, an empty setting
      (as `LimitSwitchPin` uses) should mean "no enable pin", so a config
      left at `0` can't start driving GPIO0
    - **Mill wiring (planned 18 Nov 2026, while fitting its Pi 4):** ENA
      for Axis1 (the table's X) on GPIO11 and for Axis2 (the rotary table,
      A) on GPIO16, ready for when the program drives them. GPIO11 is
      SPI0's clock, which is free as SPI0 has to be off anyway (X's step
      and reverse are on GPIO10 and GPIO9). Until the program drives them they stay inputs with their
      default pull-downs, so no current flows through ENA, which most
      drivers (the HSS57 and DM-series) treat as enabled. Check that on the
      machine. The Pi drives 3.3V: step and direction work at that level on
      these drivers, so ENA probably will too, but check it with a meter
      once it's driven

#### Driver alarms

- **Motor stalls: stop when the driver goes into its error mode.** At the
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

#### Limit switches

- Support limit switches. Could have multiple switches all on one pin,
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
    - **Mill wiring (planned 18 Nov 2026, while fitting its Pi 4):** the
      limit switches on GPIO2 (`LimitSwitchPin = 2`,
      `LimitSwitchActiveLevel = high`). GPIO2 is I2C's SDA, so it has a
      fixed 1.8kΩ pull-up to 3.3V on the board, which suits normally-closed
      switches to GND and helps on a long cable. I2C must stay disabled in
      raspi-config (the default)
    - **Microswitches, not contact leads (decided 23 Nov 2026, not yet
      wired):** the other idea was one banana socket to the machine's body
      as GND and another to bare leads, insulated from the body, which the
      carriage touches. That needs only one wire per switch, but it's
      normally open: a broken lead, or a contact fouled by oil, coolant or
      oxide, never trips, where normally-closed switches in series fail
      safe. It also ties the Pi's GND to the frame, which carries the
      motors' leakage and switching noise to earth. With any other path
      from the Pi to earth (a monitor's HDMI shield, a supply's negative, a
      scale's cable shield), that makes a ground loop through the same
      grounds as the scales and encoders. Running the lathe already upsets
      the mill's scale readings, so noise is getting in somewhere. The
      microswitches go to the Pi's GND with their own wires, not via the
      frame. Before bonding anything to the frame, check with a meter
      whether the Pi's GND is already tied to it (e.g. through a scale's
      shield), and measure the AC and DC volts between them with the lathe
      idle and running

#### Probing and edge finding

- Touching a tool or probe off the work by electrical contact (the idea
  is in Outstanding: Sensors and scales). Not started; notes from 23 Nov
  2026:
    - **Keep the Pi off the frame.** Edge finding does need the machine as
      one side of the circuit, so isolate the sense circuit rather than
      bonding the Pi's GND to it (see the ground-loop note under
      [Limit switches](#limit-switches)). A small isolated DC-DC module
      (e.g. 5 V in, 12 V out) or a separate 12-24 V supply, with its
      negative to the frame, and the sense lead through a resistor to an
      optocoupler's LED. The Pi sees only the opto's transistor, so no
      ground loop, and a spike on the lead can't reach a GPIO
    - **Wetting current.** A light touch through cutting fluid and oxide
      won't conduct reliably at 3.3 V through a pull-up; 12-24 V at about
      5-10 mA does (why industrial inputs are 24 V). If a GPIO were ever
      driven directly: a series resistor (about 1 kΩ), an RC filter and a
      clamp or TVS diode at the Pi, and a strong pull-up, not the internal
      one of about 50 kΩ
    - **The path through the spindle is unreliable.** Through a tool in
      the spindle, the circuit runs through the bearings, whose grease film
      conducts on and off (worse while turning). Clip the sense lead to the
      tool or probe instead, with the frame or the work as the other side,
      or insulate the work (paper or plastic underneath) and clip to it
    - **Latch, don't stop.** What matters is the axis's position at the
      moment of contact, not stopping within the limit switch's 2 ms
      filter. A pigpio alert callback gives a microsecond timestamp, so
      the position at contact can be recorded from it, then the axis backs
      off and touches again more slowly for accuracy (the usual two-touch
      probe). That's separate from the limit switch path, so give it its
      own pin, off by default per machine like the other hardware features

#### Start-up backlash take-up

- **Ask before the start-up backlash take-up.** Each axis moves by its
      backlash at start-up (since 14 Nov 2026, when the take-up was found
      never to have moved; see the mill scales item). A tool left against
      the work while everything was off could be pushed into it. The idea
      (14 Nov 2026), with additions from discussing it:
    - Before the take-up, a dialog says the backlash is about to be taken
      up, and asks the user to make sure the tool is clear of the work by at
      least each axis's backlash, which way and how far (e.g. "X +0.39 mm").
      It has only OK: there's no cancelling (decided 14 Nov 2026), as the
      slack must be taken up for positions to be right. The take-up starts
      only once OK is pressed (Enter, so the gamepad's Enter works too).
      Nothing can move until then. Space still stops the move
    - A take-up direction per axis in the config (as `Axis1TowardsChuck`),
      so each machine takes up the slack in its usually safe direction:
      away from the chuck on Z, outwards on the lathe's X (unless boring).
      `setBacklashCompensation()` already allows either side
    - Rejected: remembering where the slack was at shutdown (a handwheel
      turned while off would make it silently wrong), and taking the slack
      up as part of the first move (overshoots by the backlash if it was
      already taken up that way)
    - Related: the TODO comment at the take-up in `Model::initialise()`

### Sensors

#### The lathe Z scale

- **Linear scale: detecting lost steps, and absolute position.** The
      scale is read and displayed ("Z Scale"), but positions still come from
      "dead reckoning" (the motor's step count), which is wrong from the
      moment a step is lost. Should position come solely from the scale, or
      should the scale correct the expected position? Thoughts so far:
    - **The lathe's scale, being fitted (19 Nov 2026):** a "Garosa Linear
      Scale 250mm" from Amazon (a generic 5µm, 5V TTL quadrature scale; no
      manual). Its DB9 is **not** the common Sino pinout (2 = 0V, 6 = A,
      7 = +5V, 8 = B): the proof-of-concept socket has **1 = +5V,
      2 = GND, 3 = A, 4 = B**. A and B are 5V signals, and the Pi's GPIOs
      aren't 5V tolerant, so they go through a 2-channel level shifter (5V
      side to the scale, 3.3V side to GPIO 5 and 6). (The proof of concept
      fed them in directly, but only on an old, now spare, Pi 3.) Then
      check the sign, the counts per mm (200, as set) and the settle time
    - **Wired up (23 Nov 2026)** through a BSS138 level shifter board (HV
      to the Pi's 5 V, LV to its 3.3 V). It read well, and updated quickly
      on the display. `Axis1ScaleReversed` had to be true, and a backlash
      test showed `Axis1BacklashCompensationSteps` was a little low, so it
      was raised. **But with the spindle running, the reading drifted**,
      as the mill's scales do while the lathe runs:
        - With the carriage still, it drifted steadily one way, at much the
          same rate whatever the spindle's speed (the VFD's carrier
          frequency doesn't change with it). Moving the carriage a little
          and stopping again, once it drifted the other way.
        - With the scale unplugged at the DB9, the reading didn't change,
          so the noise comes in with the scale, not on the board or the
          Pi's wiring.
        - No continuity between the Pi's GND and the DB9's shell, the
          motor, the lathe's body or the scale's body: no direct ground
          loop, so it's coupled in some other way (capacitively, perhaps
          through the read head on the frame).
        - **The cause, it seems:** a spike on the scale's ground or supply
          changes both pins at once. pigpio reports the two in one sample
          as two callbacks with the same tick, A first, and the decoder
          counted them as two steps the same way, and again when the spike
          ended: four counts (20 µm) a spike, which way depending on the
          phase the carriage rests at (A = B: one way, A ≠ B: the other).
          That fits both the steady drift and its reversal. (With each
          pin's level tracked separately, a "two-phase jump" can't happen,
          so counting those, as once planned, wouldn't have shown it.)
        - **Done:** both pins changing in the same tick are now taken as a
          spike, the first undone and neither counted, and lc.log reports
          how many (at most every 10 s). `Axis1ScaleGlitchUs` sets pigpio's
          glitch filter, for spikes whose two edges fall in different
          samples.
        - **Tried on the lathe (23 Nov 2026):** with the simultaneous-change
          fix alone, it still drifted with the spindle running, so most
          spikes' edges on the two pins don't fall in the same 5 µs sample.
          With `Axis1ScaleGlitchUs = 10` too, the reading was rock solid with
          the spindle on. The spikes must be shorter than 10 µs, so an
          edge-to-edge spacing that short never comes from the carriage
          moving (about 600 µs at 1000 mm/min). A fast rapid with the
          filter on lost no counts: the scale and motor still agreed.
        - **If that isn't enough:** the VFD's carrier frequency (Pr. 72 on
          the FR-D700, to be checked; if the drift rate follows it, that
          confirms it); a shielded motor cable earthed at both ends, and
          ferrites; a Schmitt buffer (e.g. 74LVC14 at 3.3 V, which inverts,
          so flip `Axis1ScaleReversed`) or 6N137 opto-isolators in place of
          the BSS138 board, whose 10 kΩ pull-ups are easily disturbed. A fix
          at the VFD might help the mill's scales too
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
      **Done (13 Nov 2026):** the old code mixed them (with
      `Axis1UseLinearScale` on, moves were made from a step worked out from
      the scale, so were out by the whole drift). That key has gone. Now
      `AxisScale` (scale.h) lines the scale up with the motor, and the
      display and lc.log show the difference; see "Use the mill's scales in
      latheControl", which did this for both machines
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
    - **Creep at rest (mill, 19 Nov 2026):** the reading changes by 2 counts
      every few minutes at rest, and each change is re-based: from 15:36 to
      15:45, the position went 0.06 mm one way with nothing moving. Real
      (thermal?) or the scale creeping? If the latter, re-basing at rest
      chases it. Worth watching with a dial gauge on the table
    - **Finishing moves** sometimes end with "the scale's reading didn't
      settle" after their second correction (19 Nov 2026, on the mill), as
      the reading flickers. Harmless (it's within a count or two), but it
      wants looking at

#### Interfacing with the mill scales

- Investigate how to interface with the mill's linear scales
    - **Done (13 Nov 2026):** a USB-serial adapter is now wired in for good,
      listening to the Arduino's TX, and `watch_scales.py` reads the stream
      from `/dev/ttyUSB0`, with TouchDRO still working. The box's X and Y
      socket labels were indeed swapped, and have been corrected. Making use
      of the readings is the next item
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
        - Logic levels: the crystal is marked "08", so it's the 3.3 V/8 MHz
          Pro Mini (taken as settled, 23 Nov 2026), though the schematic's
          dividers suggest 5 V. An adapter set to 3.3 V is right, and the Pi
          is safe either way. A meter check is still worth doing before
          wiring TX straight to a Pi's GPIO
        - The adapter only listens, so it can't upset the HC-06
        - The box stays on its own adapter. Everything at the mill draws power
          from one mains socket, switched by a Zigbee button, so it all comes
          on at once anyway
        - Which pin is the clock. The sketch says D9; the schematic says D2.
          Only matters if the sketch is ever changed, as we only listen
        - Axes and signs, checked with `watch_scales.py` (10 Nov 2026): each
          letter is the axis it should be, so the sketch matches. The box's
          socket labels had X and Y swapped, and were corrected on
          13 Nov 2026. Counts go up when the table moves right (X),
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
    - **Possible later (23 Nov 2026), very low priority: reflash the sender
      for lc alone.** TouchDRO wasn't even switched on in the last couple
      of big milling sessions, so it may be dropped. Wiring the scales
      straight to the Pi's GPIO was considered and isn't worth it. The
      scales are iGaging's clocked 21-bit serial, not A/B quadrature, so
      the Pi would have to bit-bang the protocol (see Rejected). Their
      cables, where the lathe's noise most likely gets in, would still be
      there, just running to the Pi, along with the ground-loop risk (see
      [Limit switches](#limit-switches)). Instead, if the settle time
      (about 2 s a pass) ever seems worth cutting:
        - Back up what's on the Arduino first (avrdude, through its
          bootloader; the steps are in `hardware/touchdro-sender/README.md`)
        - Turn the sketch's smoothing off (`SCALE_*_AVERAGE_ENABLED 0`):
          that's the one-second creep after a stop. Raise
          `UPDATE_FREQUENCY` (up to 64) and the baud rate with it (9600
          tops out near 38 readings a second). Then re-tune
          `Axis*ScaleLagMs` and the settle times. The scales' own update
          rate caps how fast this can get
        - Perhaps read each scale twice per reading and drop it if the two
          differ, against the garbage jumps while the lathe runs. First
          watch the raw stream with the lathe running, to find which axes
          jump and when
        - Perhaps wire TX to a Pi 4 UART instead of the FTDI adapter, to
          drop USB. GPIO 14/15 may be wanted for a serial console, but the
          Pi 4 has spare UARTs (uart2-5, by dtoverlay); check for pin
          clashes first. The current settle time is bearable, so none of
          this is pressing

#### Using the mill scales

- **Use the mill's scales in latheControl.** To start with, X (the
      table, axis 1), which has a stepper; perhaps later Y and Z as
      read-only axes, which are moved by hand (now planned as manual axes:
      see "An array of axes" under "Structure and refactoring")
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
    - **Plan (agreed 13 Nov 2026), in stages:**
        1. The shared base: one serial reader for the sender, a common
           interface for any scale, scale and motor kept in separate frames,
           and config per axis
        2. Show the scale and its difference from the motor, and log it each
           time the axis stops, to measure the real lag, settle time, jitter,
           backlash and drift
        3. Warn of lost steps: at rest, once the reading has settled, a
           difference of more than a few counts stops things and warns
        4. Correct at rest (see below)
    - **Done (13 Nov 2026), stages 1 and 2, not yet tried on the machine:**
        - `Axis1Scale` = none (the default), quadrature (the lathe's Z) or
          serial (the mill's X), with `Axis1ScaleCountsPerMm`,
          `Axis1ScaleReversed`, the GPIO pins or serial device and letter,
          and `Axis1ScaleSettleMs` and `Axis1ScaleMaxSettleMs`. The old
          `LinearScaleAxis1*` keys are renamed to match. Only axis 1 so far
        - `DroSerialReader` (droserial.*) reads the port in its own thread,
          started from the main thread, so kept off the motors' cores. A
          reading is stale after a second with no update. It retries every
          second if the port can't be opened
        - `AxisScale` (scale.*) lines the scale up with the motor once the
          reading has settled with the motor at rest: at the start, after
          zeroing or setting the position, and when readings return after
          being lost (the sender's count starts again if it's switched off).
          The motor's position already leaves out the backlash steps, so the
          difference should stay near zero in both directions
        - The display shows e.g. "X scale: 12.345 mm, off by +0.010" below the
          spindle speed. Each time the axis stops, lc.log records how long
          the reading took to settle, the move, and the difference in mm and
          counts
        - FAKE builds simulate the sender (`FakeDroScale`), following the
          motor with a lag
    - **Tried on the mill (13 Nov 2026):** it works. `Axis1ScaleReversed`
      had to be true. Moves to -10 and to -100 both ended about 0.3 mm
      "off"; going back to 0 read 0 again (flickering between 0.00 and
      0.01, which is the scale), and on to +10 was spot on. So the error
      doesn't grow with distance, comes with a reversal and goes with the
      next one: it's backlash, not lost steps. The configured 300 steps
      (0.094 mm) is too little: the real slack looks like about 0.39 mm,
      about 1260 steps (perhaps the 25:40 gearing as well as the nut). So
      the scale has already earned its keep by measuring it. (The start-up
      take-up of 300 steps couldn't take up all of that slack either.)
    - **Done (13 Nov 2026):** with `Axis1BacklashCompensationSteps` at 1260,
      the difference stays at about zero in both directions. At rest, lc's
      scale reading sometimes differs from TouchDRO's by 10-20 µm (one or
      two counts); see the next points
    - Also seen: at rest, TouchDRO's reading moved by 0.01-0.02 mm a couple
      of seconds after the table stopped. That's the sender's smoothing (see
      hardware/touchdro-sender/README.md): it can freeze a count or two off
      while the raw reading jitters, and only jumps to the raw value once
      all of its last 24 readings (one second) are the same. lc's 500 ms
      settle time was shorter than that, so it could line up or log on the
      frozen value, which may well explain the 10-20 µm. The mill's
      `Axis1ScaleSettleMs` is now 1500. If that's not enough (e.g. the raw
      reading keeps jittering, so it never jumps), the sketch could send
      the raw readings as well; that's the only way to remove the smoothing
      for lc while TouchDRO keeps it
    - **Tried on the mill again (13 Nov 2026), with the 1500 ms settle
      time.** Every move settled 2.05-2.65 s after stopping, so the reading
      kept changing for 0.5-1.1 s after the motor stopped, which fits the
      sender's one-second window. None hit the 3 s limit.
        - **Counts per inch:** after zeroing both and moving 100 mm, TouchDRO
          read 100.04 and lc's scale 99.88 (motor 100.00). TouchDRO's adapter
          profile (exported to a .tdap file; the newer app has no way to see
          it otherwise) has 2556 counts per inch for X, Y and Z, set by the
          user with a DTI long ago, against our nominal 2560: 0.16%, which is
          exactly the gap. lc.log's 10,068 counts for the motor's 100 mm is
          about 2557. So the scale and the 2 mm leadscrew agree to about
          0.05%, and lc_mill.cfg now uses 2556 (100.6299 per mm)
        - **Sudden jumps in the difference.** Three times it suddenly grew,
          by about 5, 10 and 11 mm (once a move of exactly 90.0000 mm by the
          motor, which the scale and TouchDRO read as 100.07). It was lc's
          main X readout (the motor's position) which the user saw jump, not
          the scale. (A first guess, that the scale had lost whole 5.08 mm
          pitches of its pattern, was wrong.) Not reproduced on 14 Nov, but
          see 10:12 below
    - **Tried on the mill (14 Nov 2026), with a DTI on the table:**
        - The start-up take-up now works: the first move after starting was
          right (2.7 counts)
        - Repeated moves 0 → 10 → 0 and 0 → 15 → 0 (all reversals): the DTI
          read 9.94 and 14.91, and back at zero every time (within 10-20 µm).
          The scale saw the same 10 mm moves as about 9.957, agreeing with the
          DTI to within 0.02 mm; the motor said 10.00. In lc.log the
          difference went back and forth by 4-5 counts between the two ends.
          That's residual backlash: each reversing move falls short by the
          same amount, and the return lands back on zero. So 1260 steps is
          still about 0.05-0.06 mm (160-190 steps) short: try about 1420.
          The 15 mm moves leave about 0.04 mm unexplained (the DTI was set
          square by eye, so cosine error should be small)
        - The counts per inch can't be settled against the motor: over
          100 mm the scale read 0.1% differently from the day before, on
          another part of the table. Probably the leadscrew's pitch error
          varying along the screw (which is what the scale is for). 2556
          stays, until it can be checked against an independent reference
          (gauge blocks, or a long, carefully aligned DTI)
        - **At 10:12, the difference suddenly became -1.36 mm (137 counts).**
          X had settled at motor 0.00; when lc next saw it moving, the motor
          was already at +1.70 (at 40 mm/min it can't go that far in one
          50 ms update), and it ended at 1.10, while the scale said the table
          had gone back 0.19 mm. It then stayed at -1.4 through a rapid of
          about 12 mm, which moved properly, until zeroed at 10:13. The user
          stalled X once on a fast move, which fits: the motor's count went
          about 1.4 mm further than the table. (Though lc.log gives that
          stretch's top speed as 40 mm/min, and didn't see the first 1.7 mm
          as moving, so the fast move may have started and stopped between
          two of lc's updates.) Stage 3's warning would have caught it at
          once. To do: lower X's rapid speed (`Axis1MaxMotorRpm`) or its
          acceleration (`Axis1RampingSpeed`); a stall at the start of a fast
          move is often the acceleration rather than the top speed
        - **Done (14 Nov 2026): more logging, to diagnose such things from
          lc.log.** Each key the controller acts on, with its command (e.g.
          "Key z g: Go to a Z position"); each axis starting (from where,
          where it was first seen moving, its target, and its speed) and
          stopping; a move which starts and stops between two updates (e.g.
          a nudge), as a change of position; zeroing, setting a position,
          and the start-up backlash take-up
        - X's backlash is now 1420 steps. **Tried (14 Nov 2026, later):**
          0 → -10 → -15 → 0 and 0 → 20 → -10 → 0: the motor, the scale and
          TouchDRO all agreed within 10 µm at every stop, and back at 0. So
          1420 looks right, as far as the scale (a count is about 10 µm) can
          tell
        - The DTI still read short: 19.93 for 20 and 9.96 for 10 (0.35-0.4%),
          against 0.6% the day before. A wrong counts per inch would be the
          same percentage every time; this varies with the setup, while the
          leadscrew (and gearing) and the scale, which share nothing, agree.
          So more likely the DTI: cosine error (5-6 degrees, hard to see by
          eye), or the DTI itself near the end of its travel. To settle it
          without the DTI: calipers between something on the table and
          something fixed, before and after a 100 mm move (0.4% would be
          0.4 mm); or check the DTI against a slip gauge or micrometer
          standard, plunger vertical in a stand
        - **The Pi locked solid once (14 Nov 2026)**, just after lc started
          a test: no keyboard, no other consoles, no SysRq; power cycled.
          lc.log stopped after the scale was lined up (the last few seconds
          of a log are lost in a hard lock). Main suspect: lc_mill.cfg
          doesn't pin the motor threads to cores (lc.cfg does), so a motor
          thread, real-time and spinning for a whole move, can land on
          core 0, where a Pi 3B handles all interrupts, USB included (the
          keyboard, Ethernet and the FTDI adapter). With real-time
          throttling off (`kernel.sched_rt_runtime_us=-1`), that would starve
          them completely; luck of the core would make it intermittent.
          Also possible: the Pi 3B's USB driver (known for hard locks), or
          power. To check: `cat /proc/sys/kernel/sched_rt_runtime_us`,
          `vcgencmd get_throttled`, ssh over Wi-Fi next time (not on USB),
          and a persistent journal (`sudo mkdir -p /var/log/journal`, then
          `journalctl -b -1` after a lock). **Checked:** throttling is on
          (950000), so a motor thread could only make things sluggish, not
          dead: starvation is unlikely. `throttled=0x0`, but only since the
          power cycle, so a dip at the time can't be ruled out. A lock which
          even SysRq can't get through points more at the kernel or hardware
          (e.g. the USB driver). Still unexplained: the persistent journal
          and ssh over Wi-Fi are for next time. **Done:** the mill's motor
          threads are now pinned as on the lathe (X on core 3, A on core 2,
          spinning when idle), to rule that out
        - **A second lock-up (14 Nov 2026)**, during a clean build over ssh
          with lc still running. This time REISUB (on the Pi's own
          keyboard) worked, so the kernel was alive, and on the lock before
          it the build had been "Killed" (the out-of-memory killer). htop
          during a later build showed memory high and a lot of swap. So
          probably out of memory: four compiles at once (`make` uses nproc)
          on a 1 GB Pi 3B, with lc's spinning motor threads holding cores 2
          and 3. Quit lc before building; if it happens again, build with
          `make release JOBS=2` (the user isn't convinced builds are usually
          a problem, so the Makefile is left as it is for now). The first
          lock-up (just after starting lc, no build) is still unexplained
        - **A third lock-up (15 Nov 2026), in two stages.** First lc
          ignored keys while its display still updated, and alt-tab still
          worked. Then, much later, the whole Pi locked solid: no ssh, and
          REISUB didn't work either, so it was power cycled.
          - **The lc part is explained and fixed.** With the lathe running,
            X's scale read garbage: jumps of up to ±4000 mm every few seconds
            for about 28 minutes, the same thing that makes TouchDRO need
            re-zeroing after using the lathe. lc stopped and asked, as it
            should. Then the dialog was answered with Y while the reading was
            still jumping, so X's position was set to 4451 mm, a garbage
            value. A new mismatch was found in the very next tick, before the
            controller had seen the first one cleared, so its "dialog shown"
            flag never reset. No dialog appeared, and every key except
            Esc, Space, u and Ctrl-Q was refused for good.
          - **The fixes.** The dialog is now shown once per ask (a count
            from the model), not once per flag. Y, re-basing and finishing
            moves only ever use a reading that has settled properly (flicker
            of a count is fine); if it hasn't, Y waits and asks again.
            `Axis1ScaleFaultMm` (5 mm on the mill) treats a jump at rest that
            the table can't have made as a misread: everything stops at
            once, and only the motor's position can be kept. Not yet tried
            on the machine. (A misread while moving isn't detected. The
            settle after the move would still catch a large difference.)
          - **The Pi's hard lock is still unexplained.** Not interference:
            the lathe wasn't running when lc's unresponsiveness turned into
            the hard lock (it happened while lc was idle, during the
            diagnosis). The common factor in all three lock-ups is the FTDI
            link and lc's DRO reader, added on 13 Nov: every lock-up has come
            since. The first was before the mill's motor threads were pinned,
            so the pinning isn't the common factor. The Pi 3B's USB driver
            (dwc_otg) is known for hard locks, and the sender streams about
            24 readings a second. A dead USB stack would also take the USB
            keyboard (so REISUB) with it, but ssh over Wi-Fi died too, and
            Wi-Fi isn't on USB. Next: stream the kernel log to another machine
            (`ssh lc-mill 'journalctl -kf'`) while lc runs; look for dwc_otg
            or ftdi_sio errors; and, if lock-ups stay this frequent, a spell
            with `Axis1Scale = none` to compare
          - **The persistent journal was never actually on.** Creating
            `/var/log/journal` isn't enough on Raspberry Pi OS, which ships
            a `Storage=volatile` setting. It took a drop-in,
            `/etc/systemd/journald.conf.d/99-persistent.conf`, containing
            `[Journal]`, `Storage=persistent` and `SyncIntervalSec=30s`, then
            `sudo systemd-tmpfiles --create --prefix /var/log/journal` and
            `sudo journalctl --flush`. It's on now (15 Nov 2026), so after
            the next lock-up, `journalctl -b -1` shows the end of the
            previous boot, losing at most about the last 30 s.
        - **A fourth lock-up (16 Nov 2026, 15:19)**, during a multi-pass,
          with X moving, about 20-30 s after lc started. The lathe wasn't
          running. lc had started three times that afternoon without trouble.
          Neither log shows the lock itself. The journal's last entry is lc's
          start, and lc.log's last line comes 18 s later (from before the
          multi-pass started). lc flushes each line, but only to the kernel,
          which writes it to the SD card some seconds later, so a hard lock
          loses the end of lc.log as well as the journal's last 30 s.
          There were no USB, memory or kernel warnings beforehand, so the
          lock was sudden. Like the first (just after lc started a test),
          it came with a motor moving, so motion is a suspect alongside the
          FTDI link. Also suspect: pigpio's DMA. pigpio uses DMA all the time
          (not only for moves), and a clash with the kernel over DMA channels
          can hard-lock a Pi. The mill's Pi 3B+ differs from the lathe's
          Pi 4 there, and the lathe hasn't locked up. A log on the SD card
          can't catch a sudden lock. Next: a serial console on GPIO 14/15 to
          another machine. It gets the kernel's last messages (e.g. an RCU
          stall or soft lockup report) without needing USB, the network or
          the SD card. Until then, stream lc.log and the kernel log over ssh
          (`tail -F lc.log`, `journalctl -kf`) during tests
        - **Decided (16 Nov 2026): the mill gets a Pi 4 (4 GB), as the
          lathe has.** This removes the 3B+'s USB driver (dwc_otg) and its
          DMA setup in one go, and the lathe's Pi 4 has never locked up with
          the same code and pigpio. It also gives the RAM for builds. (Not a
          Pi 5: pigpio doesn't support it.) Active heatsinks are ordered for
          both Pis (the 3B+ had hit its soft temperature limit), and the
          enclosures, which also hold the stepper drivers and PSUs, need
          fans. If the mill doesn't lock up on the Pi 4, put the cause down
          to the 3B+ and leave it there. If it does, the serial console is
          next
        - **Fitted (18 Nov 2026):** the Pi 4, with a fresh Bookworm on a
          new SD card, X11 rather than Wayland, and lc at
          `~/src/latheControl`. A clean `make release` was noticeably
          faster. X, A and the scales work as before. It has active cooling (a
          heatsink with a fan); the lathe's Pi is still to have its fitted. Still to do: watch for
          lock-ups over longer runs, repeat the multi-pass return stall
          test (under Multi-pass), and wire the enable pins and limit
          switches (GPIO11, GPIO16, GPIO2; see Motion and timing)
        - **No lock-ups in two or three hours of real machining on the Pi
          4 (19 Nov 2026)**, with the DRO reader, scales and multi-pass all
          in use. On the 3B+, at least one would have been expected in that
          time. Promising, but keep watching over longer runs before putting
          it down to the 3B+ for good
        - Nine of the settles hit the 3 s limit ("still changing"): the
          reading flickers at rest sometimes, so a settled value can be a
          count out
        - TouchDRO read 0.00 where lc's scale read -0.03: the residual
          backlash (returns to 0 read -2 to -4 counts after zeroing), and
          TouchDRO zeroed at a different moment. NB lc's X counts the
          opposite way to TouchDRO's
    - **This matters for stages 3 and 4:** a sudden jump in the difference
      looks exactly like lost steps, and "correcting" it would move the
      table by the jump. So a large difference must stop and warn, never be
      corrected. Better still, find the cause of the jumps
    - **Found with the scale (14 Nov 2026): the start-up backlash take-up
      never moved.** The first move after starting (+10) read 9.59 on the
      scale (and TouchDRO), and the 0.40 mm difference then stayed the same
      through a reversal and another move: the whole backlash, lost once.
      The take-up moves at start-up were asked for before the motors'
      speeds were set (in `Controller::run()`, after `Model::initialise()`),
      and a motor at zero speed cancels any move. So the motors were told
      the slack had been taken up when it hadn't, on both machines, probably
      since 2021: the first move after starting was short by the backlash
      whenever the last session had ended moving the other way. **Fixed:**
      `initialise()` now sets the speeds (the second presets) first. So the
      axes now really do move by their backlash at start-up, in the positive
      direction (mill: X 0.39 mm; lathe: Z away from the chuck, and X
      outwards), which matters if the tool is touching the work. **To check
      on both machines:** the axes move at start-up, and the first move
      after starting is right
    - **Settled with an edge finder (14 Nov 2026): the DTI was out, and
      2556 is right.** A metric "123" block, 80.00 mm long (Mitutoyo digital
      calipers), its two ends found along X:
        - Mechanical edge finder: TouchDRO 80.05, lc's scale 80.06, lc's X
          (the motor) 79.98
        - Electronic edge finder: TouchDRO 80.01, lc's scale 80.01, lc's X
          79.98
        - The DTI's 0.35-0.6% would have been 0.28-0.48 mm. Instead the
          scale is within a count, and the motor (so the leadscrew and
          gearing) within 0.025%: as fine as a 10 µm scale can calibrate.
          So 2556 and the conversion stay as they are
        - The motor read 79.98 both times, but the scale differed by 0.05
          between the two finders. lc.log shows why: while finding an edge
          with small nudges back and forth, the scale less the motor at rest
          wandered by about ±4-5 counts, depending on how the table last
          moved (e.g. +4.5 at one edge's nudges, -4.7 at another's, both
          after nudges to the right). So below about 0.05 mm the table
          doesn't follow the motor exactly, especially in small nudges after
          a reversal: elastic wind-up in the gearing and nut, or backlash
          which varies along the screw (1420 is an average). At that level
          the scale is the one to believe, which argues for stage 4 (go-to
          moves finished against the scale). Meanwhile, read the scale or
          TouchDRO, not lc's X, when edge-finding
    - **Still to check:** the cause of the lock;
      X's rapid speed or acceleration, so it doesn't stall; the lathe's Z
      scale's sign
    - **Done (14 Nov 2026), not yet tried on the machine: "Measure X's
      backlash (with its scale)"** in the System menu (only with a scale).
      After a dialog warns that X will move, X moves back and forth by 1 mm
      (or twice the backlash setting), away from the chuck first, six
      times, ending where it started. The compensation stays on: after each
      reversal (the first move only puts the slack on a known side), the
      shortfall of the scale's move against the motor's is backlash the
      setting doesn't cover, so the backlash is the setting plus the
      average shortfall (`BacklashMeasurement`, backlash.*). Chosen over
      turning the compensation off, which would leave the motor's idea of
      where the table is wrong if the slack started on the other side. A
      dialog gives the result in mm and steps, the shortfall each way, and
      how much they varied (warning if by more than three counts); lc.log
      has it too. The config isn't changed. Nothing else can move meanwhile;
      Space stops it. Each move waits for the scale to settle (so about
      20 s). `AxisScale` now counts settles, and treats a change of the
      motor's position between updates as a move.
    - **Tried on the mill (14 Nov 2026), three times:** 1013, 995 and 1008
      steps (about 0.31 mm), each very repeatable (its five reversals
      within a count of each other). So the command works, but the answer
      disagrees with 1420: with 1 mm moves the table went 1.133 mm for each
      1 mm of the motor, both ways, every time. Yet moves of 5-20 mm back
      and forth at 1420 (that morning, at a similar speed) all ended within
      ±2 counts, and 10 mm moves at 1260 showed about 0.05 mm too little.
      So the lost motion seems to depend on how far the table moves: about
      0.31 mm for 1 mm moves, about 0.44 mm for 5 mm or more. Either part
      of it is elastic wind-up (nut, gears, gib friction) which a short
      move never fully builds, so no one setting suits every length; or
      the scale reads unevenly within its 5.08 mm pattern (0.13 mm would be
      a lot for that). 1420 stays for now, as most real moves are longer.
      Next: repeat at another place on the table (if the answer changes,
      it's the scale), and measure at several distances (e.g. 1, 5 and
      10 mm) to see how the lost motion depends on the length of the move.
      If it's the mechanics, stage 4 (finishing moves against the scale)
      gets round it
    - **Repeated at other places (14 Nov 2026): it's where on the table,
      not the move's length.** The scale's raw counts (which zeroing
      doesn't change, and the sender stayed on all day) put every session
      on one line. Taking the morning's zero as 0, in lc's direction:
      -16 mm: 0.451 mm (1444 steps); about -20 to 0: the morning's 5-20 mm
      moves fitted 1420 to ±2 counts; +16 mm: 0.398 mm (1274); +94 mm:
      0.313 mm (1005). Each place repeated within a count or two. So the
      backlash falls steadily, by about 0.14 mm over 110 mm of travel. The
      steady trend rules out the scale (unevenness within its pattern
      would go up and down). Most likely a worn leadscrew or nut (a screw
      wears thinnest where it's used most). So no one setting suits all of
      X's travel: 1420 suits the worn end, and is 0.13 mm too much at the
      far end. Options:
        - Stage 4 (finishing positioning moves against the scale) doesn't
          care what the backlash is, and needs no absolute position: now
          clearly the best answer in software
        - Backlash depending on position (a map from a survey) would need
          the table's absolute position, which neither lc's zero nor the
          scale has after a restart (the sender's count starts again at
          power-up): it would need homing to a reference switch
        - Mechanical: if X's nut can be adjusted, take it up until it just
          binds at the least worn end
        - Meanwhile, set the backlash for where the work usually is
    - To make a survey along X easier: log the scale's raw count with each
      measurement (so results after different zeros can be placed along
      the table), and say in the result where it measured
    - **Decided (14 Nov 2026): rely on the scale.** How lc's X and the
      scale are kept in line:
        - lc's X stays the motor's count (smooth and immediate while moving;
          the scale lags), but it's re-based from the scale at rest: once
          the axis has stopped and the reading has settled, the motor's
          position is set to the scale's (nothing moves). Everything else
          (memories, go-to, nudges, multi-pass, gear cutting) keeps working
          in the motor's steps, which now mean where the table really is.
          Memories stay valid, as both share the zero
        - If they differ by more than a limit (config; perhaps 0.2 mm, about
          20 counts, against normal corrections under 0.15 mm), no re-base:
          stop and warn (stage 3), as something's wrong (a stall, a jump).
          Undecided: whether the warning also offers "accept the scale's
          position"
        - Not re-based during a running multi-pass or threading (between
          passes), only once it's finished. Without a reading, carry on
          with the motor alone, showing that the position isn't confirmed
        - Then stage 4: positioning moves (go to memory, zero, a position,
          relative moves) settle, and if out by more than about 2 counts,
          correct (at most two or three times), never reversing into the
          work (back off past the backlash and approach again), then
          re-base. Can't tell a cut from a positioning move (both are "go
          to memory"), so it can be switched off from the menu
        - Milling a pocket (cut to a memory, move Y by hand, cut back): the
          re-base at each end means the return cut goes to the real
          position, so errors don't build up from pass to pass. Stage 4
          adds a dwell of about 2 s at each end wall, and finishes the wall
          at the memory position (a small move on, in the cut's direction;
          or if past it, back along the cut path and in again). Without it,
          X's varying backlash could leave the ends up to about 0.1 mm out
        - Order: re-base at rest with the warning first, then stage 4
    - **Done (14 Nov 2026), re-base at rest with the warning (stages 3
      and "2½"), not yet tried on the machine.** `Axis1ScaleWarnMm` (0 =
      off, the default; the mill 0.2) and `Axis1ScaleRebase` (off by
      default; on for the mill; the lathe's Z scale's sign is still to be
      checked first). Each time the scale settles: further apart than the
      limit, everything stops and a dialog asks which to believe (Y/Enter:
      the scale's position; N/Esc: keep lc's, and line the scale up with it
      again), nothing moving until answered; otherwise, a count or more
      apart, the motor's position is set from the scale's
      (`StepperMotor::setPosition()`: nothing moves, and the backlash's side
      is unchanged), in no mode or gear cutting, not part way through a
      multi-pass or motion sequence, nor while measuring the backlash.
      lc.log has each re-base. The FAKE build's simulated scale now follows
      the motor's actual steps (a table with no backlash), so re-basing
      doesn't move it too. Not done: marking the position as unconfirmed
      when the scale has no reading
    - **Tried on the mill (14 Nov 2026): works.** lc and TouchDRO now agree.
      Stalling X on a fast move brought up the dialog, as expected
    - **X's table lock had been on all along** (left on from the last
      project), so every test of X so far dragged the table against it,
      the stepper doing remarkably well. That wound up the nut and gears
      before the table moved, which looked like extra backlash. With the
      lock off, the backlash measurement gave 787 steps at the far left of
      the table, 660 in the middle and 1018 at the right (0.21-0.32 mm). So
      the 1420 (and the "worn leadscrew" idea, from a range of 0.31-0.45 mm)
      came mostly from the lock; the range that's left, least in the
      middle, isn't what wear would usually give (uneven gib tightness?).
      X's backlash is now 820 steps, the middle of the range (out by about
      0.06 mm at the ends, which re-basing and stage 4 take care of). The
      stalls may well have been the lock too: see whether they still
      happen before lowering X's rapid speed. The counts per inch (2556)
      stands: the edge finder compared the scale with the block, which the
      lock doesn't affect
    - **Done (14 Nov 2026): watching the scale at rest too.** The user saw
      TouchDRO wander a few tens of microns after X had stopped, while
      lc's re-based position stayed put, as it only re-based once per
      stop. Now, at rest, a change of 2 counts or more from where it last
      settled (`AxisScale::REST_CHANGE_COUNTS`; one count is flicker)
      settles again as at the end of a move, so the position follows it
      (or, beyond `Axis1ScaleWarnMm`, everything stops and asks). Logged as
      "X scale's reading changed at rest by ..., n s after the axis
      stopped". **To check from lc.log:** if the changes mostly come a few
      seconds after stopping, it's the sender's smoothing catching up late
      (a longer `Axis1ScaleSettleMs` would be cleaner); if at random times,
      or while cutting, the table really moves (springiness, cutting
      forces, knocks)
    - **Done (14 Nov 2026), stage 4, not yet tried on the machine:
      finishing positioning moves on the scale.** `Axis1ScaleFinishMoves`
      (off by default; on for the mill) sets how it starts; the menu's
      System category toggles it ("Finishing on the scale is ON (X)").
      After `axis1GoToPosition()` (typed go to, relative moves, `L`, the
      feature's go-tos) or `axis1GoToCurrentMemory()`, in no mode or gear
      cutting and not in a multi-pass: at each settle (after the re-base),
      within 1 count of the target it's done; short, a move on; past, back
      off by twice the backlash setting (at least 0.5 mm) and approach
      again, so always from the same side. At most three corrections, then
      a warning. Keys wait meanwhile (as for a nudge); Space stops it. Not
      the fast return (decided: it only gets things out of the way, and
      stalls at speed). lc.log has each correction and how it ended.
      **To try on the mill:** go-tos landing within a count of the target
      at both ends of the table; a pocket (cut to a memory, Y by hand, cut
      back) with it on and off
    - **Found (14 Nov 2026): X's pulley was slipping** on the stepper's
      shaft (perhaps from all that running against the lock). Tightening
      its two grub screws made repeatability far better. The backlash
      measurements before then may have included some slip: worth
      measuring again
    - **Found and fixed (14 Nov 2026): rapids stopping and starting.** A
      held Alt+arrow at 800 mm/min often seemed to stall, the speed shown
      dipping, while key 5 then an arrow at the same speed was fine. lc.log
      showed why: during one hold, "Key Space" then "Key Alt+Left" 50 ms
      later, about once a second. Under X11 a held key repeats as a release
      and a press, and Qt marks them as a repeat only if the press has
      already arrived when it sees the release; on a busy Pi it sometimes
      hadn't, so the release stopped the rapid dead (the dead man) and the
      press started it again. Fixed in the view (`RAPID_RELEASE_MS`): a
      release of a rapid's arrow stops it 60 ms later, unless the same key
      is pressed again meanwhile (a repeat); and while the arrow's held,
      its presses are repeats however marked, so letting go of Alt (which
      still stops at once) can't let a repeat start a jog. Releasing a
      rapid now stops 60 ms later (about 0.8 mm at 800 mm/min); Space is
      still immediate. **To try on the mill:** held rapids should run
      smoothly. The stop-start may also explain the earlier "stalls" on
      fast moves, so see whether any are left before lowering X's speed
    - **Also seen then: stops at speed overshoot.** Space stops from
      800 mm/min (key 5 then an arrow) left the table 0.27-0.38 mm beyond
      the motor in the direction of travel, bringing up the "disagree"
      dialog (more than 0.2 mm). The motor stops dead (only acceleration is
      ramped; see "Deceleration ramping" under Motion and timing, where
      stops the user makes are to stay instant), and the table carries on
      into the slack. Possibly worse with the pulley slipping: to check
      again now it's tight
    - **Checked, with the pulley tight and the rapids fixed (14 Nov 2026):**
      held rapids now run smoothly. Every stop from 800 mm/min still left
      the table ahead of the motor, the way it was moving: 0.12, 0.23, 0.26
      and 0.29 mm, so within the backlash (820 steps, 0.256 mm) plus a
      little. **Fixed by making the warning one-sided:** ahead of the motor
      in the direction of the last move (`AxisScale::lastMoveDirection()`),
      `Axis1ScaleWarnMm` plus the backlash setting is allowed, and re-based
      quietly; behind it (a stall can only leave it behind), or after a
      change at rest, the limit is as before. The dialog gives the limit
      which applied
    - **Still to try on the mill:** positions after moves should now agree with
      TouchDRO (within a count, allowing for the 2 s to settle); the "off
      by" figure should be about 0 at rest; lc.log's "re-based" lines show
      each correction (expect up to about 0.13 mm at the least worn end,
      with 1420). And, to see the warning: with the table's lock on, a short,
      slow move (so the motor stalls harmlessly), then the lock off
    - (The original idea:) a "measure backlash" command (System menu),
      which reverses
      the axis, sees how far it turns before the scale moves, and suggests
      the setting
    - **Stage 4, as discussed (13 Nov 2026):** positioning moves (go to a
      memory, go to zero, go to a typed position) stop short of the target
      by a configurable approach distance, then finish in the same
      direction, so the slack is taken up. Then: wait for the reading to
      settle, read the error, move by it, and repeat (two or three times at
      most), then re-base the motor's position from the scale. Notes:
        - Not "crawl in while watching the scale": the sender's smoothing
          lags, so to stop within a count the crawl would need to be about
          10-20 µm/s, which would take about a minute over 1 mm. The
          lathe's quadrature scale has about 1 ms of lag, so could watch,
          but one routine (with a shorter settle time) covers both
        - Never reverse for a small correction: back off past the backlash
          and come in again
        - A count is about 32 of mill X's steps, and readings jitter by a
          count, so only correct an error of more than about 2 counts;
          otherwise just re-base
        - Re-base only after the final approach, when the slack is known to
          be taken up, without upsetting the backlash compensation's
          direction. From then on the scale is the true position, so
          memories and targets are really scale positions
        - A large first error (a stall, lost steps, a slipping scale or a
          stale reading), or one which doesn't shrink, stops and warns, as
          stage 3 does, rather than being corrected silently
        - Not for cutting feeds (multi-pass, repeated cuts): stopping
          mid-cut would mark the work. Check, and warn, at rest afterwards
          instead. Nudges and jogs are left alone. A quick way to turn it
          off per axis (e.g. in the menu), for when speed matters more
        - A state machine stepped from `checkStatus()` (as motionsequence.*
          is), so Space cancels it and the menu and dialogs never block it,
          with nothing added to the motor threads. If the scale goes stale,
          fall back to dead reckoning with a warning
        - Climb milling can pull the table into the backlash after it's
          been positioned; stage 3's check at rest would catch that, and
          locking the table is the real fix
        - If the sender's adaptive average proves a nuisance, change the
          sketch to send unsmoothed values as well
    - Later: Y and Z as read-only (display-only) axes from the same reader,
      each with its own zero. Z's depth would help gear cutting, where the
      depth is fed by hand

### Work geometry and tools (lathe)

#### Current feature

Background: "set diameter" currently means only that **X is measured from the
spindle centreline** (`m_xDiameterSet`). The diameter shown is the *tool's*
position (2 × |X|), so it changes whenever X moves. Knowing the centreline is
the valuable part. Once we have it, remembering where the *work* is costs very
little: we store numbers in axis coordinates, which moving the axes doesn't
change.

- **Current feature: a remembered diameter and end Z.**
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
    - **The feature's other end.** Save where the feature ends along Z
      (e.g. a shoulder), as well as the work's end: set from a key, or by a
      multi-pass from the work's end (its cut's end). Then it could be gone
      to as well, offered as a multi-pass's Z end, and shown in the box. A
      step towards "Single feature vs many" (a list of segments)
    - Still to do from below: the warning when the tool goes inside the Ø
      with the spindle running, and
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
          synchronisation. Tool nose radius starts to matter here too.
          **Done** for a radius on the end, as a chain of tapers in the
          multi-pass engine: see "Radius mode" under Multi-pass. And for a
          fillet at a shoulder: see "A filleted shoulder" there
    - **Suggested first slice:** feature Ø and end Z with set and clear, their
      own display box, cleared on re-zero, fault or tool change, and used to
      pre-fill multi-pass

#### Tool library

- **Current tool and a tool database.** The program has no idea when the
      tool is changed, but the feature values above belong to the tool that
      touched off. A different tool will be out by its offset.
    - **Done (2026-11-16), in the mock build only:** a first slice, on with
      `ToolLibrary = true` (lc.cfg), in `toollibrary.*`. A "Tool library"
      menu category (`T` in the menu) and a "Tool" box at the top right.
      `T` changes tool (a list of the tools), shifting Z and X by the
      difference between the two tools' offsets. "Define a new tool"
      measures one from the tool in use by scratch tests: the tool in use
      touches the diameter and the end (`Enter` after each), then the new
      tool does the same, and it asks for the description and nose radius.
      Also "Measure a tool again" and "Describe the current tool". Kept in
      `ToolLibraryFile` (tools.cfg, not in git), plain text, written
      through a temporary file and a rename, keeping the one before as .bak.
      The radius dialogs start with the current tool's nose radius.
      **Tried on the lathe (2026-11-16):** two tools, measured by scratch
      tests; changing between them was very repeatable
        - **Decision: offsets, not datums, are stored.** A tool change is a
          re-base (nothing moves): each motor's step count shifts by the
          difference, in whole steps per tool, so going to a tool and back
          always comes back to the same step. As memory slots, breadcrumbs,
          the feature and multi-pass setups are all positions on the work,
          they carry over to the new tool's tip, and nothing is cleared (so
          "a tool change clears the feature", above, isn't needed)
        - **Decision: scratch tests first, not a dial indicator.** Each
          tool touches the same diameter and end, as the user already does
          to set up a job. A dial indicator could be added later as another
          way to measure
        - **Decision: measured from the tool in use, not only tool 1.** The
          offsets are still stored from tool 1, but any tool in the library
          can be the one measured from. So tool 1 can be measured again
          from another tool (e.g. after a new insert), which moves every
          offset so they're from its new tip, rather than every tool having
          to be measured again
        - **Decision: external tools only, for now.** The touches are on the
          outside diameter, which a boring bar can't reach: the dialog says
          so, and measuring is refused with a boring retraction. Each
          diameter touch must have X moving in, and each end touch Z
          moving towards the chuck (the motor's last direction), or it's
          refused, so the backlash is taken up the same way for every tool
        - **Decision: measuring isn't a mode.** The axes move as usual
          (keys, nudges, speeds, the gamepad), but nothing else: no leader
          keys, zeros, position sets, memories or modes. `Enter` records
          each touch and `Esc` cancels. A tool change is refused in modes
          other than multi-pass and radius (whose passes are on the work,
          so carry over), and during a multi-pass
        - **Done (2026-11-16):** an unknown tool (`T?`, last in `T`'s list),
          for one not in the library. **Decision:** each axis remembers whose
          tip its positions read (the tool in use when it was last zeroed,
          set or changed to). Changing to the unknown tool shifts nothing, so
          they still read the last tool's ("positions: T1" in the box), and
          changing back to a tool in the library shifts from that tool's
          offsets: nothing set on the work is lost, e.g. on the way to a
          knurling tool and back. Only an axis zeroed or set while the
          unknown tool was in use can't be shifted: changing to a tool in
          the library then clears what was set along it (the work's end, or
          the diameter), asking first if it's part of the feature. Memory
          slots are kept, as they were before there were tools
        - Still to do: tip orientation, tool types, widths (parting and
          grooving), invalidating offsets ("toolpost moved", a tool
          adjusted), deleting a tool, and boring bars (touched on a bore's
          diameter instead). Also, the nose radius could correct tapers and
          chamfers
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

### Structure and refactoring

#### Gear cutting and stepperControl

- Incorporate electronicRotaryTable repo
    - Pull functionality of ERT into this repo. ERT is designed specifically
      for cutting gears, so we could have a mode for gear cutting.
    - The old ERT was a text/ncurses-based UI so will need updating to Qt.
    - This would require a set-up screen to ensure the user has connected a
      rotary table, replacing one axis.
    - This would mean we could also incorporate the repo stepperControl as it
      is only shared by latheControl and ERT (which would then be redundant)
    - **Done (2026-11-13):** stepperControl's code (v1.5.1) is now in this
      repo, flat in the root like everything else (`steppermotor.*`,
      `igpio.h`, `gpio.*`, `mockgpio.h`), with no FetchContent and no
      `STEPPERCONTROL_DIR`. A plain copy: its history stays in the
      stepperControl repo. It's now built with this project's warnings
      (`-Werror -Wpedantic`), which it wasn't before: clean with clang on
      the Mac, and a clean release build with GCC on the workshop Pi
      (2026-11-13)
    - Gear cutting mode should be disabled by default, and only enabled by a
      setting in lc_mill.cfg (the milling machine currently only has a stepper
      on its X axis)
    - lc_rt.cfg is redundant and should be removed.
    - As part of this change, maybe the config file should have an "axis[n]Capabilities"
      config item, to allow the program to determine which features are available for
      an axis (e.g. retract, or, in the case of a rotary table, gear cutting and so on.
    - **Done (2026-11-13), in the mock build only:** gear cutting mode (Modes,
      "Gear cutting", `C`), on with `GearCutting = true` (lc_mill.cfg), and
      only if axis 2 is in degrees. lc_rt.cfg is removed, and the mill's
      rotary table is now labelled A (it stands vertically, turning about X;
      its leader key is still `c`, as `a` is X's nudge). The dialog takes the
      module and teeth (showing the blank Ø, the angle per tooth and the
      cutter number), X's start and end, the depth (2.157 × module, as ERT
      used, unless typed) and the depth per pass. It runs on the multi-pass
      engine, which gained an index axis (`MultiPassPlan::indexPerStart`):
      the teeth are its starts, and A turns on by a tooth before each.
        - **Decision: every tooth at one depth, then feed in.** The depth
          (Y or Z on the mill) has no motor, so it's fed by hand, once per
          turn of the blank. Taking each tooth to full depth first would need
          winding the depth in and back out for every tooth
        - **Decision: automated only.** ERT's "take the cut by hand" (and
          its `b` to step back a tooth) isn't offered: X has a motor. The
          return is back through the slot at full speed, as the depth can't
          retract
        - **Decision: A only ever turns forwards.** Each pass carries on
          round rather than going back, so the table's backlash stays taken
          up the same way. For the first tooth, A backs off 1° and comes
          forwards (as ERT took up the backlash at the start). At the end, A
          turns on to the first tooth again, and its position is put back to
          where it started (e.g. 0°); memory slots are left as they are, as
          whole turns don't move them on the table
        - **To check on the machine:** that A's backlash compensation
          (`Axis2BacklashCompensationSteps`) and the 1° back-off land the
          first tooth where the others are; whether A indexes well at
          full speed (`Axis2MaxMotorRpm`, ramped), with the blank on; whether dragging the cutter back
          through the slot marks it (if so, a "return at cutting speed"
          option, or a pause to wind the depth out, may help)
        - Stopped and started again (e.g. stopped while A turns), X goes
          to the start first and only then A turns forwards to the next
          tooth, so the teeth stay evenly spaced. They're kept until the
          number of teeth changes, or A is zeroed or set
        - Not done yet: stepping back a tooth (re-cutting one), a pause
          between teeth as a dialog option (`Enter` during a pass does it
          once)

#### Flaky unit tests

- **Unit tests which fail now and then** (2026-11-11). On neutron
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
- **More unit tests which fail now and then, on slower machines**
      (2026-11-14, on an M2 MacBook, while m4pro is away for repair; they
      passed on m4pro, which is much faster). Each failed about one run in
      three or four, and passed on re-running, with or without the change
      being tested:
    - "Threading: Z's backlash is taken up before a pass reverses"
      (test.cpp, `pulsesFromWholeRev(started) <= 5`: a pass started more
      than 5 encoder pulses from a whole revolution)
    - The spring passes test (test.cpp, `getWarning() == "Stop the motors
      first"` after `axis1GoToPosition(zStart - 0.5)`)
    - Neither failed on the M2 when it wasn't busy (five runs of the whole
      suite). With ten busy processes on its eight cores, the spring passes
      test failed five runs in six, so that's the way to reproduce them
    - **Spring passes: not a move finishing too soon, as first thought.** In
      threading mode, a Z move is a threading pass: Z's backlash is taken
      up by one step, then the pass starts on a later `checkStatus()`, and
      waits for the spindle. The test polled for Z running (without calling
      `checkStatus()`), which only worked if it caught the one step while
      it was being made. On a busy machine, the step was over between
      polls, so the pass never started (or, if the step was caught,
      `multiPassGo()` could come once it was done). **Fixed:** the test runs
      the updates until the pass has started, as `startPass()` does
      (`waitForPassToStart()`). It passed eight runs in eight under the
      same load
    - **Pass starts:** `pulsesWhenZStarts()` read the encoder's count once
      it saw Z move, polling every 0.1 ms. A wake-up 1.5 ms late is 5
      pulses at the tests' spindle speed. **Changed:** it takes readings for
      40 ms after Z starts, each working out the start from the count and
      how far Z has gone, and keeps the earliest. Both of the errors (the
      test's thread waking late, and Z lagging the spindle for a moment)
      only make a start look later, and a pass can't start early. On an
      idle machine they come out 0 or 1 pulses from a whole revolution
    - Under that much load, the threading tests still fail now and then
      (old and new versions alike, about one run in eight): the mock's
      encoder and motor threads are starved too, and then Z really does
      fall behind (e.g. 12 steps in "Z follows the spindle's position").
      The tests can't allow for that, and the Pi's motor threads are
      real-time and pinned to their own cores
    - **Not starved after all (2026-11-15).** With a `yes` on each of the
      M2's eight cores, the threading tests failed in 14 runs out of 15
      (none in 15 when idle), always with the pass starting 6 to 8 pulses
      *early*, which the motors can't cause. Z follows the spindle's
      position, which runs on past the last pulse by up to 2 ms (5 pulses
      at the tests' speed), and the tests measured it against the pulse
      count. When the mock encoder's thread runs late, the count falls
      behind and the position runs on ahead of it, so Z looked early.
      **Fixed:** the tests measure against the position
      (`Model::getSpindlePosition()`), which is what Z follows. The model
      was right all along. The two multi-pass tests also keep the earliest
      reading during a cut, rather than the first, as a reading can only
      come out late
    - Under that load, the suite still failed in 3 of 5 runs afterwards. Each
      time the mock's threads really were starved: Z 11 and 40 steps behind
      in "Z follows the spindle's position", and once the encoder's thread
      held up for over 100 ms, so it lost pulses. Those can't happen on the
      Pi (its threads are real-time). Eight `yes` processes plus the tests'
      spinning motor threads is more than the M2 has cores, so it's a
      harsher test than a busy desktop
    - **`[timing]` tests are retried (2026-11-16).** A one-off failure in the
      menu-only change's first `make test` run (the test wasn't noted) cost
      several full re-runs. Ten runs of the whole suite with six `yes`
      processes on the M2 all passed, so which tests fail now and then
      can't be found by running them. Instead, the 13 tests whose results
      depend on the mock spindle's and motors' threads keeping pace (the
      ones that lock Z to the spindle, and the threading and knurling
      passes) are tagged `[timing]`. Tests that only wait for a move to end
      (with a generous deadline), or that use a fake clock, aren't.
      `make test` runs the rest first, then each `[timing]` test on its own
      (test/run_timing_tests.sh), retried up to three times, and lists any
      that only passed on a retry as flaky. `make test-quick` leaves them
      out. A test that isn't tagged and fails now and then should be noted
      by name, then fixed or tagged

#### libgpiod and a newer Pi OS

- **Try the latest Raspberry Pi OS, with libgpiod instead of pigpio.**
      Both Pis are held on Bookworm, as later releases are believed to have
      dropped pigpio. That also holds the Pi builds to Bookworm's compiler
      and Qt (GCC 12 and Qt 6.4, as far as is known), which is why the code
      uses `fmt::format` rather than `std::format`, and must avoid newer
      C++23 library features and Qt APIs even though they build on the Mac.
    - **Plan:** on a spare SD card (so the working one is untouched),
      install the latest Raspberry Pi OS, and try a branch using libgpiod
    - **No hurry:** Bookworm (Debian 12) has long-term support until about
      mid-2028, so this can be done carefully, a step at a time
    - **The GPIO library first, then the OS.** libgpiod is in Bookworm too
      (1.6, as far as is known; v2 came later), so a libgpiod `IGpio` can be
      tried on the same SD card and hardware, chosen at build time, and
      compared with pigpio directly. Once it works, the OS change is mostly a
      rebuild. (Written against 1.6, it would need moving to v2's API for
      the latest OS, but that's a small amount of code)
    - **The mill first.** Its Pi only steps motors: it has no input pins
      wired (2026-11-16), its scales come over serial, and it has no
      spindle encoder. So it tries the stepping without the hard part (the
      inputs with pull-ups are only tried on the lathe). If that's
      good, the lathe's encoder is the only new risk left, and can be
      measured on its own
    - **What it has to replace:** only `gpio.cpp` uses pigpio, behind
      `IGpio` (igpio.h), so nothing else should change. It uses pins as
      outputs (`gpioSetMode`, `gpioWrite`: step, direction, enable), inputs
      with pull-ups (`gpioSetPullUpDown`, `gpioRead`: alarm, limit
      switches), microsecond delays and ticks (`gpioDelay`, `gpioTick`, for
      the motor threads' step timing), and edge callbacks with timestamps
      (`gpioSetAlertFuncEx`: the spindle encoder and the Z scale). libgpiod
      (v2) covers each of these: bias settings for the pull-ups, and edge
      events with kernel timestamps, read from a thread of our own
    - **The risk is the edge events.** pigpio samples the pins by DMA (every
      5 µs by default) and delivers callbacks in batches; libgpiod's events
      come from interrupts, read by a thread, which must keep up with the
      encoder at the highest spindle speed and not lose edges. It would
      need its own core, or to share one carefully, as the motor threads
      are pinned (see the motor-thread pinning notes under "Motion and
      timing"). Measure lost pulses with threading at speed before
      trusting it, as that's what showed up the pinning problem before
    - **Also check:** stepping a motor with `gpiod` line writes is fast
      enough (it goes through the kernel, unlike pigpio's direct register
      writes), and the program no longer needs root (pigpio needs it;
      libgpiod only needs access to /dev/gpiochip*), which would change how
      it's started
    - Would free the OS, the compiler (`std::format` and newer C++23) and Qt
      to move on

#### An array of axes

- **An array of axes, with manual axes (the mill's Y and Z).** Replace
      the hard-coded "Axis1..." and "Axis2..." functions and members with an
      array of axes, so the mill can have four: X (stepper), A (the rotary
      table, stepper), and Y and Z (moved by hand, read from their scales).
      Some behaviour only applies to some axes (e.g. retracting is only for
      the lathe's X, not its Z), so each axis says what it can do, rather
      than every axis having every feature.
    - **Decided (18 Nov 2026), from planning the mill's slot and
      face/pocket modes (see "Slot and face/pocket modes" under
      Multi-pass):**
        - Each axis has a kind: `stepper` or `manual`. A manual axis's
          position is its scale's reading plus an offset, so it must have a
          scale (refused at start-up without one). The mill's DRO sender
          already sends Y and Z over the same FTDI stream as X; only X is
          used so far
        - A manual axis behaves like a DRO line: shown on the main display
          all the time, with zero, set (type a value) and half (set it to
          half its reading, for finding a centre between two edges; the
          user uses this on TouchDRO all the time). Half is for the motor
          axes too. Each axis has its own leader key (check for clashes:
          `a` is already X's nudge on the mill)
        - A go-to on a manual axis is a "distance to go" prompt, not a
          motor move: e.g. "Move Y to 12.500: 0.340 to go", updated live as
          the user winds the handle. It's done by itself once the scale has
          settled within a count or two of the target, and says if it went
          past. It's in the status line or panel, not a modal dialog, so
          nothing blocks; Space (or Esc) cancels it. Moves and nudges on a
          manual axis say why they can't, rather than doing nothing
        - A go-to involving a motor axis and a manual axis runs both
          together (e.g. X moves while the user winds Y), and is done once
          both have arrived. This would make bolt-hole circles possible
          later (a list of go-to targets), so TouchDRO would only be needed
          rarely, if at all
        - Memory slots store every axis. Going to a slot moves (or prompts
          for) the cut-plane axes only: X, Y and A. Z is stored and shown,
          but is never part of a slot's go-to (the user won't move Z while
          X is moving). Z will never have a stepper: the CNC mill is
          for anything that needs full automation
        - The plan is to fit a stepper to Y later. That should then be a
          config change (`manual` to `stepper`, plus its pins, conversion
          and core). Mind the cores: X, A and Y would each have a pinned
          core, leaving one for the UI and the OS on a Pi 4. A fourth motor
          wouldn't fit without rethinking the pinning
        - The lathe stays at two axes (no topslide scale: the user never
          uses the topslide)
        - A scale on the lathe's X (axis 2) may come later (the user,
          19 Nov 2026). That should then be a config change too, so the
          scale handling (lining up, the mismatch check and dialog,
          re-basing at rest and by hand, finishing moves, backlash
          measurement, the display) wants to belong to any axis, not
          axis 1 alone as now. A quadrature scale on X would also need a
          second pin callback in the GPIO layer (only
          `setLinearScaleAxis1Callback()` exists) and two free pins
    - **Plan, in stages, each tested and working before the next:**
        1. The axis abstraction, with the new manual axes built on it from
           the start: config, calibration (Y and Z probably need their own
           counts per mm, as X did), the DRO lines, zero, set, half, the
           go-to prompt, and memory slots for every axis
        2. Move X and A (and the lathe's Z and X) onto it, one area at a
           time (display, memory, nudges, go-tos, scale), with no change of
           behaviour, so lc.cfg and lc_mill.cfg keep working with the same
           keys
        3. The slot and face/pocket modes on top of it (see Multi-pass)
    - The `Axis1*` / `Axis2*` config keys stay as they are; the new axes
      are `Axis3*` and `Axis4*` on the mill
    - **Done (18 Nov 2026), the first part of stage 1, not yet tried on the
      mill:** Y and Z as DRO lines, with zero, set and half. Not yet (see
      below for those, done since) the go-to prompt, memory slots, or a
      settled/steady reading (which the
      go-to prompt and the modes will need)
        - `Axis3Kind` / `Axis4Kind` = manual (none by default), with a label,
          a leader key (none by default; y and z on the mill), and the same
          scale keys as axis 1's (a manual axis without a scale is refused).
          The scales share axis 1's DRO reader (`makeScale()`, split out of
          `configureAxis1Scale()`)
        - `ManualAxis` (manualaxis.*): the scale's reading plus an offset,
          which zero, set and half change. It starts at zero (the 
          choice: re-zero happens at each start anyway, as on TouchDRO, so
          showing the scale's own reading first was no use). Its zero isn't
          kept when lc stops, and if the readings are lost (the sender's
          count may have started again), it starts at zero again when they
          return
        - The display: a line each below X and A, at 45 px rather than 60 so
          two fit where the spindle speed would be, with "no reading" when
          there isn't one. X's scale line moved up to X's row, after its
          speed, shortened to e.g. "scale 12.345 (+0.010)" (the units are
          X's); the mode's name, and the lathe's taper angle and RETRACTED
          below it, moved down a line to make room
        - Half for the motorised axes too (`XH`, etc.): as setting the
          position, it clears that axis's memory slots and asks before
          clearing the current feature. Not while the axis is moving
        - **Tried on the mill (19 Nov 2026): halving did nothing on any
          axis.** The controller checked the key against the mode before
          adding the leader to it, so after `y` it saw a plain `h`, the
          quick chamfer, which lc_mill.cfg's DisabledModes turns off. Fixed:
          the key after a leader is checked as the leader's key (`a3_h`), as
          the menu already did. The same fault meant `z b` (re-base) while
          a multi-pass was paused never got through either (`z` on its own
          wasn't allowed then), so axis 1's leader is now allowed then too.
          Space after a leader is now plain Space (it was the leader's
          Space, which stopped everything but didn't cancel a manual go-to),
          and Esc after a leader just cancels it (it stopped everything)
        - Not allowed during a multi-pass (nor are any other keys but a
          few), though zeroing a manual axis moves nothing. The slot and
          face/pocket modes may want Z zeroed at a pause
    - **Seen on the mill (18 Nov 2026):** Z's scale counts up as the tool
      goes down, so `Axis4ScaleReversed` is now true, following the usual
      convention that Z goes negative towards the work
      Y left as it was: it increases "towards the column", as wanted
      (checked the same day)
    - **To try on the mill:** each scale's counts per mm (the nominal 2560
      per inch so far: calibrate against a known move, as for X), and that
      the y and z leader keys don't clash with anything in use
    - **Used on the mill (19 Nov 2026)** for a whole job (a mounting
      bracket), without TouchDRO: Y and Z zero and set worked as expected
      (half not needed yet). The workflow for Z: bring the tool down
      to the work, set Z to the work's current thickness, then machine down
      to the thickness wanted by Z's reading. The part came out within a
      few tens of microns of the plan in each dimension, so the nominal
      counts per mm look right, though they haven't been calibrated against
      a known move yet. No clashes with the y and z leader keys seen
    - **Done (19 Nov 2026), the rest of stage 1, not yet tried on the
      mill:** memory slots for the manual axes, and going to them by hand.
      `M` stores every axis; a manual axis's leader then `m`, `Enter` and
      `Delete` store, go to and clear it alone. Going to a slot shows the
      distance to wind on the axis's own line ("to 12.50: 0.34 to go", red
      once past), and ends once the reading has settled within two counts
      of the target (`ManualAxis::isSettled()`: within a count, as it
      flickers, for `Axis3ScaleSettleMs`, 1500 on the mill). Nothing blocks:
      `\` then `Enter` moves X and A while Y is wound, and Space or Esc
      cancels. `Axis4InSlotGoTo = false` leaves the mill's Z out of `\`
      then `Enter`, as decided (z then Enter still goes to Z's alone).
      Zeroing, setting or halving a manual axis clears its slots, as for the
      motorised axes (positions from the old zero would be somewhere else),
      as does losing its readings. On the mill, the memory rows are a
      little smaller, to fit Y's and Z's above the mode's lines. Plain
      Enter still goes to axis 1's slot alone, as before. A manual axis's
      leader then `i` types a value into its slot, as `zi` and `xi` do
      (22 Nov 2026); it needn't have a reading. Not done: a sound
      when a manual go-to arrives (the line just clears), and the multi-pass
      dialog doesn't use the manual axes' slots yet (stage 3). Stage 2
      (moving X and A onto the same abstraction) is next
    - **Done (19 Nov 2026), the first part of stage 2:** memory slots,
      zero, set and half, and the display, with no change of behaviour.
      The Model has calls taking an axis number (1 to `Model::AXES`, 4)
      which work the same for every axis: `hasAxis()`, `axisLabel()`
      (which replaces the 75 `read("Axis1Label", "Z")`s), `getMemory()`,
      `storePosition()`, `goToCurrentMemory()`, `clearCurrentMemorySlot()`,
      `clearMemory()`, `inSlotGoTo()`, `zero()`, `setPosition()`,
      `half()` and `formatPosition()`. The `axis1...`/`axis2...`/
      `manualAxis...` versions of these are now private, behind them.
      Every axis's slots are in one array, in mm (a motorised axis's are
      whole steps, so going there and "already there" are exact), rather
      than the motorised axes' in steps with a sentinel for "unset". The
      controller's keys for these act on `key::axisOfKey()` rather than a
      case per axis, and the view has one list of axis lines and one of
      memory rows (the motorised and manual axes differ only in what they
      show beside the position: speed, or "no reading" and the go-to)
        - A bug fixed on the way: a memory value typed in (`i`) was
          truncated to a step, not rounded, so 0.7 mm at 1/1000 mm a step
          was stored as 699 steps (0.699 mm). Now it's the nearest step
        - **Then (19 Nov 2026), the motorised axes' state:** the dozen
          pairs of members (`m_axis1Status`/`m_axis2Status`, fast
          returning, rapid in progress, previous speed, was running, the
          motor log, the positions' tool, the last relative move, the
          breadcrumbs, faulted) are one `MotorAxisState` per axis, in
          `m_motorAxes`, through `axisState(n)`. The calls which differed
          only by axis take its number: `isMotorRunning()`,
          `motorSpeed()`, `setMotorSpeed()` (which also replaces
          `axis1SetSpeed()`), `waitForMotor()`, `motorStep()`,
          `motorPosition()`, `axisStatus()`, `goToPosition()`,
          `goToOffset()`, `repeatLastRelativeMove()` and
          `goToPreviousPosition()` (whose two copies are now one).
          `enum class Axis` is gone (its last uses take a number), as is
          `m_lastRelativeMoveAxis`, which was set but never read. The
          motors themselves are still `m_axis1Motor`/`m_axis2Motor` (about
          400 uses), with `motor(n)` for code taking a number: they can
          move into the state with the moves
        - **Then (20 Nov 2026), speed:** `speedIncrease()`,
          `speedDecrease()` and `selectSpeedPreset()` take the axis number,
          in place of the six `axisNSpeed...()`, and the controller's cases
          for `=`, `-` and the presets are one each. Each axis keeps its own
          steps (axis 1: 1 below 20, then 20; axis 2: 2 below 10, then 10),
          in a table, and axis 1's is still left alone when threading
          (where the presets are off for both). `Model::speedPreset()`
          gives a preset's speed, so the menu no longer has its own copy of
          the defaults. Two small differences, both deliberate: faster now
          goes up to the maximum rather than stopping short of it on axis 1
          (e.g. 990 went no further, with 1000 the maximum) or going past
          it on axis 2 (391 went to 401, with 400 the maximum, on the
          lathe's X); and a speed within 0.1 of a step's boundary (e.g.
          20.0001, read back from the motor's rpm) counts as on it, as
          axis 2's already did
        - **Then (20 Nov 2026), stopping:** `stopAxis()` and
          `stopAxisRamped()` take the axis number, in place of the four
          `axisNStop...()`, which were the same but for the motor (and only
          used inside the Model). `stopAllMotors()` (Space) is unchanged:
          it signals both motors before waiting for either, so they stop
          together. A new test presses "stop" twice on each axis with
          deceleration ramping: the first slows down, the second stops at
          once
        - **Then (20 Nov 2026), moves, rapids, nudges and fast returns:**
          `move()`, `rapid()`, `nudge()` and `fastReturn()` take the axis
          number, in place of the eight `axisN...()`, with one direction
          type for every motorised axis, `AxisDirection` (`Forward` is the
          left and up arrows' way; `axisDirection()` turns a `ZDirection` or
          `XDirection` into one). Axis 1's moves still go through the
          motion sequence (for a taper or threading), and its fast return
          in a taper is still at 100 mm/min. Nothing tested the directions,
          and they turned out not to agree: a move's direction came from
          `AxisNMotorFlipDirection`, but a nudge's from the conversion
          factor's sign (and, on axis 2 only, the flip as well). Those
          agreed on the lathe and the mill's X, but on the mill's rotary
          table (a positive conversion, not flipped) `W`/`S` turned it the
          opposite way to Up/Down. Decided (20 Nov 2026): the arrows' way,
          so a nudge's direction now comes from the flip alone, as a
          move's does, and only A's nudges change. A test now checks every
          move, rapid and nudge's direction with both machines' settings
          (and fails on the old rule, for the mill's A nudge alone). A
          nudge's steps are now rounded, rather than truncated. **Tried on
          the mill (20 Nov 2026):** nudges used a lot, as normal. **Still to
          check on the mill:** that `W` turns A the same way as Up
        - **Then (20 Nov 2026), locks:** the lock calls already took an
          axis number; now the refusals do too. `axisMoveRefused()` is the
          one check for a move of an axis (adding X for axis 1 in a taper,
          as X follows Z), in place of `axis1MoveRefused()` and
          `axis2MoveRefused()`, and `moveRefused()` takes the axes to check
          (e.g. `{ 1, 2 }`) rather than a bool for each of two. Axis numbers
          are `int` throughout, as elsewhere, rather than `unsigned` for
          the locks. No change of behaviour
        - **Then (20 Nov 2026), the motors:** `m_axis1Motor` and
          `m_axis2Motor` are now each axis's `MotorAxisState::motor`, and
          the Model's 265 uses go through `motor(n)`. (Its const version
          returns a plain pointer, as the unique_ptr did, since const
          functions call the motor's non-const ones.) That changed the
          order they're destroyed in, after the scale rather than before,
          and the fake scale's thread reads axis 1's motor: so the Model now
          has a destructor which calls `resetMotorThreads()` (the scale,
          then axis 2's motor, as it may follow axis 1's in a taper, then
          axis 1's), rather than relying on the members' order. Only the
          app's quit called it before; the tests didn't. No change of
          behaviour. **Tried on the mill (20 Nov 2026):** quitting, with no
          problems
        - **The scale, decided (20 Nov 2026):** a scale on the lathe's X is
          planned, once Z's (the trial: quadrature, straight to two GPIO
          pins, as X's will be, not a DRO sender) is working and proven; it's
          waiting for the 5 V to 3.3 V converter boards. X's is to do
          everything the mill's X does: the checks at rest and the re-base,
          the checks while moving, finishing moves, and re-basing between
          passes (which, on the lathe, covers X's infeed as well as Z). If
          two axes disagree with their scales at once, one dialog deals with
          both: it waits until every scale has settled (the first mismatch
          stops everything, and the axes settle at different moments), then
          lists each axis's disagreement, and Y/Enter (the scales) or N/Esc
          (the motors) applies to every axis listed. In three parts, each its
          own commit:
            - (a) the scale's state and calls by axis number, with only
              axis 1 reading a scale: no change of behaviour
            - (b) axis 2 reading `Axis2Scale...`: the checks at rest and
              while moving, the combined dialog, the display on X's line,
              and X's leader then `B`. Tried in the fake build only
            - (c) finishing moves and re-basing between passes for axis 2,
              with the retraction and the multi-pass X correction
        - **Done (20 Nov 2026), part (a):** each motorised axis's
          `MotorAxisState` has a `ScaleState` (the `AxisScale`, the at-rest
          check's state, the mismatch and its asks, the stall stop, the
          finish-on-scale setting and any finishing move under way, and the
          fake scale), in place of `m_axis1Scale` and its dozen members. Its
          17 public calls take the axis number (`hasScale()`,
          `scalePositionMm()`, `scaleDifferenceMm()`, `scaleMismatchMm()`,
          `acceptScalePosition()`, `keepMotorPosition()`,
          `rebaseFromScaleByHand()`, `finishesOnScale()`, and so on), as do
          the at-rest check, the re-bases, `configureScale()` (which reads
          `AxisNScale...`) and `backlashCompensationMm()`. checkStatus()
          updates and checks both axes' scales, but only `configureScale(1)`
          is called, so only axis 1 has one. The scale is declared after the
          motor in `MotorAxisState`, so destroyed first, and ~Model() stops
          both scales before the motors anyway. Still axis 1's alone: the
          finishing moves' logic (part c), `cutReady()` (axis 1 is the cut
          axis), the backlash measurement, and the controller's dialog and
          the view's scale text (part b)
        - **Done (20 Nov 2026), part (b), not yet tried on a machine:**
          `configureScale(2)` reads `Axis2Scale...`, so axis 2 can have a
          scale, off unless set. It's lined up again wherever X's motor
          position is set (zeroing, setting, halving, a diameter, and gear
          cutting's renumbering by whole turns), and has the same checks at
          rest and while moving as axis 1's, the re-base at rest
          (`Axis2ScaleRebase`), and the re-base by hand (X's leader then
          `b`, also in the menu). A quadrature scale on any axis but 1 must
          have its pins set, as axis 1's default to 5 and 6; in the fake
          build, it's simulated by following its motor (the mock GPIO only
          simulates axis 1's). The scale's text is on each motorised axis's
          line (X's less any multi-pass correction, as X's position is), and
          with a scale on axis 2, the taper's angle and RETRACTED are drawn a
          line lower, clear of it (checked on snapshots). One dialog asks
          about every axis which disagrees: it waits until no other axis's
          scale is still settling (`Model::scalesStillSettling()`, which
          doesn't wait for an axis which already disagrees, as a misreading
          scale may never settle), then lists each, and Y/N applies to every
          one; an axis whose scale is misreading keeps its motor's position
          either way. Keys wait while either axis disagrees, and the alarm
          sounds for either. New tests: X's re-base at rest and by hand,
          zeroing X lining its scale up, both disagreeing in one dialog, the
          dialog waiting for the other scale to settle (which fails without
          the wait), one misreading and the other disagreeing, and a
          quadrature scale on axis 2 needing its pins. Still axis 1's alone
          (part c): finishing moves, re-basing between passes, and the
          re-base by hand while a multi-pass is paused
        - **Done (20 Nov 2026), part (c), not yet tried on a machine:**
          finishing moves for either axis (`startFinish()`,
          `advanceFinish()`, `finishMove()` and `endFinish()` take the axis
          number; X's go-to, relative move and memory return start one, as
          Z's do), with X's own on and off in the menu
          (`toggleAxis2FinishOnScale`, System, `G`; `Axis2ScaleFinishMoves`
          to start on). Keys wait while either axis finishes, and Space ends
          both. Re-basing between passes for X, the feed axis: the
          multi-pass asks the new `IMultiPassMachine::feedReady()` after
          each feed to a pass's depth (or before the pass, if it's there
          already), in a new `FeedSettling` state, until it's true. The
          Model's waits for X's scale to settle and re-bases X (with
          `Axis2ScaleRebaseBetweenPasses`, in a plain multi-pass or gear
          cutting, as for `cutReady()`). If the re-base leaves X short of or
          past the depth, the multi-pass feeds again, at most
          `MAX_FEED_CORRECTIONS` (3) times, then stops with a warning
          rather than cut at the wrong depth. While paused, X is re-based
          each time its reading settles, and by hand (`XB`), as Z is. A
          re-base renumbers the motor's steps, so the retraction's stored
          step (`m_xOldPosition`) is then the nominal position, which is
          where the table really is: unretracting goes to the true depth.
          The multi-pass X correction is only in the shown and feed
          positions, not the motor's, so a re-base doesn't disturb it. The
          settling overlay and the status line name X while it waits. New
          tests: the engine waiting for the feed, feeding again, and giving
          up after three; X finishing on its scale (short, past, a memory
          return, Space); X's toggle in the menu; and a multi-pass with X
          falling 0.03 mm short of its motor, paused and not, which (without
          the feed's re-base) cuts the last pass 0.03 mm too deep. Not
          done: a correction that reverses (X fed past the depth) relies on
          the motor's backlash compensation, rather than backing off and
          approaching again as a finishing move does; the re-base after it
          catches what's left, within the three tries. **To try on the
          lathe**, once X has its scale. This completes stage 2
        - **Then (20 Nov 2026), approaching on the scale** 
          (Question: could an overshoot cut what it shouldn't? Yes, where the
          tool is in the work, e.g. finishing at a pocket's end wall on the
          mill; a correction afterwards can't undo it. The feed is normally
          clear of the work, with Z off the end, but correcting an
          overshoot by reversing leaves the slack the wrong way, so the cut
          can push the tool out, and a pass starting inside the material
          would plunge too deep). An overshoot comes from momentum at a stop
          from speed, and from the backlash compensation on a reversal (a
          fixed 820 steps on the mill's X, where the backlash is 0.31-0.45
          mm). So, with `AxisNScaleApproachMm` (off unless set) and
          `AxisNScaleApproachSpeed` (20 if not set): a move finished on the
          scale stops that far short, waits for the scale, then creeps the
          rest the same way (not counted as a correction; a move shorter
          than the approach creeps all the way; backing off after going
          past returns to the approach's start, then creeps). The creep's
          speed is put back afterwards, as a stop from it would leave it
          (`speedAfterStop()`, so a fast go-to still ends at
          `AxisNSpeedResetTo`). The multi-pass feed (axis 2, with
          `Axis2ScaleRebaseBetweenPasses`) does the same inside the Model
          (`FeedApproach`: back off if past, move short, wait for the scale
          and re-base, creep in), with `feedMoving()` true throughout, so
          the multi-pass sees one feed; this also replaces the reversing
          correction noted as not done above. Only the feed before a pass,
          not a radius's path. On for the mill's X at 0.2 mm and 20 mm/min
          **Tried on the mill (20 Nov 2026):** go-tos
          on X stop short and creep in as expected. Tests: with
          the table running 0.05 mm ahead of the motor, a go-to's scale
          never reads past the target with the approach, and does (by more
          than 0.04) without; a short move creeps all the way; and a
          multi-pass whose X table gets 0.03 mm ahead at the last feed never
          goes deeper than the depth with it, and does (by more than 0.02)
          without. Still to do, as a follow-up: a multi-pass cut's end in Z
          (e.g. into a shoulder) isn't checked against the scale, and the
          cut starts with a reversal from the return, so the backlash
          compensation's error shifts where it ends; it would want to stop
          short and creep in too
        - **Then (20 Nov 2026), the cut axis's ends:** the user chose, of
          stopping short and creeping (a dwell with the tool in the work,
          which may leave a faint mark the approach's distance before the
          end), or taking up the slack beyond the start and re-basing there
          before the cut (no dwell in the work, but it needs room beyond the
          start, which a pocket's wall on the mill hasn't), the first. So the
          approach is now general (`AxisApproach`, by axis number, in
          `m_approaches`): besides the feed, axis 1's multi-pass moves (the
          cut, the cut back, the return, and the move to a pass's start)
          stop short, wait for the scale and re-base, then creep in, with
          `Axis1ScaleApproachMm` and `Axis1ScaleRebaseBetweenPasses`. Not a
          radius's path, threading, or a cut with X following Z, as
          re-basing Z part way would move X (the follower moves by the
          leader's change in position: see `StepperMotor::synchronise()`).
          When the return has just re-based at the start, `cutReady()`
          doesn't wait for the scale again, so a pass waits twice (the cut's
          end and the return's), not three times. A creep is never faster
          than the axis was going (`creepSpeed()`; a fine cut on the lathe
          may be slower than 20 mm/min), for the finishing moves too. The
          simulated scale now gives `tableMm()`, where the table really is,
          without the reading's lag, as the return starts before the reading
          would catch up with a cut's end. Test: Z's table gets 0.05 mm
          ahead as the second cut starts, and 0.05 the other way as its
          return starts: with the approach, the table never goes past the
          cut's end or back past the start; without, it ends 0.05 past the
          end (and with the return left out of the approach, 0.05 past the
          start). On the mill (0.2 mm), each pass now waits about 4 s
          rather than 2. **Tried on the mill (20 Nov 2026):** a multi-pass
          on X worked as expected, stopping short and creeping at both
          ends, but not on a real cut: **still to see** whether the dwell
          0.2 mm before a cut's end leaves a mark (a smaller approach, e.g.
          0.1, would move it closer to the end). `XB` (re-basing by hand) not
          tried since the scale's calls moved to axis numbers
        - **Then (20 Nov 2026), in the fake build with lc_mill.cfg**, the
          user saw a multi-pass from 0 to -5 stop at -4.80, jump to about
          -5.08, then go back, rather than creep. The simulated table had
          no backlash, so the mill's 820 steps (0.256 mm) of compensation
          took it 0.256 further than the motor on each reversal into a cut:
          past the stop-short point (0.2) and the end, and the re-base (the
          "jump") then found it there, and the creep reversed onto the end.
          Two fixes: the simulated table now has backlash
          (`FakeDroScale`'s slack, `Axis1FakeBacklashSteps`, the
          compensation unless set; the two tests which want a table with
          none set it to 0), so the fake build behaves like a machine whose
          compensation is right; and an approach which finds itself past its
          target once re-based backs off past the backlash and approaches
          again (once), with a warning that the approach is shorter than the
          overshoot ("X went 0.056 mm past its target: make
          Axis1ScaleApproachMm more than that"), as finishing moves now warn
          too. Tests: the simulated slack both ways, and the use case
          (0.256 compensation, 0.2 approach): with the table's backlash as
          compensated it stops short, creeps, and never passes the end, with
          no warning; with none, it warns
        - **Then (21 Nov 2026), on the mill:** a multi-pass along X from
          about 0 to -97 mm (cuts at 140 mm/min, returns at 800) gave that
          warning five times ("X went 0.010 mm past its target"). Each
          97 mm move there ended 0.11-0.22 mm further than the motor by the
          scale, whatever the speed (40 to 800 mm/min), and 0.25 mm on a fast
          return to a slot. So it isn't momentum. Short moves end within
          about 0.05 mm, and 97 mm moves between -62 and 35 mm only
          0.04-0.09 mm further, so it looks like the lead screw's pitch
          varying along its length (not the scale's counts per mm, which
          would be the same everywhere), which the re-basing deals with. The
          mill's approach is now 0.35 mm (0.1 more than any seen). The
          warning's advice was misleading: it gave how far past the target
          the axis went (0.01), when the approach (0.2) had to be more than
          the overshoot from where it aimed (0.21). It now says both, and
          suggests more than the overshoot. **To check on the mill:** no
          more warnings with 0.35, and (if wanted) the lead screw's error
          along the table, e.g. by a slow move end to end and the scale's
          difference every 10 mm
        - **Then (21 Nov 2026), clearing a warning:** on the mill, Esc was
          pressed (by instinct) to clear that flashing warning, which left
          multi-pass mode part way through. So `F9` now clears the warning
          and does nothing else (allowed whatever is going on, and with the
          menu open), and a warning which can be cleared ends with
          "[F9 to clear]". Not the encoder's lost pulses in threading or
          knurling, which is still true until the thread is chosen again.
          A warning whose cause is still there comes back. Test: F9 clears
          a lock refusal while Z keeps moving, with and without the menu
          open. Tried on the mill (22 Nov 2026): F9 cleared a warning.
          Esc ending a multi-pass without asking is still as it was
        - **Then (22 Nov 2026), not yet tried on the mill:** `Esc` in any
          mode now asks first ("Leave multi-pass mode?", `Enter` or `Y` to
          leave, `Esc` or `N` to stay), so an Esc by instinct then another
          keeps the mode. During a multi-pass (or threading passes, or gear
          cutting) the dialog says the passes would end part way through.
          It doesn't go through `okCancel()`, which stops the motors first:
          a pass carries on while it's open, and Space still stops
          everything. With no mode it doesn't ask, and a go-to wound by
          hand is cancelled at once either way, as it's only a guide. (While
          defining a tool, Esc still just cancels that, without asking.)
          Tests: Esc then Esc or N stays, Y leaves, a taper cut keeps
          moving while it asks, no dialog with no mode, and gear cutting's
          paused pass carries on after Esc then Esc. (A Z move in a taper
          first seemed to stop by itself in the test: it's a sequence,
          backlash take-up then the cut, which moves on only as the
          controller ticks, and the UI tests don't tick unless told to. On
          the machine, run() ticks, and the cut runs; not a fault)
    - **Done (22 Nov 2026), not yet tried on the mill:** a manual axis's
      leader then `G` (`YG`, `ZG`) goes to a typed position, wound by hand,
      as for its memory slots (the same line, and the ticks). Found missing
      in the fake build: there was no `a3_g`/`a4_g`, so it did nothing. The
      dialog says the axis is moved by hand. It can't say which way to
      wind, as the target isn't known until it's typed, so the go-to's line
      says it instead ("..., clockwise" or "..., anticlockwise", and the
      other way once past), from `Axis3ClockwiseIncreases` (and Axis4).
      Unset, nothing is said. Tried on the mill (22 Nov 2026): winding Y's
      handle anticlockwise makes its position go up, so lc_mill.cfg has
      `Axis3ClockwiseIncreases = false`. Z's main handle lowers it
      anticlockwise (pulled down), but its fine feed knob lowers it
      clockwise, the opposite way, so one key can't describe both.
    - **Done (22 Nov 2026), not yet tried on the mill:** the go-to's line
      names the handle the direction is for, from `Axis3HandleName` (or
      Axis4; empty, the default, names none). The mill has
      `Axis4ClockwiseIncreases = false` and `Axis4HandleName = fine feed`,
      as it's the fine feed that's wound near a target. The line now says
      "CW" and "CCW" rather than "clockwise" and "anticlockwise", on every
      axis (the user was happy with that): the view is drawn 1280 wide and
      scaled, so the go-to's text has room for about 40 characters, and
      "to -125.00: 125.00 to go, fine feed anticlockwise" is 49

#### Smaller tidy-ups

- `axis.cpp` / `axis.h` are compiled but unused, and their `mgo::Axis`
      class clashes in name with `enum class mgo::Axis` in model.h
    - **Done:** removed (they're in git's history, if the "array of axes"
      idea below wants them)
- Set the initial X-axis retraction direction from the config file
    - **Done:** `Axis2RetractionDirection` (outwards or inwards) in lc.cfg

### Other modes and features

#### Cut knurling

- **New mode: "cut knurling".** A cut (rather than pressed) knurl, made
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
    - **Started (2026-11-12), in the mock build only:** knurling mode (Modes,
      "Cut knurling", `K`). It uses the multi-pass dialog (start, Z end, X
      infeed as the depth, infeed per pass, constant area), plus the number of
      starts and the helix angle. The lead comes from the diameter at the
      start, so X must have been set as a diameter. The cuts are threading
      passes at the knurl's lead. The multi-pass engine gained two general
      options: `starts` (each pass cuts every start, 1/N of a turn apart,
      before feeding in) and `cutBack` (the return is a cut too).
        - **Decision: each start there and back, rather than all of one hand
          and then all of the other.** With the tool still in, the return to
          the start cuts the opposite hand, so there's no retract and no
          non-cutting return at all. Every cut back starts at the Z end, at
          the same spindle angle each pass, so the passes line up. The cost
          is that the Z end always needs a relief groove (or to run off the
          work)
        - **Decision: knurling's spindle counts as turning down to 5 rpm**
          (threading's limit is 30), as on the lathe, Z's 1000 mm/min limits
          a 45° knurl on a 10 mm bar to about 25 rpm. The dialog refuses a
          lead Z couldn't follow even at 5 rpm. The rpm shown (in any mode)
          isn't rounded down to a multiple of 10 below 50 rpm
        - **To check on the machine:**
            - Z starts each pass from rest straight to full speed (follow
              mode doesn't ramp), and for a long lead that's far faster than
              threading has ever asked for. The HSS57 may stall. If it does,
              each cut may need a run-up (start further off the work, with
              Z's speed ramped up to the spindle's before the tool reaches
              it), which stepperControl's follow mode doesn't do yet
            - Whether the spindle runs smoothly with useful torque at 5-25
              rpm, and whether the encoder's rpm and lost-pulse checks behave
              at those speeds
            - How the tool's flanks cope with cutting both ways
        - **Not done yet:** taking the Ø from the current feature rather
          than X; multi-start threading (the engine supports it now, but the
          threading passes dialog doesn't offer it); allowing for a tool
          other than 90° in the starts suggested
        - **First hurdle on the machine (2026-11-12): the spindle won't go
          slow enough.** The VFD (Mitsubishi FR-D700-SC, set up by the
          package's supplier, with an external pot box) won't go below about
          70 rpm, so the mode refuses as "too fast". At 70 rpm, the lead can
          be at most 800 ÷ 70 ≈ 11.4 mm, which needs a helix angle of at
          least 70° on a 10 mm bar (80° on 20 mm): long thin diamonds, not a
          normal knurl. The supplier had set Pr.2 (minimum frequency) and C2
          (Pr.902, terminal 2 bias frequency) to 5 Hz; lowering Pr.2 to 2 Hz
          as a test made no visible difference, and it was put back.
        - **Solved the same day: both settings lowered to 1 Hz.** Each one
          alone holds the floor at 5 Hz: C2 is the frequency the pot gives
          fully down (0 V), and Pr.2 clamps the output to at least its
          value. Lowering both to 1 Hz brought the floor right down (at the
          old ratio of about 14 rpm per Hz, about 14 rpm: enough for 45° up
          to about 18 mm). On the D700's panel, the C parameters are in a
          "C" menu of their own, not in the numbered list. The supplier's
          5 Hz may have been to protect the motor (little torque, and little
          cooling, at a few Hz under V/F control), so the spindle shouldn't
          be left running slowly under load. The two other ideas below were
          considered first, and may still be useful (e.g. for bigger bars,
          or more torque):
            - **A "back gear" (hardware):** a second pair of pulleys on the
              motor's toothed belt, giving a lower range. The floor drops in
              proportion: 3:1 gives about 23 rpm (enough for 45° on 10 mm),
              5:1 about 14 rpm (18 mm), 7:1 about 10 rpm (25 mm). The torque
              goes up by the same ratio. The encoder is driven from the
              spindle, so its gearing in the config doesn't change with the
              belt. No code change needed
            - **A hand-turned spindle (software):** passes already follow the
              spindle's position, not its speed. In stepperControl's follow
              mode, Z stops when the spindle stops, and goes back along the
              groove if it's turned back (its target is clamped between the
              cut's start and end). Starting from rest by hand would also
              avoid Z having to jump to full speed. It would need, as a
              knurling dialog option:
                - no minimum spindle speed: a pass starts with the chuck
                  still, and waits for it to be turned to the start angle
                - Z's follow speed at its maximum (it's set from the
                  measured speed now, which would be about zero)
                - "forwards" fixed as the spindle's normal direction, rather
                  than taken from the encoder as each pass starts (rocking
                  the chuck back first would give that cut the wrong hand)
                - no stop when the spindle stops (normal when hand-turning;
                  Z only moves when the chuck does, and Space still stops)
                - no lost-pulse check: a pause in turning, then carrying on
                  at the same speed, looks exactly like lost pulses. Log
                  them instead (core pinning fixed the original cause)
                - a tighter limit on how far Z may lag (a quarter of a
                  revolution is 8 mm of Z at 45° on 10 mm), with a "turn
                  slower" warning as it gets near
                - prompts: "Turn the chuck forwards", "waiting for start
                  3's angle"
                - On the machine: a crank on the spindle's outboard end, and
                  the VFD off at the mains. At hand speeds, Z would move in
                  bursts of one encoder pulse (about 0.04 mm at 45° on 10
                  mm), which is probably fine but may sound rough
            - **Either way, there's a lot of turning:** waiting for each
              start's angle takes about as long as the cut. A whole knurl
              (31 starts, there and back, about 6 passes) is very roughly 350
              revolutions, 15-20 minutes at 20 rpm. Cutting whichever start's
              angle comes round next, rather than in order, would save much
              of the waiting, powered or by hand
        - **First knurls on the machine (2026-11-12):** Ø 22.7, Z 14.8 mm,
          at 70° (the spindle still wasn't slow enough for 45°), about 27-30
          rpm (about 2 Hz). 22 cuts each time went well, then:
            - **First run (71 starts): the VFD tripped mid-cut.** The
              encoder's pulses stopped, so the passes were stopped. The
              fault wasn't noted before it was reset; the D700 keeps its
              last eight faults (MODE to the fault history, with the
              frequency, current and voltage at the fault), so it can still
              be looked up. Only "Stopped" was shown, with no reason.
              **The fault history showed E.THM** (motor overload, shown as
              "E.rHn" in seven segments): the drive's thermal model of the
              motor, not a real fault. For a standard self-cooled motor
              (Pr.71), the model allows less and less current as the
              frequency drops, as the motor's fan barely turns. In V/F
              control at about 2 Hz, the motor also draws a lot of
              magnetising current even when idling (torque boost, Pr.0), and
              much of a knurling run is waiting for start angles. About 20
              minutes of low-speed testing came before that run. Things to
              check: that Pr.9 matches the rated current on the motor's
              plate. Not to do: set Pr.71 to a constant-torque motor (only
              for a motor with a separately powered fan), or raise Pr.9
              above the plate
            - **Second run (40 starts): a pass waited for ever.** Its start
              angle was set more than a revolution *behind* the spindle
              ("from spindle pulse 1839425 (now 1840261)"), which only
              happens if the spindle was taken to be turning backwards. The
              direction came from the encoder's latest edge: a stray edge on
              its B channel flips that for a few milliseconds, until the next
              A edge, and the jerky spindle at 2 Hz (with the encoder belted
              at 35:100) makes stray edges likely. One pass in 22 started
              in such a window. (Threading passes had the same flaw, though
              it's much less likely at their speeds.) The counting itself
              isn't affected: each A edge sets the direction again first
            - **Fixed:** a pass now takes the spindle's direction from how
              far its count has moved over the last tenth of a second or so
              (sampled on each update), and refuses to start if it can't
              tell. Every pass of a run must turn the same way as the first
              (or it would cut the other hand). A pass waiting for its start
              angle is stopped, with a warning, if the spindle turns three
              revolutions away from it, so nothing can hang silently. And
              when the spindle stopping stops a multi-pass (or any motor),
              the warning says so. Not yet tried on the machine
        - **Where it stood (2026-11-12): undecided.** Running the motor
          at 1-2 Hz trips its overload protection, so the user may set Pr.2
          and C2 back to 5 Hz (about 70 rpm), which limits knurls to steep
          helix angles (about 70° on 10 mm, more on bigger bars). The back
          gear looks impractical for about 10 rpm: a single stage of about
          7:1 needs a very large pulley on the spindle. (Two smaller stages
          through a countershaft would avoid that, but it's more work
          still.) Hand-turning the spindle (above) needs no motor at all,
          so it has none of these problems. Cutting the starts in whichever
          order their angles come round would shorten each run (so less
          heat), but isn't worth doing until the way forward is chosen
        - **A good knurl at the spindle's usual floor (2026-11-15):** with
          the VFD set back as supplied (about 70 rpm), Ø 17.5, 80°, 16
          starts, 0.2 mm deep (0.1 mm first pass), 4 passes. The "diamonds"
          are very long and thin, which looks almost like a weave, but the
          surface grips very well, which is what it's for. So steep angles
          at 70 rpm are the way forward for now, with no motor or VFD
          changes. Then, as the user asked:
            - **The dialog suggests the angle and starts** (until typed in;
              typed ones are kept, and clearing one has it suggested again).
              The angle is the smallest whose lead Z can follow at
              `KnurlingSpindleRpm` (new in lc.cfg, 70; 45° if not set): 79°
              for Ø 17.5. The starts space the grooves twice the depth apart
              across them, for sharp diamonds with a 90° tool: 26 for Ø 17.5
              at 79° and 0.2 mm deep. (The 16 used left flats: 0.6 mm apart
              across against a 0.4 mm groove.) The dialog now shows the
              spacing across the grooves, not round the work, as at 80° the
              two differ six-fold, and the old figure hid that too many
              starts would just turn the work down
            - **Starts in whichever order comes round first.** The engine
              offers the model every start still to cut (that way, this
              pass), and the model starts the one whose angle comes round
              first after the backlash take-up. Each pass still cuts every
              start once each way. A cut used to wait for the next whole
              turn and then the start's offset (up to two turns); now it's
              at most just over one, and usually much less. (Gear cutting's
              indexed starts are still in order.) Not yet tried on the
              machine
        - **A faster Z for knurling doesn't work as it is (2026-11-16).** At
          70 rpm, Z's speed limits the angle: 45° on Ø 17.5 would need Z at
          3,850 mm/min, beyond even the motor thread's ceiling of 3,000 rpm
          (a 20 µs step), and Ø 10 about 2,750 rpm. Raising
          `Axis1MaxMotorRpm` from 1000 to 1400 only brought the angle
          suggested for that bar down from 79° to 77°, and Z stalled into
          alarm on the passes, though fast moves at 1400 (which ramp) were
          fine. So it's a pass's start from rest straight to full speed
          that Z can't do, not the speed itself. Set back to 1000
            - **Idea (low priority): a ramped start for knurling passes.**
              Each cut starts a few millimetres further off the work, and Z
              ramps up while following the spindle (stepperControl's follow
              mode allows ramping): it falls behind at first, then catches
              up before the tool reaches the work. That needs Z's top speed
              well above the speed the lead needs (to catch up), so it would
              want a knurling-only Z limit (e.g. `KnurlingMaxZSpeed`), so
              rapids and other moves stay as they are. The lag check, a
              quarter of a turn, would need to allow for the run-up. The
              cut back would need a run-up at the chuck end too

#### Thread pitches file

- Add ability to add new thread pitches without recompilation. Move the
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

#### A fresh log each session

- Start a fresh log each session: at start-up, before `INIT_MGOLOG("lc.log")`
      in main.cpp, rename any existing lc.log to lc.log.previous (replacing an
      older one), so there's always this session's log and the last one's. The
      log grows without limit now that threading passes, retracts, spindle
      readings and lost pulses are all logged, and the session in question
      after a workshop test is hard to find in it. (`std::filesystem::rename`;
      ignore a failure, e.g. no log yet, rather than refusing to start)
    - **Done:** as described. Tried on the mill (22 Nov 2026): lc.log is
      fresh each session, and lc.log.previous is kept

#### Generic multi-pass origins

- Add some form of generic multi-pass functionality (defining a start position,
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
- Once this "generic multi-pass" functionality is available, it could be used for:
        - threading on the lathe
        - turning down to a workpiece to a set diameter (lathe)
        - radiusing (lathe). **Done** as radius mode (see "Radius mode" above),
          from the current feature as suggested here; the end X became the
          radius itself (up to half the diameter for a full ball end)
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
          **Partly done** as gear cutting mode (the table turns 360° ÷ teeth
          between cuts, and the depth is fed by hand once per turn); the Y
          feed per revolution, e.g. for a hexagon, isn't
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
