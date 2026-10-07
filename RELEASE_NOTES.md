# v1.0.0: first release

Firmware for the C42 control stick: an Arduino Pro Micro that shows up as one USB joystick with three axes and two buttons.

## What it does

| HID | Control | Hardware |
|---|---|---|
| X | Roll | AS5600 on TCA9548A **SD3** |
| Y | Pitch | AS5600 on TCA9548A **SD2** |
| Z | Hand brake | AS5600 on TCA9548A **SD4** |
| Button 1 | Trim up | tact switch, D4 → GND |
| Button 2 | Trim down | tact switch, D5 → GND |

- **Three AS5600s behind a TCA9548A** at 0x70, read directly over I²C. The only library needed is "Joystick" by Matthew Heironimus.
- **Shows up as "C42 Control Stick"**, not "Arduino Leonardo". This needs the custom board in `hardware/c42`, which uses USB ID 0x1209:0x0001.
- **Calibration:**
  - Centre and brake rest are taken at every power-up.
  - Each axis learns its extents the first time it's moved to its stops, and they're saved to EEPROM.
  - Each half of pitch and roll is scaled separately.
  - Hold both trim buttons while plugging in to wipe the saved extents.
- **The magnet can sit at any angle:** positions are measured from centre, so the sensor's 0/4095 point can fall inside the travel.
- **Dropouts:** if a sensor or magnet stops reading, that axis holds its last value.
- **Settings:** about 100 Hz updates, EMA filter, ±6-count deadzone, and a reverse flag for each axis.

## Install

1. Copy `hardware/c42` to `<sketchbook>/hardware/c42` and restart the Arduino IDE.
2. Select the board **C42 Control Stick (Pro Micro)** and install the **Joystick** library.
3. Upload `c42_joystick/c42_joystick.ino`.
4. Plug in with the stick centred and the brake released, then move every axis to its stops once.

See the README for full wiring and tuning.
