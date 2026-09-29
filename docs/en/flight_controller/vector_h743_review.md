# Vector H743 Hammer AC Compatibility Review

Reviewed 25 September 2026 against the current working tree, the generated `vector_h743_default` startup scripts and parameter metadata, and the recorded 24 September first boot.
The reference is `/home/bweeber/Documents/repos/ardupilot-vector-master`, specifically `libraries/AP_HAL_ChibiOS/hwdef/vector-H743`, `vector_configs/Hammer-V-Ardupilot-params-AC.param`, and the implementations behind those settings.
This is a source/configuration review, not a new hardware test or flight qualification.

## Implementation Update

The findings below describe the pre-change review snapshot.
The subsequent RC/OSD bench implementation addresses power-topic publication, console/TEL2 ownership, receiver mapping, GNSS offsets, Vector OSD transport, and initial Tramp settings.
It retains microSD parameter storage with the user's agreement.
See [Vector H743 Bench Verification](vector_h743_bench.md) for current behaviour, limitations, and outstanding hardware checks.

## Conclusion

The current Hammer airframe is a partial compatibility baseline.
Motor allocation, primary sensor buses, board rotation, and UART pin assignments match the reference.
RC configuration, power reporting, persistent storage without an SD card, telemetry behaviour, VTX settings, auxiliary outputs, and Vector OSD still have gaps.
The existing bench evidence confirms boot, USB communication, IMU sampling, and barometer sampling only.

## Issues To Resolve

### 1. Power Status Is Not Published

`boards/vector/h743/src/board_config.h:83` defines raw ADC inputs, but does not provide `BOARD_ADC_USB_CONNECTED`.
`src/drivers/adc/board_adc/ADC.cpp:200` compiles out system-power publication without this macro.
The board also uses `ADC_SCALED_5V_CHANNEL` and `ADC_SCALED_3V3_CHANNEL` names rather than the system-power publisher's `ADC_SCALED_V5_SENSE` and `ADC_SCALED_V3V3_SENSORS_SENSE` interface.
The first boot recorded `system power unavailable` and an unpublished `system_power` topic.
Resolve the power/USB validity and rail-scaling interface using the actual board circuitry; do not replace this with an arming-check bypass.

### 2. TEL2 Shares The Console

`boards/vector/h743/nuttx-config/nsh/defconfig:233` selects USART1 as the NuttX serial console.
That is also TEL2, `/dev/ttyS0`, selected by `MAV_1_CONFIG=102` in the Hammer airframe.
`CONFIG_NSH_CONSOLE=y` remains enabled in the generated NuttX configuration.
Console buffering does not isolate this port: `platforms/nuttx/src/px4/common/console_buffer.cpp:200` forwards buffered output to the original console on stderr.
Consequently console messages can contaminate MAVLink traffic, and the console and MAVLink readers can compete for received bytes.
Give the console and MAVLink separate ownership before using TEL2 as a reliable telemetry link.

### 3. Receiver Selection Does Not Configure Pilot Controls

`4091_hammer_v_ac:100` selects the USART6 receiver port, but never assigns the stick, arm, or flight-mode channels.
The generated parameter defaults have `RC_MAP_ROLL`, `RC_MAP_PITCH`, `RC_MAP_THROTTLE`, `RC_MAP_YAW`, `RC_MAP_ARM_SW`, and `RC_MAP_FLTMODE` all zero (unassigned).
The ArduPilot file uses throttle channel 1, roll channel 2, pitch channel 3 with `RC3_REVERSED=1`, and yaw channel 4.
It also uses channel 6 for flight modes and auxiliary switch functions including channel 5 arming.
The PX4 manual-control source defaults to `COM_RC_IN_MODE=3`, retaining the first valid RC/MAVLink source until reboot.
Receiver calibration, control direction, switch mapping, mode selection, and loss-of-link behaviour must be set and tested explicitly.
ArduPilot numeric mode and auxiliary-function IDs cannot be copied directly into PX4.

### 4. Parameters Require An SD Card

`boards/vector/h743/default.px4board:3` stores parameters at `/fs/microsd/params`.
The ArduPilot hardware definition instead reserves internal flash for parameter storage.
The user confirmed no card was inserted during the first boot, and the log records failed parameter exports and an uncalibrated accelerometer.
Install and verify an SD card before calibration, or implement a suitable flash-backed parameter store.
A reboot must demonstrate that calibration and settings persist.

### 5. Vector OSD Is Unimplemented

`boards/vector/h743/init/rc.board_extras:2` deliberately does not start an OSD driver.
ArduPilot's `OSD_TYPE=6` selects `libraries/AP_OSD/AP_OSD_VECTOR.cpp`, which uses a custom write-only SPI protocol and a Vector font-selection register.
PX4's standard ATXXXX driver probes through register reads and cannot simply be enabled as an equivalent backend.
SPI2 and PD10 chip select are described in the port, but this is not a working overlay implementation.
Port the Vector backend and required display content before expecting OSD telemetry.
Tramp VTX control does not provide the video overlay.

### 6. Tramp Has No Default Frequency/Power Table

The airframe starts Tramp on USART2, but supplies neither a VTX table nor Hammer's band/channel/power settings.
`ROMFS/px4fmu_common/init.d/rc.vtxtable:8` only attempts to load a saved table.
`src/drivers/vtxtable/config.h:540` initialises an empty table; without a saved configuration, frequency and power lookups return zero.
`src/drivers/vtx/tramp.cpp:128` rejects a zero power lookup, and `src/drivers/vtx/protocol.h:154` derives the channel frequency from this table.
ArduPilot requests RaceBand channel 1 (5658 MHz), 25 mW, and a 2500 mW maximum.
Supply a table appropriate to the actual transmitter and map these settings deliberately; `VTX_POWER` is a table index in PX4, not an ArduPilot milliwatt value.
With the previously reported missing SD card, there is no saved table to load.

### 7. Serial Protocols Are Only Partially Equivalent

The UART/pad assignments match the reference, and the generated `rc.serial_port` mapping agrees with the airframe.
The following describes configured roles, not verified external-device communication.

| ArduPilot Port | UART / PX4 Device | PX4 Role | Remaining Difference |
| --- | --- | --- | --- |
| SERIAL0 | USB | MAVLink / MAVLink shell | First boot verified |
| SERIAL1 | UART7 / ttyS5 | TEL1, MAVLink onboard, 921600 baud | Custom GPSD stream not reproduced |
| SERIAL2 | USART1 / ttyS0 | TEL2, MAVLink normal, 115200 baud | Console conflict described above |
| SERIAL3 | USART2 / ttyS1 | TEL3, Tramp half duplex, protocol baud 9600 | Table/settings absent; hardware communication unverified |
| SERIAL4 | UART4 / ttyS3 | TEL4 device available, no Hammer service | ArduPilot custom protocol 51 unsupported |
| SERIAL5 | USART3 / ttyS2 | GNSS, configured 230400 baud | Receiver detection, fix, and configuration unverified |
| SERIAL6 | USART6 / ttyS4 | Serial RC autodetection | Stick/switch setup absent; receiver unverified |
| SERIAL7 | UART8 / ttyS6 | ESC serial telemetry | RPM/telemetry reception unverified |

The port clears the generic board's USART2 TX/RX swap when starting the VTX driver, so Hammer Tramp uses the normal TX pad in single-wire mode.
ArduPilot protocol 52 is more than a baud rate: `libraries/GCS_MAVLink/GCS_Common.cpp:6941` configures a dedicated GPSD stream including custom GNSS messages and particular rates.
PX4's ordinary onboard MAVLink stream does not reproduce it.
The ArduPilot TIM3/PB1 PPS output is also only reserved in this PX4 port, not generated.

### 8. Control Tuning And Auxiliary Functions Are Incomplete

The airframe translates selected motion limits and filters, but retains generic PX4 attitude/rate-controller gains.
It does not reproduce Hammer's thrust curve, hover calibration, motor spool behaviour, switch functions, or failsafe settings.
For example, Hammer has `MOT_THST_EXPO=0.65`, `MOT_SPIN_ARM=0.05`, and `MOT_SPIN_MIN=0.09`, while this build retains `THR_MDL_FAC=0` and `DSHOT_MIN=0.055`.
These parameters have different controller semantics and require measured validation rather than blind numerical copying.
ArduPilot's second, FFT-tracked notch is not reproduced by the selected PX4 ESC-RPM notch alone.
Hammer's auxiliary output mappings are absent: output 5 receives RC9, output 6 mapped RC13, output 7 mapped RC12, output 8 mapped RC14, and output 10 mapped RC11.
The pins exist, but those output functions are not assigned in the PX4 airframe.
ArduPilot's alternative external-vision estimator source configuration is also not translated.

## Motor Allocation And Rotation

For `SYS_AUTOSTART=4091`, the rotor positions and torque signs agree with ArduPilot `FRAME_TYPE=18` combined with `SERVO1..4_FUNCTION=36,33,34,35`.
Do not use the generic `4090` airframe as a substitute: its motor geometry/order differs.

| Physical Vector Output | MCU Pin | PX4 Motor | ArduPilot Motor | Position | Expected Rotation Viewed From Above |
| --- | --- | --- | --- | --- | --- |
| 1 | PA0 / TIM5_CH1 | 1 | 4 | Front left | CCW |
| 2 | PA1 / TIM5_CH2 | 2 | 1 | Rear right | CCW |
| 3 | PA2 / TIM5_CH3 | 3 | 2 | Front right | CW |
| 4 | PA3 / TIM5_CH4 | 4 | 3 | Rear left | CW |

Evidence: `4091_hammer_v_ac:14`, `boards/vector/h743/src/timer_config.cpp:10`, ArduPilot `AP_MotorsMatrix.cpp:639`, and `src/modules/control_allocator/module.yaml:222`.
`PWM_MAIN_TIM0=-6` selects bidirectional DShot600, matching Hammer's DShot600 and bidirectional telemetry mask for outputs 1–4.
The default 14 motor poles also matches Hammer's `SERVO_BLH_POLES=14`.
The generic `4090` comment incorrectly equates ArduPilot `MOT_PWM_TYPE=5` with DShot600; Hammer itself uses `MOT_PWM_TYPE=6`.

The rotor torque signs tell the controller which direction to expect; they do not program ESC rotation.
Existing ESC direction settings should remain compatible if the same motors, wiring, and ESC configuration are retained.
Actual rotation and output identity remain unverified until each motor is checked individually with propellers removed, as described in the [PX4 actuator checks](https://docs.px4.io/main/en/config/actuators).
Bidirectional DShot refers to telemetry, not automatic reversal of the motor.

## Sensor Status

| Sensor / Function | Configuration | Evidence And Remaining Work |
| --- | --- | --- |
| Accelerometer and gyro | ICM45605 through extended icm45686 driver, SPI1, PE9 CS | First boot produced samples with no recorded transfer/FIFO errors; accelerometer still needs calibration |
| IMU ranges | ICM45605: +/-16 g and +/-2000 deg/s | Driver scales match those selected ranges; dynamic performance not tested |
| Board orientation | `SENS_BOARD_ROT=8`, roll 180 degrees | Matches `AHRS_ORIENTATION=8`; verify displayed attitude against the installed aircraft |
| Barometer | BMP388-family driver, I2C2, address 0x76 | First boot identified BMP390 chip ID 0x60 and live pressure/temperature with no recorded transfer errors |
| External compass | I2C1 external bus, common magnetometer drivers, automatic external probing | Current defaults are `SYS_HAS_MAG=1`, `SENS_EXT_I2C_PRB=1`; detection/calibration/orientation not demonstrated |
| GNSS | USART3, GNSS driver enabled | No recorded fix validation; Hammer antenna offsets `(0.07, -0.02, -0.15)` metres not transferred |
| Battery voltage/current | PC0/PC1 ADC, divider 11, current scale 160 A/V | Scaling matches Hammer settings; no battery-powered measurement verification; power-status interface is defective |
| ESC RPM | Bidirectional DShot600 and UART8 telemetry selected | No verified RPM feedback; dynamic notch depends on usable feedback |

Hammer enables compass fusion and sets `COMPASS_ORIENT=10`; this port has no corresponding calibrated external-magnetometer orientation.
The earlier board note about `SYS_HAS_MAG=0` does not describe the current defaults.
Generic external probing makes supported sensors discoverable; it does not establish that the fitted device was found or that its orientation is correct.

## Verification And Next Steps

This review used source comparison, generated startup scripts/parameter metadata, and existing bench logs.
No firmware, controller parameters, ESC settings, or motor state were changed.
The recorded hardware evidence is in `/home/bweeber/Documents/flight-controller-backups/vector-H743-200364500000-20260924-115557/`, principally `FIRST_BOOT.md` and `first-boot-shell.log`.

1. Fix power reporting and console/TEL2 ownership; establish persistent parameter storage.
2. Complete receiver, switches, failsafes, sensor orientation/calibration, and GNSS offsets.
3. Supply VTX configuration and implement the Vector OSD backend; resolve required custom telemetry/PPS/auxiliary functions.
4. Validate reboot persistence, sensor health, serial peripherals, and video overlay on the bench.
5. Check each motor's identity, direction, idle operation, and RPM feedback with propellers removed.
6. Validate controller gains, thrust behaviour, and filtering before progressing to flight testing.
