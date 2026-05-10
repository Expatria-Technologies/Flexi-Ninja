#ifndef CONFIG_H
#define CONFIG_H
#include "internals.h"

    // **************************************************************************
    // ** This file contains the configuration for the stepper ninja project   **
    // ** if you want to use pins instead of GPIO use PIN_1, PIN_2, PIN_4, ... **
    // **************************************************************************

    // default network settings after you flash the PICO
    #define DEFAULT_MAC {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56}
    #define DEFAULT_IP {192, 168, 0, 177}
    #define DEFAULT_PORT 8888
    #define DEFAULT_GATEWAY {192, 168, 0, 1}
    #define DEFAULT_SUBNET {255, 255, 255, 0}
    // timeout for detecting disconnection from linuxcnc
    #define DEFAULT_TIMEOUT 1000000

#ifdef BOARD_FLEXI_2350

    // All GPIO form 0-15 and 22-31 are usable
    #define stepgens 6
    #define stepgen_steps {GP12, GP14, GP16, GP18, GP20, GP22}
    #define stepgen_dirs {GP13, GP15, GP17, GP19, GP21, GP23}
    #define step_invert {0, 0, 0, 0, 0} // step pin invert for each stepgen (0 = not inverted, 1 = inverted)
    
    #define encoders 2
    #define enc_pins {GP09, GP45} // uses 2 sequential pins, only need to set the first pin. Encoder 1 on GPIO 9, 10,. Encoder 2 on GPIO 45, 46.
    #define enc_index_pins {GP11, GP47}  // Encoder index pins. Encoder 1 on GPIO 11, encoder 2 on GPIO 47
    #define enc_index_active_level {high, high}

    #define in_pins {GP24, GP27, GP30, GP31, GP32, GP34, GP35, GP36, GP37, GP38, GP39} // Input GPIO on RP2350. HALT, HOLD, CYCLE_START, MCU_IRQ (TODO, Motor alarm?), DOOR, B_LIMIT, A_LIMIT, Z_LIMIT, Y_LIMIT, X_LIMIT, PROBE
    #define in_pullup {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}

    #define out_pins {PIN_NULL}

    // if you want to use the module with pwm output, set this to 1
    #define use_pwm 1 // use of pwm output
    #define pwm_count 1
    #define pwm_pin {GP26} // PWM GPIO for the module (GPIO 13, GPIO 14)
    #define pwm_invert {0} // Invert the PWM signal (1 = inverted, 0 = not inverted)
    #define default_pwm_frequency 5000 // default pwm frequency in Hz if not specified in the HAL configuration
    #define default_pwm_maxscale 4096 // default pwm max scale if not specified in the HAL configuration
    #define default_pwm_min_limit 0 // default pwm min limit if not specified in the HAL configuration

#endif // BOARD_FLEXI_2350

    // used gpio for SPI on the RPI: 8, 9, 10, 11
    // used gpio for SPI on the PICO: 40, 41, 42, 43
    // available GPIO left side:  2,3,4,17,27,33,0,5,6,13,19,26
    // available GPIO right side: 14,15,18,23,24,25,1,12,16,20,21
    #define raspi_int_out 22 
    #define raspi_inputs {21}//TODO - Probaby don't need these.
    #define raspi_input_pullups {0}
    #define raspi_outputs {20}
    // if you are using raspberry pi SPI instead of Wizchip you get the GP20, GP21 free on the PICO

    #define default_pulse_width 2500 // default pulse width in nanoseconds, for the stepgen if not specified in the HAL configuration
    #define default_step_scale 1000 // default step scale in steps/unit for the stepgen if not specified in the HAL configuration
    
    #define use_timer_interrupt 0 // Use a timer interrupt with a 3-slot step ring buffer to smooth PC transmission jitter for step commands    

    #ifndef encoder_pio_version
    #define encoder_pio_version ENCODER_PIO_SUBSTEP // 0 = old quadrature encoder PIO, 1 = substep encoder PIO
    #endif

    #define KBMATRIX

#include "footer.h"
#include "kbmatrix.h"
#endif
