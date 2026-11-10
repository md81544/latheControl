# TouchDRO sender for the mill's scales

This is the small DIY box on the milling machine that reads the three iGaging
(AccuRemote-type) digital scales, X, Y and Z, and sends their readings over
Bluetooth to TouchDRO on the Android tablet. It isn't part of latheControl, but
it's kept here so it isn't lost, and because latheControl may read the same
data in future (see "Investigate how to interface with the mill's linear
scales" in TODO.md).

## Files

- `ArduinoDRO/ArduinoDRO.ino`: the Arduino sketch. It is "ArduinoDRO + Tach
  V5.12" by Yuriy Krushelnytskiy and Ryszard Malinowski, released under the
  GPL v3. It was found in `~/syncthing/maker/machining/touchDRO/` as
  `ArduinoDRO_V2_1/ArduinoDRO_V2_1.ino` (last modified 7 May 2019), and is
  believed, but not yet confirmed, to be what is loaded on the Nano. The file
  has been renamed only because the Arduino IDE needs the sketch to sit in a
  folder of the same name; its contents are unchanged.
- `schematic.png`: the circuit, from the same folder, where it was called
  "Bluetooth Controller schematic.png".

## Hardware

- An Arduino Nano, in a socket on the board. Take it out to reprogram it (note
  which way round it goes) and use the Arduino IDE as normal.
- An HC-06 Bluetooth module, set to 9600 baud and named "Ael Dro". Its PIN was
  left at the default.
- The Nano is powered through its own USB port from a cheap USB power
  adapter. (The schematic and the original builder's notes feed 5 V from a DC
  jack to the Nano's 5 V pin instead.) A separate 3.3 V regulator, fed from
  the Nano's 5 V, powers the scales, because they only work at 3.3 V.
- The scales plug in through USB break-out boards: pin 1 is 3.3 V, pin 2 is
  the clock, pin 3 is the data, and pin 5 is ground.
- The clock out to the scales, and the Nano's TX to the HC-06's RX, each go
  through a resistor divider to bring 5 V down to about 3 V. Each data input
  has a 10k pull-down, so that an unplugged scale doesn't leave it floating.

Two things on the schematic need checking against the real board:

- **The pins don't match the sketch.** The schematic has the clock on D2 and
  the data from X, Y and Z on D3, D4 and D5. The sketch has the clock on D9
  and the data on D7, D6 and D5 (`SCALE_CLK_PIN`, `SCALE_X_PIN` and so on).
  Either the board was built differently from the schematic, or this isn't the
  sketch on the Nano.
- **TX and RX are swapped on the schematic.** On a Nano, D0 is RX and D1 is
  TX. The wiring makes sense if the labels are read the other way round: the
  Nano's TX goes through the divider to the HC-06's RX.

## What it sends

The Nano writes plain text from its serial port at 9600 baud, about 24 times
a second, one token per axis:

    X1234;Y-567;Z89;

The values are raw scale counts. iGaging scales usually give 2560 counts per
inch, which is about 100.8 per mm, so about 10 µm per count. TouchDRO converts
the counts to mm. The sketch smooths each reading (a 24-sample adaptive
moving average), which steadies the display but lags a little while the table
is moving. The readings are relative to when the scales were powered up, not
absolute.

## Checking what's on the Nano

Connect the Nano to a computer by USB and open the Arduino IDE's serial
monitor at 9600 baud. You should see lines like the one above at about 24 Hz.
This also shows that the Nano's USB port carries the same data as the
Bluetooth link. That is the way latheControl would read it, with TouchDRO
carrying on as before.
