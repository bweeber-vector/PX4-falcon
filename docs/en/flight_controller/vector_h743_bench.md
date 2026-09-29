# Vector H743 Bench Verification

This milestone verifies sensors, the mLRS receiver, and the Vector video overlay before actuator testing.
Use Hammer airframe `4091`, insert a microSD card, disconnect the motors, and power the controller through USB.
The firmware does not require an updated bootloader if the previously installed Vector PX4 bootloader is present.

## Build And Scope

Build `vector_h743_default` using the repository Linux build skill.
On this machine the previously installed Python dependencies are in `/tmp/px4-vector-python`:

```sh
PYTHONPATH=/tmp/px4-vector-python make vector_h743_default
```

The application is `build/vector_h743_default/vector_h743_default.px4`.
The application address remains `0x08020000`; the bootloader and board ID remain unchanged.
This milestone adds the Vector write-only OSD protocol to `atxxxx`, RC configuration, GNSS antenna offsets, ADC power reporting, exclusive TEL2 MAVLink ownership, and initial Tramp settings.
It does not qualify controller gains, motors, or flight behaviour.

## Stored Parameters And SD Card

Airframe values are defaults: saved user parameters take precedence.
Read back the parameters below after updating; do not assume a firmware upload resets existing settings.
Keep an export of any existing calibration before changing it.

```sh
mount
param show SYS_AUTOSTART
param show RC_MAP_*
param show RC3_REV
param show COM_RC_IN_MODE
param show COM_FLTMODE*
param show OSD_ATXXXX_CFG
param show SENS_GPS0_OFF*
```

`/fs/microsd` must be mounted and parameter export must succeed.
After calibrating in *QGroundControl*, run `param save`, reboot, reconnect, and check that the calibration and settings persist.
A card in an unsupported filesystem must be handled explicitly; the verification script does not format it.

## Receiver

The receiver is mLRS.
Its OUT port can supply CRSF, SBUS, or inverted SBUS depending on receiver configuration.
The ArduPilot file selects serial RC autodetection and does not identify the actual wire protocol.
The [mLRS receiver settings](https://github.com/olliw42/mLRS-docu/blob/main/docs/PARAMETERS.md) document these choices.
This port keeps autodetection on USART6 (`/dev/ttyS4`); `rc_input status` and `input_rc.input_source` identify the detected protocol.
CRSF is source 14 and SBUS is source 9 for this driver.
If the receiver is configured for inverted SBUS relative to the driver's expectation, select CRSF or the appropriate SBUS polarity on the receiver instead of assuming autodetection tries both polarities.

The mLRS MAVLink serial link is separate from the receiver OUT/RC wire.
TEL2 runs MAVLink at 115200 baud with flow control disabled; the receiver's serial baud must match if that link is connected there.
A receiver that supplies RC only through MAVLink needs an explicit different setup; this profile expects a physical RC signal on USART6.
See the [mLRS CRSF and MAVLink setup](https://github.com/olliw42/mLRS-docu/blob/main/docs/CRSF.md).

| Function | Hammer Channel / Default | Bench Expectation |
| --- | --- | --- |
| Throttle | 1, range 1000–2000 | Mapped throttle increases from -1 to +1 |
| Roll | 2, centred at 1500 | Right stick gives positive mapped roll |
| Pitch | 3, centred at 1500, `RC3_REV=1` | Forward stick gives positive mapped pitch (nose down) |
| Yaw | 4, centred at 1500 | Right stick gives positive mapped yaw |
| Arm switch | 5 | Assigned, keep disarmed during this milestone |
| Flight modes | 6 | Acro / Acro / Stabilized / Stabilized / Position / Position |

ArduPilot applies `RC3_REVERSED=1` before its positive-nose-up attitude input.
PX4 manual pitch is positive-nose-down, so retaining PX4 `RC3_REV=+1` gives the equivalent requested pitch direction for the same receiver output.
Confirm this physically; do not infer transmitter channel order solely from the mLRS brand.
PX4's switch thresholds differ from ArduPilot's, so verify all available positions of the actual transmitter switch.
Airmode remains disabled pending controller tuning.
Calibrate receiver endpoints in *QGroundControl* rather than treating imported nominal endpoints as measurements.

```sh
rc_input status
listener input_rc -n 1
listener manual_control_setpoint -n 1
listener manual_control_switches -n 1
```

Move one stick at a time and confirm raw channels and mapped directions.
Then switch the transmitter off and confirm `rc_lost` or `rc_failsafe` becomes true and mapped input becomes invalid.
Restore the transmitter and confirm recovery.
A receiver configured to continue plausible channel values on radio loss can hide the loss from the flight controller; this must be caught in this test.
Keep the arm switch in its disarmed position throughout.

## OSD And Video Standard

ArduPilot's Vector backend starts in PAL; it has no register readback with which to discover the camera standard.
The PX4 default is therefore `OSD_ATXXXX_CFG=2` (PAL).
A rolling, displaced, or missing overlay needs checking against the camera/receiver standard and video power supply.
To try NTSC, set `OSD_ATXXXX_CFG=1`, save, and reboot.
The driver reads this setting at startup.
`OSD_ATXXXX_HOFF=15` and `OSD_ATXXXX_VOFF=16` reproduce the horizontal/vertical offsets read from this aircraft's ArduPilot configuration; these also take effect after restarting the driver or rebooting.

Startup command:

```sh
atxxxx -s -b 2 -m 3 -f 5000 -V start
```

Do not start a second copy manually when the board has already started it.
Use `atxxxx status` to inspect it.
The `-V` option bypasses readback only for the explicitly selected Vector protocol; it sends the Vector ArduPilot-font register and uses register/value writes for each character.
The entire screen refreshes at 5 Hz, including blank cells, so stale characters are cleared and a display powered after the FC can acquire the current screen.
PAL has 16 rows; NTSC has 13, and all diagnostic content fits both.

`OSD_VEC_LAYOUT=1` selects the Hammer flight display, using the saved aircraft's ArduPilot `OSD1_*` positions and Vector-specific font map.
`OSD_VEC_LAYOUT=0` selects the original bench diagnostics; the change takes effect without rebooting.
The diagnostic screen shows uptime, arm/preflight state, flight mode, RC protocol/channel count, raw channels 1–4, mapped stick values, sensor availability and battery readings.
`DATA` means recent published data, not calibrated or flight-qualified sensor health.

### Hammer Screen

Coordinates below are zero-based character columns and PAL rows, matching the saved ArduPilot parameters.
The compass and horizon coordinates identify their centres.

| Information | Column | Row | PX4 Source / Units |
| --- | --- | --- | --- |
| Flight timer | 0 | 0 | Armed and airborne time since OSD driver start, minutes:seconds |
| Compass ribbon | 15 | 0 | Estimated attitude yaw |
| Link quality | 26 | 0 | Receiver link quality, percent |
| Ground speed and direction | 0 | 1 | Estimated horizontal velocity, km/h; arrow relative to heading |
| Heading | 13 | 1 | Estimated attitude yaw, degrees |
| RSSI | 26 | 1 | Receiver RSSI, percent |
| Home arrow and distance | 12 | 2 | Fused global position and home, metres / kilometres |
| Efficiency | 1 | 7 | Current divided by ground speed, mAh/km; dashes below 2 km/h |
| Artificial horizon | 15 | 7 | Estimated roll and pitch |
| Average cell voltage | 24 | 7 | Battery voltage divided by OSD cell count |
| Vertical speed | 2 | 8 | Negative NED vertical velocity, m/s |
| Current | 25 | 8 | Filtered battery current, amperes |
| Home-relative altitude | 1 | 9 | Estimated altitude above home, metres |
| Messages | 2 | 10 | PX4 failsafe/RC/prearm state and recent warning log text |
| Travelled distance | 22 | 11 | Integrated horizontal speed while armed, since driver start |
| Callsign | 12 | 13 | First 12 characters of SD-root `callsign.txt`, uppercase |
| Mode and arming icon | 25 | 13 | Four-character PX4 mode abbreviation and actual arm state |
| GNSS satellites | 0 | 14 | Receiver satellite count and fix state |
| Plus Code | 11 | 14 | Ten-digit Open Location Code from current GNSS coordinates |
| VTX power | 25 | 14 | Last published VTX power-table label; zero in pit mode |

In NTSC, the last two populated rows move to rows 11 and 12 and travelled distance moves to row 9.
The lower horizon is clipped to keep messages and footer information readable.
PAL reproduces the recorded widget positions directly.
The original Vector font has no vertical horizon subcell glyphs; steep-bank lines use the ASCII vertical stroke.

Missing, stale, invalid or unavailable telemetry shows dashes or an explicit warning.
Low cell voltage (below 3.6 V), low RSSI (below 30 percent), and fewer than nine satellites or no 3D fix blink, matching the saved warning thresholds.
A current attitude estimate is not proof that the compass or accelerometer has been calibrated correctly.
Home altitude and distance remain unavailable until home and position are valid.

`OSD_VEC_CELLS=6` reproduces ArduPilot's saved `OSD_CELL_COUNT=6` for the average-cell display only.
Set it to zero to use `battery_status.cell_count` instead, or set it to the actual pack cell count.
This does not set `BAT1_N_CELLS`, calibrate the battery monitor, or change battery failsafes.
The capacity icon is omitted until the battery monitor supplies a usable cell count and remaining estimate; valid cell voltage remains visible.
Unavailable directions use dashes instead of question marks.
No callsign is invented if the SD file is absent; adding or changing the file takes effect at driver restart.

The VTX topic supplies a table label, not an RF power measurement, and publishes only when its reported state changes.
Communication failure reported by the VTX driver replaces the value with dashes; an unexpectedly stopped VTX driver can leave its last value visible.
The OSD does not change VTX settings.
The saved table's single entry remains 25 mW.

PX4 mode names and prearm checks are retained rather than displaying ArduPilot controller names.
Warning log text scrolls for up to ten seconds; not every PX4 event has a corresponding text log.
Failsafe and RC-loss warnings take precedence.
Flight statistics are accumulated by this driver and reset at driver restart; they do not reproduce ArduPilot's persisted statistics.

A successful SPI write has no acknowledgement from this hardware.
`frames sent` proves driver activity, not a visible display.
In diagnostics, visually verify uptime advances and stick values track movement.
In the Hammer display, verify the horizon follows board motion, heading/home-arrow directions are correct, the lower rows align, voltage/current match telemetry, and transmitter loss changes the message.
The diagnostic overlay was confirmed updating on the aircraft before this layout change.
The Hammer flight display was uploaded through the PX4 bootloader on 25 September 2026, with uploader verification passing.
The controller rebooted disarmed; the OSD frame count advanced with zero SPI transfer failures, and Tramp still reported 25 mW at 5658 MHz.
The user confirmed the Hammer layout was visible and improved on the diagnostic layout.
A subsequent candidate replaced question-mark placeholders with dashes or an omitted capacity icon and was uploaded with bootloader verification passing.
Visual confirmation of that correction remains pending.
At that check RC input was still absent, sensor calibration was incomplete, and the battery monitor had no configured cell count; these remain separate bench tasks.
A photograph of the screen is useful evidence.
If USB does not power the camera, OSD supply, or VTX rail, no video is expected until those components have an appropriate bench supply.
Determine the board's power routing before adding another supply.

## VTX

The Tramp driver uses USART2's normal TX pad in half duplex at its protocol baud of 9600.
The Hammer baseline requests 5658 MHz and starts with a single 25 mW power-table entry.
Higher power settings are intentionally left for configuration against the fitted VTX; do not enter a milliwatt value directly into PX4's `VTX_POWER`, which is an index.
A saved VTX table takes precedence over the initial table and must be inspected when reusing an SD card.

```sh
vtxtable status
vtx status
```

Check both requested settings and the device's returned settings.
An unpowered VTX may report not found even when the UART configuration is correct.
Video-transmitter control and the SPI video overlay are separate functions.

## Sensors And Power

```sh
icm45686 status
bmp388 status
listener sensor_accel -n 1
listener sensor_gyro -n 1
listener sensor_baro -n 1
listener vehicle_magnetometer -n 1
listener vehicle_gps_position -n 1
listener system_power -n 1
listener battery_status -n 1
```

The IMU is ICM45605 on revision 1, serviced by the extended `icm45686` driver; the barometer is BMP390, serviced by `bmp388`.
Confirm fresh samples, finite values, and no rising driver error counters.
Check that the attitude display follows physical movement with board orientation roll 180 degrees, then perform sensor calibration.
GNSS antenna offsets `(0.07, -0.02, -0.15)` metres are supplied through `SENS_GPS0_OFFX/Y/Z`.
An indoor no-fix result is different from no GNSS messages.

External magnetometers are probed on I2C1.
Identify the detected compass and its calibration slot before applying Hammer's external-compass orientation (rotation 10, roll 180 degrees/yaw 90 degrees), then calibrate and verify heading.
Do not write an orientation to an assumed slot or reuse another board's calibration IDs/offsets.

`system_power` now reports measured 5 V and two 3.3 V rails.
Check these against actual measurements; the voltage dividers follow the ArduPilot hardware definition.
USB presence uses received USB-frame progress, because this hardware definition has no VBUS sense pin.
It cannot identify a charge-only cable and can report disconnected when the host suspends USB.
The inherited brick-valid flag is not a physical battery-presence measurement; consult `battery_status.connected` and measured voltages.
No arming checks are bypassed to make the bench pass.

## Automated Evidence Capture

Close *QGroundControl* before opening the USB port with the script:

```sh
python3 Tools/vector_h743_flash.py --check-only --peripherals
```

This mode performs no DFU operations, flash writes, parameter changes, arming, or actuator commands.
It records sensor/power samples, receiver state, mapped inputs, switch state, OSD transport status, compass/GNSS samples, and VTX configuration.
It rejects lost/stale/invalid receiver input and failed OSD transport.
It does not certify visual OSD operation, all stick/switch directions, receiver loss response, calibration persistence, or GNSS/compass accuracy; complete those observations separately.
Preflight failures such as missing calibration or battery/USB restrictions can still produce exit code 3 during a USB bench session.
Read the individual findings rather than treating that code as a complete peripheral diagnosis.

## Host Verification

```sh
python3 -m unittest discover -s test -p test_vector_h743_flash.py
c++ -std=c++17 -Wall -Wextra -Werror -I. test/vector_osd_protocol_test.cpp -o /tmp/vector-osd-test
/tmp/vector-osd-test
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I. test/vector_hud_test.cpp -o /tmp/vector-hud-test
/tmp/vector-hud-test
```

The OSD test decodes every generated PAL/NTSC cell address, including the 255/256 boundary, and checks output bounds and invalid-input rejection.
The HUD test checks Vector glyph codes and cardinal arrows, widget positions, unit conversion, pitch/roll direction, warning blinking, Plus Code reference coordinates, and PAL/NTSC bounds including steep/inverted attitudes.
Hardware verification is still required after the build and host tests pass.
