# Vector H743

:::warning
This is a development port based on the local ArduPilot `vector-H743` hardware definition.
The PX4 image has booted on a Vector board, with USB/MAVLink, the command shell, the IMU, and the barometer checked on the bench.
It has not been flight tested.
Manufacturer support, physical connector types, a purchase source, and a flight log are not yet available for this port.
:::

## Firmware And Flash Layout

Build the application with `make vector_h743_default` and the PX4 bootloader with `make vector_h743_bootloader`.
The MCU is an STM32H743 with a 24 MHz crystal and 2 MiB of flash.
The bootloader occupies `0x08000000` through `0x0801ffff`; the application begins at `0x08020000`.
The PX4 firmware board ID is `6700`, matching the ArduPilot Vector board ID.
The USB application and bootloader descriptors currently use the ArduPilot source build's `1209:5741` VID/PID for local evaluation.
A product-specific USB allocation is needed before distribution.

Program the PX4 bootloader with an SWD programmer or STM32 ROM DFU at `0x08000000`, then use the PX4 USB bootloader for the `.px4` application image.
The existing ArduPilot bootloader has not been verified to accept the PX4 image.
Do not assume that a matching board ID makes the bootloaders interchangeable.

Parameters are stored at `/fs/microsd/params`; a microSD card is needed for persistent settings.
ArduPilot instead reserves flash page 14 for its 32 KiB parameter store.
This port has not reserved that flash page, so the two firmware images do not share parameter storage.

## Scripted Backup, Flashing, And Health Checks

`Tools/vector_h743_flash.py` automates the STM32 ROM DFU workflow on Linux.
It requires `dfu-util` and a Python environment containing `pyserial` and `pymavlink`.
Build both `vector_h743_bootloader` and `vector_h743_default` first.
The script uses their existing build artifacts; it does not build firmware.

Close *QGroundControl*, disconnect the flight battery, remove propellers, and connect the controller in STM32 ROM DFU mode.
The DFU USB identity is `0483:df11`.
From the PX4-Autopilot repository root, run:

```sh
python3 Tools/vector_h743_flash.py
```

The script displays the selected device and asks you to type `FLASH <serial>` to confirm the board and bench setup.
It then performs these steps:

1. Read all 2 MiB of current flash twice and require an exact match.
   Save both full-flash images, a separate 128 KiB bootloader-sector image, and the option-byte snapshot before any erase or write.
2. Program the new bootloader at `0x08000000` and verify every byte by reading it back.
3. Program PX4 at `0x08020000` and read back the entire flash.
   Verify both images, the erased sector padding, and preservation of untouched sectors and option bytes.
4. Prompt you to unplug and reconnect USB normally, without holding BOOT/RESET, using the same USB connector.
5. Check the PX4 heartbeat, disarmed state, board and firmware identity, airframe, IMU/barometer data, system-power publication, microSD mount, and preflight status.
   Save telemetry, shell output, and health findings.

A read failure, short read, unequal backups, or failure to save the backup stops the script before any erase or write.
A bootloader verification failure stops it before the application write.
Later failures preserve the session files and stop further operations; there is no automatic rollback or read-unprotect operation.
The script never writes option bytes, arms the vehicle, runs motors, calibrates sensors, or changes parameters.

Each run creates a new private directory under `~/Documents/flight-controller-backups/`.
It contains `original-bootloader.bin`, both `original-flash-read*.bin` files, `original-option-bytes.bin`, exact copies of the new images, transfer logs, `manifest.json`, and recovery notes.
The full-flash backup also preserves flash-stored ArduPilot parameters, but does not include microSD contents.
Keep earlier backups: rerunning the script after installing PX4 backs up the current PX4 installation.

Defaults select the local Vector build outputs and expect Hammer airframe `4091`.
Both image packages must identify board `6700`.
When supplying a raw `.bin`, keep its matching build-generated `.px4` package alongside it so the script can check board metadata and payload equality.
Use `--bootloader` and `--firmware` for other image paths, `--dfu-serial` to select among multiple DFU devices, or `--backup-dir` to choose where sessions are saved.
Use `--expected-airframe 4090` when intentionally checking the generic Vector airframe.

To repeat just the health checks on a normally booted controller:

```sh
python3 Tools/vector_h743_flash.py --check-only
```

This mode performs no DFU or flash operations.
Use `--port /dev/ttyACM0` if more than one Vector USB serial port is present.
Exit code `0` means the basic bench checks passed, `1` indicates an operation or communication failure, `2` indicates invalid command-line arguments, and `3` means boot verification completed with health issues.
A successful bench check does not establish flight readiness.

### Initial Bench Findings

The first hardware boot confirmed Hammer airframe `4091`, live accelerometer and barometer data, and no observed sensor transfer errors.
No microSD card was inserted, so parameter saves failed; insert a card while powered off before calibration or changing persistent settings.
The accelerometer also needed calibration.
The current board configuration lacks `BOARD_ADC_USB_CONNECTED`, so `board_adc` does not publish `system_power` and PX4 reports system power unavailable.
This requires a board-port fix before arming; it is not explained solely by USB-only power.

## Pin Mapping

The mapping below comes from `libraries/AP_HAL_ChibiOS/hwdef/vector-H743/hwdef.dat` in the local ArduPilot reference checkout.
It lists MCU pads, not connector pin numbers.

| Function | MCU pins |
| --- | --- |
| USB FS | PA11 D−, PA12 D+ |
| IMU ICM45605, revision 1; ICM45686, revision 0 | SPI1 PA5 SCK, PA6 MISO, PD7 MOSI, PE9 CS; PE13 IMU power enable |
| Vector OSD | SPI2 PB13 SCK, PC2 MISO, PB15 MOSI, PD10 CS |
| External compass/GNSS I²C | I²C1 PB6 SCL, PB7 SDA |
| Internal BMP388 barometer | I²C2 PB10 SCL, PB11 SDA; address `0x76` |
| GNSS serial | USART3 PD8 TX, PD9 RX |
| TELEM1 / TELEM2 | UART7 PE8 TX, PE7 RX / USART1 PB14 TX, PA10 RX |
| VTX / payload serial | USART2 PD5 TX, PD6 RX / UART4 PB9 TX, PB8 RX |
| RC / ESC telemetry | USART6 PC6 TX, PC7 RX / UART8 PE1 TX, PE0 RX |
| CAN1 | PD1 TX, PD0 RX |
| Motor outputs 1–4 | TIM5 PA0, PA1, PA2, PA3 |
| PWM outputs 5–8 / 9–10 | TIM4 PD12–PD15 / TIM15 PE5–PE6 |
| Buzzer | PA15 |
| LEDs | PC13 blue, PC15 green, PC14 red; active low |
| microSD | SDMMC1 PC8–PC12, PD2 |
| Battery voltage/current | ADC1 PC0 / PC1 |
| Other rail ADC inputs | PA4 5 V, PA7 3.3 V, PC4 12 V, PC5 sensor VDD |
| Payload control GPIO | PE3, PE4, PB2 |
| ArduPilot PPS output | TIM3 PB1; not yet implemented in PX4 |

Physical connector pin order and connector part numbers need confirmation against the board schematic before wiring.

## Startup And Airframes

The board startup enables the IMU power rail, probes the ICM45605 or ICM45686 on SPI1 at 8 MHz, starts the BMP388 on internal I²C2, and enables PX4's external I²C compass probe. ADC, microSD, CAN1, and ten timer outputs are configured.

`4091_hammer_v_ac` is the new default airframe for the Hammer V ArduCopter parameter set. `4090_vector_h743_quad_x` remains available as the generic Vector quad X baseline. An existing microSD parameter store may retain its previous `SYS_AUTOSTART`; select 4091 and reboot to use the Hammer profile. TIM5 outputs 1–4 use bidirectional DShot600 in both airframes, while outputs 5–10 remain PWM.

The Hammer profile follows `vector_configs/Hammer-V-Ardupilot-params-AC.param` in the local ArduPilot checkout. It selects the BetaFlight X reversed rotor geometry (`FRAME_TYPE=18`) and physical output order: 1 front left CCW, 2 rear right CCW, 3 front right CW, and 4 rear left CW. It sets the board rotation to roll 180°, battery voltage divider to 11, current scale to 160 A/V, and capacity to 15 Ah. The latter is a vehicle-specific calibration; the generic board current scale is 15 A/V.

The airframe translates Hammer's 280°/s acro rates, 80° maximum air tilt, position speed and acceleration limits, and 60 Hz IMU low-pass settings where PX4 has corresponding parameters. PX4 constrains manual tilt to 70°, climb to 8 m/s, descent to 4 m/s, and landing descent to at least 0.6 m/s, so those values cannot exactly reproduce ArduPilot's limits. The RPM-tracked gyro notch is an approximation and needs ESC RPM data and log inspection. ArduPilot's attitude and rate PID numbers are not directly portable to PX4 because the controllers use different gain definitions and control paths; the PX4 gains remain a starting point for flight-log-based tuning, not a validated Hammer tune.

### Serial roles for the Hammer airframe

| ArduPilot port and protocol | PX4 device and assignment | Status |
| --- | --- | --- |
| USB, MAVLink2 | USB CDC MAVLink | Configured |
| SERIAL1, UART7, custom `MAVLink_GPSD`, 921600 | `/dev/ttyS5`, TEL1 MAVLink onboard mode, 921600 | Standard PX4 streams at the link's automatic rate; custom message schedule is absent |
| SERIAL2, USART1, MAVLink2, 115200 | `/dev/ttyS0`, TEL2 MAVLink normal mode, 115200 | Configured |
| SERIAL3, USART2, half-duplex Tramp | `/dev/ttyS1`, TEL3 PX4 Tramp VTX driver | Configured in firmware; on-board UART swap is cleared before VTX opens |
| SERIAL4, UART4, custom `VECTOR_ESAD` | `/dev/ttyS3`, TEL4 reserved | No PX4 ESAD driver yet |
| SERIAL5, USART3, GNSS, 230400 | `/dev/ttyS2`, GPS1, 230400 | Configured |
| SERIAL6, USART6, serial RC input | `/dev/ttyS4`, RC protocol auto scan | Configured; receiver protocol and signal must be verified on hardware |
| SERIAL7, UART8, ESC telemetry | `/dev/ttyS6`, DShot ESC telemetry | Configured |

The generic 4090 airframe starts MAVLink on TEL1, TEL2, TEL3, and payload UART4, and uses the RC-labeled port for RC input. Its TEL3 MAVLink UART has TX/RX swapped at board startup to match the earlier ArduPilot baseline. In the Hammer profile, the VTX driver restores normal TX/RX pin mapping for half-duplex Tramp. TEL4 stays unused so a future ESAD driver can claim it. Port pin assignments match the ArduPilot hardware definition, but connector pin order is still unverified.

The ATXXXX driver now has an explicit `-V` backend for Vector's write-only SPI protocol and ArduPilot font.
Board startup selects SPI2, mode 3, 5 MHz, and PAL by default, matching the ArduPilot backend.
The default `OSD_VEC_LAYOUT=1` reproduces the saved Hammer ArduPilot OSD1 widget positions with PX4 telemetry and the Vector font map.
Select `OSD_VEC_LAYOUT=0` for the receiver/sensor diagnostic display.
See the [bench verification guide](vector_h743_bench.md#hammer-screen) for widget sources, cell-count configuration and remaining differences.
See [Vector H743 Bench Verification](vector_h743_bench.md) for PAL/NTSC selection and checks.

The Hammer profile now assigns TAER receiver channels, channel 5 arming, and channel 6 flight modes.
Pitch direction is translated to PX4's input convention rather than copying ArduPilot's reversal flag numerically.
GNSS antenna offsets are configured.
External compass probing is enabled with `SYS_HAS_MAG=1`; the fitted compass still needs identification, rotation selection, and calibration.

The UART console has been replaced by a RAM log so TEL2 belongs exclusively to MAVLink.
The ADC driver now publishes measured 5 V and 3.3 V rails in `system_power`.
There is no documented VBUS GPIO: USB presence is inferred from received USB frame activity and does not detect a charge-only cable or reliably distinguish a suspended host from disconnection.
`board_read_VBUS_state()` remains permissive solely so USB CDC can enumerate without a VBUS sense wire; it is not used as evidence of power validity.
The board has no documented power-good input, so the inherited brick-valid indication is not a measured battery-presence signal; use `battery_status.connected` and measured rail voltages during verification.

ArduPilot's TIM3/PB1 PPS output, custom GPSD message stream, protocol 51, external-vision source selection, and auxiliary output mappings remain unimplemented.
Controller gains, motor thrust behaviour, and failsafe equivalence still require validation before motor or flight testing.


## Hardware Bringup

Before any powered actuator test, confirm the 24 MHz clock, USB enumeration, microSD mount, parameter persistence, IMU and barometer output, compass probe, CAN, RC serial, ESC telemetry, and each PWM pin on a bench.
Check motor order, rotation direction, DShot timing, and arming behaviour with propellers removed.
Record a PX4 flight log before treating this target as flight ready.
