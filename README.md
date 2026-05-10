# flexi-ninja

A fork of stepper-ninja for Expatria's FlexiHAL-2350.

## Features

- **Supported configurations**:

  - FlexiHAL-2350 <https://expatria.myshopify.com/products/flexihal-2350-cnc-controller-rev-a3-1-for-grblhal-and-linuxcnc>

- **step-generator**: There are 6 stepgens with the FlexiHAL-2350, with a max step rate of 1 MHz per channel. Pulse width is set from a HAL pin (96 ns - 6300 ns with a 125 MHz pico, 60 ns - 4000 ns with a 200 MHz pico). FIXME

- **quadrature-encoder**: max 8 with pico1, max 12 with pico2. High speed, zero-pulse handling, velocity estimation for low-resolution encoders, and spindle-synchronized motion support. FIXME

- **digital I/O**: you can configure the free pins of the pico as inputs and outputs. FIXME

- **PWM**: you can configure up to 16 GPIOs for PWM output (1900 Hz with 16-bit resolution up to 1 MHz with 7-bit resolution), and you can choose active-low or active-high behavior. FIXME




## License

- The quadrature encoder PIO program uses BSD-3 license by Raspberry Pi (Trading) Ltd.
- The `ioLibrary_Driver` is licensed under the MIT License by Wiznet.
- The upstream stepper-ninja reposistory this is based off of is licensed under the MIT License by Zsolt Viola.