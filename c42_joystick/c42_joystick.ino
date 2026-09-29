// Ikarus C42 flight sim joystick: Arduino Pro Micro, 3 x AS5600 behind a TCA9548A mux
// Library (install via Arduino Library Manager):
//   "Joystick" by Matthew Heironimus
// The AS5600s are read directly over Wire, so no AS5600 library is needed.
// Board: "C42 Control Stick (Pro Micro)" from hardware/c42 in this repo, which is the
//        Leonardo renamed over USB; plain Arduino Leonardo also works (ATmega32u4)
//
// Wiring:
//   TCA9548A  VIN->VCC, GND->GND, SDA->D2, SCL->D3, A0/A1/A2 -> GND (address 0x70)
//   AS5600    one per mux channel, VCC/GND/SDA/SCL to that channel's SDn/SCn
//             ch 0 = pitch, ch 1 = roll, ch 2 = brake  (see MUX_CH_* below)
//   Trim up   D4 -> switch -> GND
//   Trim down D5 -> switch -> GND
//
// Output (one USB HID joystick):
//   X = roll, Y = pitch, Z = brake, button 1 = trim up, button 2 = trim down
//
// Calibration: power up with the stick centred and the brake released. That
// position is taken as centre / rest. Then move every axis to its stops once;
// the extents are learnt and saved to EEPROM. To wipe them, hold both trim
// buttons while plugging in.

#include <Wire.h>
#include <EEPROM.h>
#include <Joystick.h>

#define MUX_ADDR    0x70
#define AS5600_ADDR 0x36

#define MUX_CH_PITCH 0
#define MUX_CH_ROLL  1
#define MUX_CH_BRAKE 2

#define PIN_TRIM_UP   4
#define PIN_TRIM_DOWN 5

#define REVERSE_PITCH false    // flip any of these if the axis runs backwards in-sim
#define REVERSE_ROLL  false
#define REVERSE_BRAKE false

#define FILTER_STRENGTH 4      // higher = smoother but slower to respond (EMA divisor)
#define DEADZONE 6             // counts either side of centre / rest (~0.5 deg)
#define UPDATE_INTERVAL_MS 10  // ~100 Hz
#define SAVE_DELAY_MS 2000     // batch EEPROM writes while an axis is being swept

#define AXIS_MAX 1023
#define AXIS_MID 512

enum { PITCH, ROLL, BRAKE, NUM_AXES };
const uint8_t muxChannel[NUM_AXES] = { MUX_CH_PITCH, MUX_CH_ROLL, MUX_CH_BRAKE };
const bool reverseAxis[NUM_AXES]   = { REVERSE_PITCH, REVERSE_ROLL, REVERSE_BRAKE };

// Extents are stored as signed offsets from centre, not absolute angles, so the
// sensor's 0/4095 wrap point can fall anywhere in the travel.
const uint8_t EEPROM_MAGIC = 0xC5;
struct CalibrationData {
  uint8_t magic;
  int16_t minOffset[NUM_AXES];  // <= 0
  int16_t maxOffset[NUM_AXES];  // >= 0
};

Joystick_ Joystick(
  JOYSTICK_DEFAULT_REPORT_ID, JOYSTICK_TYPE_JOYSTICK,
  2, 0,                  // 2 buttons, no hat switch
  true, true, true,      // X, Y, Z
  false, false, false,   // Rx, Ry, Rz
  false, false,          // rudder, throttle
  false, false, false);  // accelerator, brake, steering

CalibrationData cal;
uint16_t centre[NUM_AXES];
float filtered[NUM_AXES];  // signed offset from centre
bool calDirty = false;
unsigned long lastChange = 0;
unsigned long lastUpdate = 0;

bool muxSelect(uint8_t ch) {
  Wire.beginTransmission(MUX_ADDR);
  Wire.write(1 << ch);
  return Wire.endTransmission() == 0;
}

bool readReg(uint8_t reg, uint8_t n, uint8_t *buf) {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)AS5600_ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// Reads one sensor's 12-bit angle. False if the mux, the sensor or the magnet
// is missing, so the caller can hold the last good value.
bool readAngle(uint8_t axis, uint16_t *angle) {
  uint8_t b[2];
  if (!muxSelect(muxChannel[axis])) return false;
  if (!readReg(0x0B, 1, b) || !(b[0] & 0x20)) return false;  // STATUS.MD: magnet detected
  if (!readReg(0x0E, 2, b)) return false;                    // ANGLE, high byte first
  *angle = ((b[0] & 0x0F) << 8) | b[1];
  return true;
}

// Signed shortest distance from centre, -2048..2047.
int16_t wrapOffset(uint16_t raw, uint16_t c) {
  int16_t d = (int16_t)raw - (int16_t)c;
  if (d > 2047) d -= 4096;
  if (d < -2048) d += 4096;
  return d;
}

void clearCalibration() {
  cal.magic = EEPROM_MAGIC;
  for (uint8_t a = 0; a < NUM_AXES; a++) cal.minOffset[a] = cal.maxOffset[a] = 0;
  EEPROM.put(0, cal);
}

// Stick and brake springs return to the same place every time, so centre is
// taken fresh at each power-up rather than stored.
void captureCentres() {
  for (uint8_t a = 0; a < NUM_AXES; a++) {
    int32_t sumOff = 0;  // average relative to the first sample, so it is wrap-safe
    uint8_t n = 0;
    uint16_t first = 0, raw;
    for (uint8_t i = 0; i < 16; i++) {
      if (!readAngle(a, &raw)) continue;
      if (n == 0) first = raw;
      sumOff += wrapOffset(raw, first);
      n++;
      delay(2);
    }
    centre[a] = n ? (uint16_t)((first + sumOff / n + 4096) & 0x0FFF) : 0;
    filtered[a] = 0;
  }
}

// Centred axis: two halves, each scaled to its own learnt extent.
int centredOutput(float off, int16_t minOff, int16_t maxOff) {
  if (off > -DEADZONE && off < DEADZONE) return AXIS_MID;
  if (off > 0) {
    if (maxOff <= DEADZONE) return AXIS_MID;
    return map((long)off, DEADZONE, maxOff, AXIS_MID, AXIS_MAX);
  }
  if (minOff >= -DEADZONE) return AXIS_MID;
  return map((long)off, minOff, -DEADZONE, 0, AXIS_MID);
}

// One-ended axis: rest is 0, and whichever way the lever travels further is "pulled".
int brakeOutput(float off, int16_t minOff, int16_t maxOff) {
  long pull = (maxOff >= -minOff) ? (long)off : -(long)off;
  long extent = max(maxOff, -minOff);
  if (extent <= DEADZONE || pull < DEADZONE) return 0;
  return map(pull, DEADZONE, extent, 0, AXIS_MAX);
}

void setup() {
  pinMode(PIN_TRIM_UP, INPUT_PULLUP);
  pinMode(PIN_TRIM_DOWN, INPUT_PULLUP);

  Wire.begin();
  Wire.setClock(100000);  // stock speed: the sensors are on long leads up the stick

  EEPROM.get(0, cal);
  bool bothHeld = !digitalRead(PIN_TRIM_UP) && !digitalRead(PIN_TRIM_DOWN);
  if (cal.magic != EEPROM_MAGIC || bothHeld) clearCalibration();

  delay(50);  // let the sensors settle after power-up
  captureCentres();

  Joystick.setXAxisRange(0, AXIS_MAX);
  Joystick.setYAxisRange(0, AXIS_MAX);
  Joystick.setZAxisRange(0, AXIS_MAX);
  Joystick.begin(false);  // manual sendState() below, so updates land in one HID report
}

void loop() {
  if (millis() - lastUpdate < UPDATE_INTERVAL_MS) return;
  lastUpdate = millis();

  int out[NUM_AXES];
  for (uint8_t a = 0; a < NUM_AXES; a++) {
    uint16_t raw;
    if (readAngle(a, &raw)) {  // otherwise hold last good value
      filtered[a] += (wrapOffset(raw, centre[a]) - filtered[a]) / FILTER_STRENGTH;
      int16_t f = (int16_t)filtered[a];
      if (f < cal.minOffset[a]) { cal.minOffset[a] = f; calDirty = true; lastChange = millis(); }
      if (f > cal.maxOffset[a]) { cal.maxOffset[a] = f; calDirty = true; lastChange = millis(); }
    }

    int v = (a == BRAKE)
      ? brakeOutput(filtered[a], cal.minOffset[a], cal.maxOffset[a])
      : centredOutput(filtered[a], cal.minOffset[a], cal.maxOffset[a]);
    v = constrain(v, 0, AXIS_MAX);
    out[a] = reverseAxis[a] ? AXIS_MAX - v : v;
  }

  // Only write once the axes have stopped growing: EEPROM has a finite write life.
  if (calDirty && millis() - lastChange > SAVE_DELAY_MS) {
    EEPROM.put(0, cal);
    calDirty = false;
  }

  Joystick.setXAxis(out[ROLL]);
  Joystick.setYAxis(out[PITCH]);
  Joystick.setZAxis(out[BRAKE]);
  Joystick.setButton(0, !digitalRead(PIN_TRIM_UP));
  Joystick.setButton(1, !digitalRead(PIN_TRIM_DOWN));
  Joystick.sendState();
}
