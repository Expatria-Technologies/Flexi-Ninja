![Logo](https://github.com/user-attachments/assets/ee8cc3e8-f3bd-4e02-be1e-11510bde26a1)
### Canadian-designed open source hardware. For everyone.

# Flexi-Ninja

Flexi-Ninja is a specialized fork of [stepper-ninja](https://github.com/atrex66/stepper-ninja) for Expatria's hardware. Flexi-Ninja replaces the breakout board abstraction with a PlatformIO based build system and board definitions in `config.h`. 

Different in this fork from upstream stepper-ninja:
- PIO-based step generation with added position feedback to LinuxCNC for closed-loop tracking
- Configurable DIR pin setup time per axis via HAL pin to meet stepper driver timing requirements
- FlexGPIO I2C IO expander Used for alarm inputs, tool/probe sensors, axis enables, coolant, spindle, and auxiliary outputs
- Hybrid encoder handling: high-speed PIO-based encoder (ENC1) and IRQ-based encoder (ENC2). ENC2 can be disabled to free pins as Aux inputs.
- Well-defined I/O with intuitive HAL pin names (every input has an inverted `-not` companion pin)
- All pin and module configuration in `config.h`

Using the SPI build of this firmware is recommended. To use this with LinuxCNC requires a Raspberry Pi 4 or 5 (Pi 5 strongly recommended). A pre-configured Pi image is available in the [Releases](https://github.com/Expatria-Technologies/Flexi-Pi/releases) section of the [Flexi-Pi](https://github.com/Expatria-Technologies/Flexi-Pi) repository, with setup notes for the Pi image in the [README](https://github.com/Expatria-Technologies/Flexi-Pi/blob/master/README.md) over there.

## Supported Boards

- [FlexiHAL 2350](https://github.com/Expatria-Technologies/FlexiHAL_2350)

## Flashing

**Recommended**: Use the `flash_flexi2350` script to flash both MCUs on the board via the Pi header.

**Alternative**: USB drag-and-drop using the UF2 bootloaders in the RP2350 and RP2040. You will need to be sure you flash both the RP2350 that runs the main firmware and the RP2040 which is used for FlexGPIO.


## Reference Config

A reference LinuxCNC configuration is included in [`config-samples/flexi-ninja/`](config-samples/flexi-ninja/). This is a QtDragon HD based configuration that will need editing for your specific machine.

- Edit `flexi-ninja.ini` for machine travels, limits, scale, DIR_SETUP, etc.
- Edit `flexi-ninja.hal` for VFD options (vfdmod or hy_vfd are included in the reference configutations) etc. 

## HAL Pin Structure

| Prefix | Pattern | Examples |
|--------|---------|----------|
| Inputs (GPIO) | `flexi-ninja.input.{NAME}` / `{NAME}-not` | `HALT`, `FEED_HOLD`, `X_LIM`, `AUXIN0-2` |
| Inputs | `flexi-ninja.input.{NAME}` / `{NAME}-not` | `X_ALM`, `Y_ALM`, `TOOL`, `PROBE` |
| Outputs | `flexi-ninja.output.{NAME}` | `X_EN`, `SPINDLE_ENABLE`, `MIST`, `AUXOUT_0-7` |
| Stepgen | `flexi-ninja.stepgen.{N}.{pin}` | `enable`, `command`, `feedback`, `dir-setup`, `debug-steps` |
| Encoder | `flexi-ninja.encoder.{NAME}.{pin}` | `position`, `velocity-rps`, `enabled`, `scale`, `filter-tau` |
| PWM | `flexi-ninja.SPINDLE_PWM.{pin}` | `value`, `enable`, `frequency` |

Every input pin has an inverted `-not` companion pin (e.g. `flexi-ninja.input.HALT-not`) to simplify HAL wiring. Use these if you need an inverted input rather than trying to invert it with separate components. 

## HAL Pinout

- TODO - Pinout diagram with HAL pin names.

## Stepgens

**STEP pulse timing**: `flexi-ninja.stepgen.pulse-width` sets the STEP pulse width in nanoseconds. Current minimum is 2500 ns (2.5 µs), which is supported by most common stepper drivers such as the DM542T.

**DIR setup timing**: `flexi-ninja.stepgen.{N}.dir-setup` sets the DIR pin lead time before the first STEP edge, in nanoseconds. It is configurable per per-axis, and the reference config includes this in the INI. `DIR_SETUP = 5000` for 5 µs.

## Encoders

- **ENC1**: High-speed PIO-based quadrature encoder. Tested to 500 kHz; will likely work at higher frequencies. Use as the primary encoder.
- **ENC2**: GPIO IRQ-based encoder (edge counting). Tested to 50 kHz. Can be disabled via the `enc_enabled` HAL pin. When it ENC 2 is disabled (pin name = 0) , the pins are available as `AUXIN0/1/2` inputs for general use.

## Building Firmware

PlatformIO is used for the firmware build system, it is recommended to use the PlatformIO plugin in VScode. Two build environments are available:

| Environment | Communication | Command |
|-------------|--------------|---------|
| `flexi-2350_spi` | Direct SPI slave to Pi **(recommended)** | `pio run -e flexi-2350_spi` |
| `flexi-2350_w5500` | Ethernet via W5500 | `pio run -e flexi-2350_w5500` |

## Building HAL Driver

If you are not using the pre-configured Flexi-Pi image, you will need to build and install the HAL compponents. There is an install script in the `hal-driver/` directory which will do this:

```
cd hal-driver
./install.sh
```

This builds and installs both `flexi-ninja.so` (SPI) and `flexi-ninja-eth.so` (Ethernet-only) to `/usr/lib/linuxcnc/modules/`. There is no harm in installing both regardless of which one you are using.

## License

- The PIO quadrature encoder program uses BSD-3 license by Raspberry Pi (Trading) Ltd.
- The `ioLibrary_Driver` is licensed under the MIT License by Wiznet.
- The upstream stepper-ninja repository this is based on is licensed under the MIT License by Zsolt Viola.
