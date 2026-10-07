# Changelog

## v1.0.0 (2026-10-07)

- First release. Pitch, roll and hand brake come from three AS5600s on TCA9548A SD2/SD3/SD4 and report as X/Y/Z. Trim up and trim down are on D4/D5 as buttons 1 and 2.
- Centre is taken at power-up, extents are learnt and saved to EEPROM with wrap-safe offsets, and holding both trims at plug-in resets the calibration.
- Custom board `hardware/c42`: the device shows up over USB as "C42 Control Stick" (0x1209:0x0001).
