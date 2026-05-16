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

    typedef struct {
        uint8_t gpio;
        const char name[16];
        int8_t pullup;
    } GpioPin;

    typedef struct {
        uint8_t gpio;
        const char name[16];
        int8_t invert;
    } PwmPin;

    typedef struct {
        uint8_t ex_num;
        const char name[16];
    } ExpanderPin;

    typedef struct {
        uint8_t base_pin;
        uint8_t index_pin;
        uint8_t index_active_level;
    } EncoderPin;

    typedef struct {
        uint8_t step_pin;
        uint8_t dir_pin;
        uint8_t invert;
    } StepgenPin;

#ifdef BOARD_FLEXI_2350

    #define stepgens 6
    // Step, dir, step invert
    #define STEPGEN_CONFIG { \
        {GP12, GP13, 0}, \
        {GP14, GP15, 0}, \
        {GP16, GP17, 0}, \
        {GP18, GP19, 0}, \
        {GP20, GP21, 0}, \
        {GP22, GP23, 0}, \
    }
    
    #define encoders 2
    // Base, index, index active level
    #define ENCODER_CONFIG { \
        {GP09, GP11, high}, \
        {GP45, GP47, high}, \
    }

    // GPIO, name, pullup
    #define INPUT_PINS { \
        {GP24, "HALT", 1}, \
        {GP27, "FEED_HOLD", 1}, \
        {GP30, "CYCLE_START", 1}, \
        {GP32, "DOOR", 1}, \
        {GP34, "B_LIM", 1}, \
        {GP35, "A_LIM", 1}, \
        {GP36, "Z_LIM", 1}, \
        {GP37, "Y_LIM", 1}, \
        {GP38, "X_LIM", 1}, \
        {GP39, "PROBE_IRQ", 1}, \
    }

    #define out_pins {PIN_NULL}

    // PWM for spindle 0-10v
    #define use_pwm 1
    #define pwm_count 1
    // GPIO, name, invert
    #define PWM_PINS {{GP26, "SPINDLE_PWM", 0}}
    #define default_pwm_frequency 5000 // default pwm frequency in Hz if not specified in the HAL configuration
    #define default_pwm_maxscale 4096 // default pwm max scale if not specified in the HAL configuration
    #define default_pwm_min_limit 0 // default pwm min limit not specified in the HAL configuration

    // Expander GPIO, name
    #define EX_INPUT_PINS { \
        {EX5,  "X_ALM"}, \
        {EX6,  "Y_ALM"}, \
        {EX7,  "Z_ALM"}, \
        {EX8,  "A_ALM"}, \
        {EX9,  "B_ALM"}, \
        {EX10, "C_ALM"}, \
        {EX3,  "TOOL"}, \
        {EX4,  "PROBE"}, \
    }

    // Expander GPIO, name
    #define EX_OUTPUT_PINS { \
        {EX29, "X_EN"}, \
        {EX28, "Y_EN"}, \
        {EX27, "Z_EN"}, \
        {EX26, "A_EN"}, \
        {EX25, "B_EN"}, \
        {EX24, "C_EN"}, \
        {EX13, "MIST"}, \
        {EX14, "COOL"}, \
        {EX23, "AUXOUT_0"}, \
        {EX22, "AUXOUT_1"}, \
        {EX21, "AUXOUT_2"}, \
        {EX20, "AUXOUT_3"}, \
        {EX19, "AUXOUT_4"}, \
        {EX18, "AUXOUT_5"}, \
        {EX17, "AUXOUT_6"}, \
        {EX16, "AUXOUT_7"}, \
        {EX11, "SPINDLE_ENABLE"}, \
        {EX12, "SPINDLE_DIR"}, \
    }

    // SPI port definition (GPIO)
    #define SPI_PORT        spi1
    #define GPIO_MISO       GP43
    #define GPIO_CS         GP41
    #define GPIO_SCK        GP42
    #define GPIO_MOSI       GP40
    #define GPIO_RESET      GP00
    #define GPIO_INT        GP08
    #define LED_GPIO        PIN_NULL

    #define use_flexgpio 1
    #define FLEXGPIO_SDA    GP06
    #define FLEXGPIO_SCL    GP07
    #define FLEXGPIO_IRQ    GP31
    #define FLEXGPIO_ADDR   0x48
    #define PROBE_SELECT_BIT 31

#endif // BOARD_FLEXI_2350


    #define default_pulse_width 2500 // default pulse width in nanoseconds, for the stepgen if not specified in the HAL configuration
    #define default_step_scale 1000 // default step scale in steps/unit for the stepgen if not specified in the HAL configuration
    
    #define use_timer_interrupt 0 // Use a timer interrupt with a 3-slot step ring buffer to smooth PC transmission jitter for step commands    

    #ifndef encoder_pio_version
    #define encoder_pio_version ENCODER_PIO_SUBSTEP // 0 = old quadrature encoder PIO, 1 = substep encoder PIO
    #endif

#include "footer.h"
#endif
