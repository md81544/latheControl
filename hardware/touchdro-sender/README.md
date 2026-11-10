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
  `ArduinoDRO_V2_1/ArduinoDRO_V2_1.ino` (last modified 7 May 2019). Its data
  pins match the box's wiring, so it is very likely what is loaded. The file
  has been renamed only because the Arduino IDE needs the sketch to sit in a
  folder of the same name; its contents are unchanged.
- `schematic.png`: the circuit from the original build instructions, from the
  same folder, where it was called "Bluetooth Controller schematic.png". The
  box differs from it in places; see below.

## Hardware

What was found on opening the box (10 November 2026):

- The Arduino has no USB port, and its power comes in on the RAW pin, so it
  is almost certainly a Pro Mini (or a clone), not the Nano the schematic
  shows. It's the same ATmega328P chip, so the sketch is the same. It was
  programmed through an FTDI USB-serial adapter.
- Power comes from a mains adapter, through a jack on the box, to RAW and
  GND, so the Arduino's own regulator makes its supply.
- Each scale plugs into a USB socket on the box. The sockets' D+ lines (the
  scales' data) go to D5, D6 and D7, the same pins as the sketch. The
  schematic shows D3, D4 and D5 instead. The sockets' labels suggest X and
  Y are swapped compared with the sketch, but the readings show each letter
  is the right axis, so the labels are probably wrong.
- The crystal is marked "08", which suggests the 3.3 V/8 MHz kind of Pro
  Mini. The sketch works with either kind, as long as the right board was
  chosen when compiling it.

Not yet checked:

- Which pin carries the clock. The sketch says D9; the schematic says D2.
- The supply voltage: a multimeter between VCC and GND with the box on will
  confirm 3.3 V. The schematic's dividers only make sense at 5 V.

From the schematic and the original builder's notes, which may also differ
from the box:

- An HC-06 Bluetooth module, set to 9600 baud and named "Ael Dro". Its PIN was
  left at the default.
- The scales need 3.3 V, from a separate regulator. On the USB sockets, pin 1
  is 3.3 V, pin 2 (D−) the clock, pin 3 (D+) the data, and pin 5 ground.
- The clock out to the scales, and the Arduino's TX to the HC-06's RX, each go
  through a resistor divider to bring 5 V down to about 3 V. Each data input
  has a 10k pull-down, so that an unplugged scale doesn't leave it floating.
- The schematic's TX and RX labels are swapped: on these boards D0 is RX and
  D1 is TX. The wiring makes sense if the labels are read the other way round.

## What it sends

The Arduino writes plain text from its serial port at 9600 baud, about 24
times a second, one token per axis:

    X1234;Y-567;Z89;

The values are raw scale counts. iGaging scales usually give 2560 counts per
inch, which is about 100.8 per mm, so about 10 µm per count. TouchDRO converts
the counts to mm. The sketch smooths each reading (a 24-sample adaptive
moving average), which steadies the display but lags a little while the table
is moving. The readings are relative to when the scales were powered up, not
absolute.

Directions, checked on 10 November 2026: the counts go up when the table
moves right (X), when the table moves in towards the column (Y), and when the
head comes down (Z).

## Listening to it

Connect a USB-serial adapter (such as the FTDI one) with only two wires: the
adapter's RX to the Arduino's TX (TXO on the Pro Mini's FTDI header), and GND
to GND. Leave the adapter's VCC unconnected, because the box has its own
supply, and leave DTR unconnected, because it resets the Arduino. Set the
adapter for the board's logic level. Then open a serial monitor at 9600 baud:
you should see lines like the one above at about 24 Hz. The HC-06 carries on
as before, so TouchDRO keeps working. This is how latheControl would read the
scales too.

This was tested on 10 November 2026 with the old FTDI adapter, and the stream
looked as expected, for example `X250383;Y-553984;Z131;`. The large values
are just where the scales' counters happen to be; zeroing deals with them.

Because the stream has no line breaks, it's hard to see changes in a serial
monitor. `watch_scales.py` prints a line only when a reading changes, naming
the axis and the change in counts and mm. It needs pyserial
(`pip install pyserial`):

    python3 watch_scales.py /dev/ttyUSB0
