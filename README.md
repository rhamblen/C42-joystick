# C42 Joystick Firmware

**Current version: v1.0.0.** See [CHANGELOG.md](CHANGELOG.md).

Firmware for a 3D-printed Ikarus C42 flight-sim joystick. It runs on an Arduino Pro Micro, which shows up in Windows as one USB joystick with three axes and two buttons:

| HID | Control | Sensor |
|---|---|---|
| X | Roll | AS5600 on TCA9548A SD3 |
| Y | Pitch | AS5600 on TCA9548A SD2 |
| Z | Hand brake | AS5600 on TCA9548A SD4 |
| Button 1 | Trim up | tact switch, D4 → GND |
| Button 2 | Trim down | tact switch, D5 → GND |

This is the sister of the C42 throttle sketch. That one reads a single AS5600 directly. This one has three, and each AS5600 has the same fixed I²C address (0x36), so they sit behind a **TCA9548A multiplexer**, one channel each.

## Build

1. Copy `hardware/c42` into your sketchbook's `hardware` folder, so that you have `<sketchbook>/hardware/c42/avr/boards.txt`, then restart the Arduino IDE.
2. Select the board **C42 Control Stick (Pro Micro)**. It is the stock Leonardo with its USB name changed, so the stick shows up as "C42 Control Stick" and not "Arduino Leonardo". Plain **Arduino Leonardo** still works, just under that name.
3. Library Manager: install **Joystick** by Matthew Heironimus. No AS5600 library is needed because the sensors are read directly over `Wire`.
4. Open `c42_joystick/c42_joystick.ino` and upload.

The custom board also uses its own USB PID (0x1209:0x0001, a pid.codes test ID). Windows caches a joystick's name per VID/PID, so this is what keeps it from reusing the "Arduino Leonardo" name. It also keeps it separate from the C42 throttle, which still uses the Leonardo's 0x8036. For the few seconds after a reset, the bootloader still shows up as a Leonardo. That's normal.

## Wiring

| From | To |
|---|---|
| Pro Micro D2 (SDA) / D3 (SCL) | TCA9548A SDA / SCL |
| Pro Micro VCC / GND | TCA9548A VIN / GND, and every AS5600 VCC / GND |
| TCA9548A A0, A1, A2 | GND (address 0x70) |
| TCA9548A SD2/SC2 | pitch AS5600 |
| TCA9548A SD3/SC3 | roll AS5600 |
| TCA9548A SD4/SC4 | brake AS5600 |
| D4, D5 | trim up / trim down switch, other leg to GND (internal pull-ups) |

The channel, pin and axis-reverse settings are `#define`s at the top of the sketch.

## Calibration

- **Centre is taken at every power-up.** Plug in with the stick resting on its centring springs and the brake released. Don't hold the stick while it starts.
- **Extents are learnt.** Move pitch, roll and the brake to their stops once. Each half of pitch and roll is scaled to its own stop, so a stick that goes further one way than the other still reads full scale at both ends. The extents are saved to EEPROM about 2 s after they stop growing.
- **Reset:** hold both trim buttons while plugging in. This wipes the stored extents; then sweep the axes again.
- Offsets are measured from centre with wrap handling, so the magnet can sit at any angle. It doesn't matter if the sensor's 0/4095 point falls inside the travel.
- If a sensor or magnet drops out, that axis holds its last good value.

## Tuning

| Setting | Default | Effect |
|---|---|---|
| `FILTER_STRENGTH` | 4 | EMA divisor: higher values are smoother but slower |
| `DEADZONE` | 6 counts (~0.5°) | dead band around centre and brake rest |
| `UPDATE_INTERVAL_MS` | 10 | ~100 Hz report rate |
| `REVERSE_PITCH/ROLL/BRAKE` | false | flip an axis that runs backwards in the sim |
