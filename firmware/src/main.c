#include "main.h"
#include "quadrature_encoder_substep.h"
#include "flexgpio.h"
#define ENCODER_IDLE_STOP_SAMPLES 2500

// Flexi-Ninja
// Based on stepper-ninja but Viola Zsolt (atrex66@gmail.com)
// License: MIT


// -------------------------------------------
// GLOBAL VARIABLES
//-----------------------------------------

extern wiz_NetInfo default_net_info;
extern uint16_t port;
extern configuration_t *flash_config;

wiz_NetInfo net_info;
wiz_PhyConf *phy_conf;

transmission_pc_pico_t *rx_buffer;
transmission_pico_pc_t *tx_buffer;

// ==================== I/O buffers ====================
volatile uint32_t input_buffer[4];      // Input register mirror for diagnostics / terminal
const GpioPin input_pins[] = INPUT_PINS;
const uint8_t output_pins[] = out_pins;
// =====================================================

uint8_t first_send = 1;
volatile bool first_data = true;

uint dma_tx;
uint dma_rx;
dma_channel_config dma_channel_config_tx;
dma_channel_config dma_channel_config_rx;

#if stepgens > 0
const StepgenPin stepgen_config[] = STEPGEN_CONFIG;
uint32_t total_steps[stepgens] = {0,};

    #if use_timer_interrupt == 1
    #define STEP_RING_BUFFER_SIZE 3

    volatile uint32_t step_command_ring[STEP_RING_BUFFER_SIZE][stepgens];
    volatile uint8_t step_ring_head = 0;
    volatile uint8_t step_ring_tail = 0;
    volatile uint8_t step_ring_count = 0;
    volatile uint32_t step_timer_period_us = 1000;
    volatile uint32_t last_step_packet_us = 0;
    volatile uint64_t next_step_alarm_us = 0;
    volatile bool step_ring_overflow = false;
    volatile bool step_ring_underflow = false;
    #endif
#endif

uint8_t buffer_index = 0;

uint8_t *src_ip;
uint16_t src_port;
uint8_t rx_counter=0;

uint8_t rx_size = sizeof(transmission_pc_pico_t);
uint8_t tx_size = sizeof(transmission_pico_pc_t);
static uint8_t spi_rx_frame[SPI_TRANSFER_SIZE];

uint8_t sety = 0;
uint8_t nop = 0;
uint8_t old_sety = 0;
uint8_t old_nop = 0;

uint8_t connected = 0;
uint8_t timer_started = 0;
int alarm_num = -1;

uint8_t checksum_error = 0;
uint8_t timeout_error = 1;
uint32_t last_time = 0;
absolute_time_t last_packet_time;
uint32_t TIMEOUT_US = 100000;
uint32_t time_diff;
uint8_t checksum_index = 1;
uint8_t checksum_index_in = 1;

#if use_pwm == 1
uint32_t pwm_freq_buffer[pwm_count];
const PwmPin pwm_pins[pwm_count] = PWM_PINS;
uint32_t old_pwm_frequency[pwm_count];
#endif

PIO_def_t stepgen_pio[stepgens];

#if encoders > 0

    const EncoderPin encoder_config[] = ENCODER_CONFIG;
    const uint8_t encoder_count = sizeof(encoder_config) / sizeof(encoder_config[0]);
    uint32_t encoder[encoders] = {0,};
    volatile uint8_t index_reset_flags = 0;

    static uint8_t enc_index_enabled[encoders] = {0,};
    PIO_def_t encoder_pio[encoders];

    #if encoder_pio_version == ENCODER_PIO_SUBSTEP
    substep_state_t substep_state[encoders];
    #endif

    static inline void reset_encoder_counter(uint8_t i) {
#if use_stepcounter == 0
        pio_sm_exec(encoder_pio[i].pio, encoder_pio[i].sm, pio_encode_set(pio_y, 0));
        encoder[i] = 0;
        #if encoder_pio_version == ENCODER_PIO_SUBSTEP
        substep_state[i].raw_step = 0;
        substep_state[i].position = 0;
        substep_state[i].speed = 0;
        substep_state[i].speed_2_20 = 0;
        substep_state[i].stopped = 1;
        substep_state[i].idle_stop_sample_count = 0;
        uint now_us = time_us_32();
        substep_state[i].prev_step_us = now_us;
        substep_state[i].prev_trans_us = now_us;
        substep_state[i].prev_trans_pos = 0;
        substep_state[i].prev_low = 0;
        substep_state[i].prev_high = 0;
        #endif
#else
        pio_sm_exec(pio1, i, pio_encode_set(pio_y, 0));
        encoder[i] = 0;
#endif
    }

#endif

#if stepgens > 0
// -------------------------------------------
// Pulse generation setup
// -------------------------------------------
static inline void __time_critical_func(apply_stepgen_commands)(const uint32_t *step_commands) {
    if (checksum_error){
        return;
    }

    for (int i = 0; i < stepgens; i++) {
        uint32_t command_word = step_commands[i];
        if (command_word != 0){
            gpio_put(stepgen_config[i].dir_pin, (command_word >> 31));
            pio_sm_put_blocking(stepgen_pio[i].pio, stepgen_pio[i].sm, command_word & 0x7fffffff);
        }
    }
}

void __time_critical_func(stepgen_update_handler)() {
    apply_stepgen_commands((const uint32_t *)rx_buffer->stepgen_command);
}

    #if use_timer_interrupt == 1
    static inline void reset_step_ring_buffer(void) {
        uint32_t irq_state = save_and_disable_interrupts();
        step_ring_head = 0;
        step_ring_tail = 0;
        step_ring_count = 0;
        last_step_packet_us = 0;
        step_ring_overflow = false;
        step_ring_underflow = false;
        restore_interrupts(irq_state);
    }

    static void arm_step_timer(void) {
        uint64_t now_us = time_us_64();

        if (alarm_num < 0) {
            alarm_num = hardware_alarm_claim_unused(false);
            if (alarm_num < 0) {
                printf("No free hardware alarm for step ring buffer\n");
                return;
            }
            hardware_alarm_set_callback((uint)alarm_num, timer_callback);
        }

        next_step_alarm_us = now_us + step_timer_period_us;
        hardware_alarm_set_target((uint)alarm_num, from_us_since_boot(next_step_alarm_us));
        timer_started = 1;
    }

    static void enqueue_stepgen_commands(const uint32_t *step_commands) {
        uint32_t now_us = time_us_32();
        uint8_t should_arm_timer = 0;
        uint32_t irq_state = save_and_disable_interrupts();

        if (last_step_packet_us != 0) {
            uint32_t measured_period_us = now_us - last_step_packet_us;
            if (measured_period_us > 50 && measured_period_us < TIMEOUT_US) {
                step_timer_period_us = (step_timer_period_us * 3u + measured_period_us + 2u) / 4u;
            }
        }
        last_step_packet_us = now_us;

        if (step_ring_count < STEP_RING_BUFFER_SIZE) {
            for (int i = 0; i < stepgens; i++) {
                step_command_ring[step_ring_head][i] = step_commands[i];
            }
            step_ring_head = (uint8_t)((step_ring_head + 1u) % STEP_RING_BUFFER_SIZE);
            step_ring_count++;
            step_ring_overflow = false;
            if (!timer_started && step_ring_count == STEP_RING_BUFFER_SIZE) {
                should_arm_timer = 1;
                step_ring_underflow = false;
            }
        } else {
            step_ring_overflow = true;
        }

        restore_interrupts(irq_state);

        if (should_arm_timer) {
            arm_step_timer();
        }
    }
    #endif
#endif

// -------------------------------------------
// Core 1 Entry Point
// -------------------------------------------
void core1_entry() {

    multicore_lockout_victim_init();

    memset(src_ip, 0, 4);
    sleep_ms(500);

    gpio_init(LED_GPIO);
    gpio_set_dir(LED_GPIO, GPIO_OUT);

    // initialize encoder index pins
    #if encoders > 0
        for (int i=0;i<encoder_count;i++){
            if (encoder_config[i].index_pin!=PIN_NULL){
                gpio_init(encoder_config[i].index_pin);
                gpio_set_dir(encoder_config[i].index_pin, false);
                printf("Encoder index %d pin %d initialized\n", i, encoder_config[i].index_pin);
            }
        }
    #endif

    while(1){
        gpio_put(LED_GPIO, !timeout_error);
        time_diff = (uint32_t)absolute_time_diff_us(last_packet_time, get_absolute_time());
        if (time_diff > TIMEOUT_US) {
            if (timeout_error == 0){
                printf("Disconnected from linuxcnc.\n");
                connected = 0;
                stop_timer();
#if encoders >0
                    for (int i = 0; i < encoders; i++) {
                        reset_encoder_counter((uint8_t)i);
    #if use_stepcounter == 0
                        printf("Encoder %d reset\n", i);
    #else
                        printf("Step counter %d reset\n", i);
    #endif
                    }
#endif
                rx_counter = 0;
                timeout_error = 1;
                checksum_index = 1;
                checksum_index_in = 1;
                checksum_error = 0;
                first_data = true;
                first_send = 1;
                #ifdef out_pins
                    if (sizeof(output_pins) / sizeof(output_pins[0]) > 0){
                        for (int i = 0; i < sizeof(output_pins) / sizeof(output_pins[0]); i++) {
                            if (output_pins[i] != PIN_NULL)
                                gpio_put(output_pins[i], 0); // reset outputs
                        }
                    }
                #endif

            }
        // terminal handling only when not connected to the linuxcnc
         handle_serial_input();
        }
        else {
            timeout_error = 0;
            connected = 1;
            // enable disable encoder index interrupts based on the enc_control pins
            #if encoders > 0
                if (encoder_count > 0){
                    for (int i=0;i<encoder_count;i++){
                        if (encoder_config[i].index_pin!=PIN_NULL){
                            if (((rx_buffer->enc_control >> i) & 0x01u) == 1u){
                                if (enc_index_enabled[i] == 0){
                                    if (encoder_config[i].index_active_level == high){
                                        gpio_set_irq_enabled_with_callback(encoder_config[i].index_pin, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
                                    }
                                    else{
                                        gpio_set_irq_enabled_with_callback(encoder_config[i].index_pin, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
                                    }
                                    enc_index_enabled[i] = 1;
                                }
                            }else {
                                if (enc_index_enabled[i] == 1){
                                    if (encoder_config[i].index_active_level == high){
                                        gpio_set_irq_enabled_with_callback(encoder_config[i].index_pin, GPIO_IRQ_EDGE_RISE, false, &gpio_callback);
                                    }
                                    else{
                                        gpio_set_irq_enabled_with_callback(encoder_config[i].index_pin, GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
                                    }
                                    enc_index_enabled[i] = 0;
                                }
                            }
                        }
                    }
                }
            #endif

            #if stepgens > 0
            if (rx_buffer->pio_timing < sizeof(pio_settings) / sizeof(pio_settings[0])) {
                sety = pio_settings[rx_buffer->pio_timing].sety & 31;
                nop = pio_settings[rx_buffer->pio_timing].nop & 31;

                if (old_sety != sety || old_nop != nop) {
                    for (int i=0; i<stepgens; i++){
                        pio_sm_exec(stepgen_pio[i].pio, stepgen_pio[i].sm, pio_encode_jmp(1));
                        stepgen_pio[i].pio->instr_mem[4] = pio_encode_set(pio_y, sety);
                        stepgen_pio[i].pio->instr_mem[5] = pio_encode_nop() | pio_encode_delay(nop);
                    }
                    float cycle_time_ns = 1.0f / pico_clock * 1000000000.0f;
                    printf("New pulse width set: %d\n", pio_settings[rx_buffer->pio_timing].high_cycles * (uint8_t)cycle_time_ns);
                    old_sety = sety;
                    old_nop = nop;
                }
            }
            #endif

            #if use_pwm == 1
            for (int i=0; i<pwm_count; i++){
                if (old_pwm_frequency[i] != pwm_freq_buffer[i]){
                    old_pwm_frequency[i] = pwm_freq_buffer[i];
                    ninja_pwm_set_frequency(pwm_pins[i].gpio, rx_buffer->pwm_frequency[i]);
                }
            }
            #endif
        }
    }
}

int main() {

    stdio_init_all();
    gpio_init(LED_GPIO);
    gpio_set_dir(LED_GPIO, GPIO_OUT);
    sleep_ms(2000);

    src_ip = (uint8_t *)malloc(4);
    rx_buffer = (transmission_pc_pico_t *)malloc(rx_size);
    if (rx_buffer == NULL) {
        printf("rx_buffer allocation failed\n");
        return -1;
    }
    memset(rx_buffer, 0, rx_size);
    tx_buffer = (transmission_pico_pc_t *)malloc(tx_size);
    if (tx_buffer == NULL) {
        printf("tx_buffer allocation failed\n");
        return -1;
    }
    memset(tx_buffer, 0, tx_size);

    printf("\033[2J");
    printf("\033[H");
    
    printf("\n\n--- flexi-ninja ---\n");
    printf("https://github.com/atrex66/flexi-ninja\n");
    printf("E-mail:atrex66@gmail.com\n");
    printf("\n");

//    if (!set_sys_clock_khz(pico_clock / 1000, true))
//    {
//        printf("Clock can not configure to %d");
//        while (1)
//        {
//            sleep_ms(1000);
//        }
//    }
    
    #ifndef RASPBERRY_PI_SPI

        //clock_configure(clk_peri,
        //                0,
        //                CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
        //                clock_get_hz(clk_sys),
        //                clock_get_hz(clk_sys));

        spi_init(spi0, 40000000);

        // force spi clock speed
        #if pico_clock == 125000000
            hw_write_masked(&spi_get_hw(spi0)->cr0, (0) << SPI_SSPCR0_SCR_LSB, SPI_SSPCR0_SCR_BITS); // SCR = 0
            hw_write_masked(&spi_get_hw(spi0)->cpsr, 4, SPI_SSPCPSR_CPSDVSR_BITS); // CPSDVSR = 4
        #endif
    #endif
    
    // load network config from the flash
    load_configuration();

    #ifndef RASPBERRY_PI_SPI
        #pragma message "W5500 init"
        w5100s_init();
        w5500_interrupt_init();
    network_init();
    #else
    printf("Raspberry PI spi communication.\n");
    spi_init(SPI_PORT, 8 * 1000 * 1000); //8mhz
    spi_set_slave(SPI_PORT, true);
    // In slave mode CPSR must be >= 12 and SCR = 0 for correct operation.
    // spi_set_baudrate (called by spi_init) computes these for master mode
    // and may set CPSR < 12, causing unreliable slave behaviour.
    spi_get_hw(SPI_PORT)->cpsr = 12;
    hw_write_masked(&spi_get_hw(SPI_PORT)->cr0, 0, SPI_SSPCR0_SCR_BITS);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(GPIO_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SCK, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_MISO, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_CS, GPIO_FUNC_SPI);
    gpio_set_pulls(GPIO_CS, true, false);

    dma_tx = dma_claim_unused_channel(true);
    dma_rx = dma_claim_unused_channel(true);

    dma_channel_config_tx = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&dma_channel_config_tx, DMA_SIZE_8);
    channel_config_set_dreq(&dma_channel_config_tx, DREQ_SPI1_TX);
    channel_config_set_read_increment(&dma_channel_config_tx, true);
    channel_config_set_write_increment(&dma_channel_config_tx, false);

    dma_channel_config_rx = dma_channel_get_default_config(dma_rx);
    channel_config_set_transfer_data_size(&dma_channel_config_rx, DMA_SIZE_8);
    channel_config_set_dreq(&dma_channel_config_rx, DREQ_SPI1_RX);
    channel_config_set_read_increment(&dma_channel_config_rx, false);
    channel_config_set_write_increment(&dma_channel_config_rx, true);

    // Configure GPIO_INT as input with pull-up
    gpio_init(GPIO_INT);
    gpio_set_dir(GPIO_INT, GPIO_IN);
    gpio_pull_up(GPIO_INT);
    gpio_set_input_hysteresis_enabled(GPIO_INT, false);

    #endif
    
    multicore_launch_core1(core1_entry);
    flexgpio_init();

    PIO pio = pio0;
    int p = 0;
    #ifdef out_pins
    printf("Output GPIO: ");
    for (int i = 0; i < sizeof(output_pins) / sizeof(output_pins[0]); i++) {
        if (output_pins[i] != PIN_NULL) {
            gpio_init(output_pins[i]);
            gpio_set_dir(output_pins[i], GPIO_OUT);
            gpio_put(output_pins[i], 0);
            printf("%d ", output_pins[i]);
        }
    }
    printf("\n");
    #endif

    #ifdef INPUT_PINS
    printf("Input Pins: ");
    for (size_t i = 0; i < sizeof(input_pins) / sizeof(input_pins[0]); i++) {
        gpio_init(input_pins[i].gpio);
        gpio_set_dir(input_pins[i].gpio, GPIO_IN);
        printf("%s ", input_pins[i].name);
        if (input_pins[i].pullup == -1){
            gpio_set_pulls(input_pins[i].gpio, false, true);
        } else if (input_pins[i].pullup == 1){
            gpio_set_pulls(input_pins[i].gpio, true, false);
        }
    }
    printf("\n");
    #endif

    #if use_pwm == 1
        printf("Pwm GPIO:");
        for (int i=0;i<pwm_count;i++){
            ninja_pwm_init(pwm_pins[i].gpio);
            if (pwm_pins[i].invert){
                gpio_set_outover(pwm_pins[i].gpio, GPIO_OVERRIDE_INVERT); // Invert the PWM signal
                printf("_");
            }
            printf("%s ", pwm_pins[i].name);
        }
        printf("\n");
    #endif

    #if stepgens > 0

    uint32_t offset[2] = {0, };
    offset[0] = pio_add_program_at_offset(pio0, &freq_generator_program, 0);

    uint32_t sm = 0;
    uint8_t o = 0;
    for (uint32_t i = 0; i < stepgens; i++){
        stepgen_pio[i] = get_next_pio(stepgen_len);
        if (stepgen_pio[i].sm == 255){
            printf("Not enough pio state machines, check the config.h \n");
        }
        pio = stepgen_pio[i].pio;
        sm = stepgen_pio[i].sm;
        pio_gpio_init(pio, stepgen_config[i].step_pin);
        gpio_init(stepgen_config[i].dir_pin);
        gpio_set_dir(stepgen_config[i].dir_pin, GPIO_OUT);
        if (stepgen_config[i].invert){
           gpio_set_outover(stepgen_config[i].step_pin, GPIO_OVERRIDE_INVERT);
        }
        gpio_set_dir(stepgen_config[i].step_pin, GPIO_OUT);
        pio_sm_set_consecutive_pindirs(pio, sm, stepgen_config[i].step_pin, 1, true);
        pio_sm_config c = freq_generator_program_get_default_config(offset[o]);
        sm_config_set_set_pins(&c, stepgen_config[i].step_pin, 1);
        pio_sm_init(pio, sm, offset[o], &c);
        pio_sm_set_enabled(pio, sm, true);
        printf("stepgen%d. pio:%d sm:%d init done...\n", i, stepgen_pio[i].pio_blk, sm);
    }
    #endif

    #if encoders > 0
        #if use_stepcounter == 0
            for (int i = 0; i < encoders; i++) {
                uint8_t encoder_program_len;

                printf("Encoder %d GPIO init\n", i);
                gpio_init(encoder_config[i].base_pin);
                gpio_init(encoder_config[i].base_pin+1);
                gpio_set_dir(encoder_config[i].base_pin, GPIO_IN);
                gpio_set_dir(encoder_config[i].base_pin+1, GPIO_IN);
                gpio_pull_up(encoder_config[i].base_pin);
                gpio_pull_up(encoder_config[i].base_pin+1);
                //tx_buffer->encoder_latched[i] = 0;
                tx_buffer->encoder_velocity[i] = 0;
                #if encoder_pio_version == ENCODER_PIO_SUBSTEP
                encoder_program_len = quadrature_encoder_substep_len;
                #else
                encoder_program_len = quadrature_encoder_legacy_len;
                #endif
                encoder_pio[i] = get_next_pio(encoder_program_len);
                if (encoder_pio[i].sm == 255){
                    printf("Not enough pio state machines, check the config.h \n");
                }
                printf("Encoder %d pre init\n", i);
                #if encoder_pio_version == ENCODER_PIO_SUBSTEP
                // Initialize substep encoder
                substep_init_state(encoder_pio[i].pio, encoder_pio[i].sm, encoder_config[i].base_pin, &substep_state[i]);
                // Default (3 samples) is too aggressive for low RPM and can
                // force speed to zero between sparse transitions.
                substep_state[i].idle_stop_samples = ENCODER_IDLE_STOP_SAMPLES;
                printf("Encoder %d set calibration data ....\n", i);
                // Set default calibration data - can be overridden later
                substep_set_calibration_data(&substep_state[i], 64, 128, 192);
                printf("encoder%d. pio:%d sm:%d init done (substep)...\n", i, encoder_pio[i].pio_blk, encoder_pio[i].sm);
                #else
                if (pio_can_add_program_at_offset(encoder_pio[i].pio, &quadrature_encoder_program, 0)) {
                    pio_add_program_at_offset(encoder_pio[i].pio, &quadrature_encoder_program, 0);
                }
                quadrature_encoder_program_init(encoder_pio[i].pio, encoder_pio[i].sm, encoder_config[i].base_pin, 0);
                encoder[i] = quadrature_encoder_get_count(encoder_pio[i].pio, encoder_pio[i].sm);
                printf("encoder%d. pio:%d sm:%d init done (legacy)...\n", i, encoder_pio[i].pio_blk, encoder_pio[i].sm);
                #endif
            }
        #else
            for (int i = 0; i < encoders; i++) {
                gpio_init(encoder_config[i].base_pin);
                gpio_init(encoder_config[i].base_pin+1);
                gpio_set_dir(encoder_config[i].base_pin, GPIO_IN);
                gpio_set_dir(encoder_config[i].base_pin+1, GPIO_IN);
                gpio_pull_up(encoder_config[i].base_pin);
                gpio_pull_up(encoder_config[i].base_pin+1);
            }
            pio_add_program(pio1, &step_counter_program);
            for (int i = 0; i < encoders; i++) {
                step_counter_program_init(pio1, i, encoder_config[i].base_pin, 0);
                printf("step counter %d init done...\n", i);
            }
        #endif
    #endif //encoders>0

    // init the firsat tx_buffer checksum because of the full duplex communication with the pico.
    tx_buffer->checksum = calculate_checksum(tx_buffer, tx_size - 1);

    printf("Pio init done....\n");

    handle_udp();
}

void reset_with_watchdog() {
    watchdog_enable(1, 1);
    while(1);
}

void __time_critical_func(cs_select)() {
    gpio_put(GPIO_CS, 0);
    asm volatile("nop \n nop \n nop");
}

void __time_critical_func(cs_deselect)() {
    gpio_put(GPIO_CS, 1);
}

uint8_t __time_critical_func(spi_read)() {
    uint8_t data;
    spi_read_blocking(SPI_PORT, 0x00, &data, 1);
    return data;
}

void __time_critical_func(spi_write)(uint8_t data) {
    spi_write_blocking(SPI_PORT, &data, 1);
}

int32_t __time_critical_func(_sendto)(uint8_t sn, uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t port) {
    uint16_t freesize;
    uint32_t taddr;

    if (first_send) {
        setSn_DIPR(sn, addr);
        setSn_DPORT(sn, port);
        first_send = 0;
    }

    wiz_send_data(sn, buf, len);
    setSn_CR(sn, Sn_CR_SEND);
    while (getSn_CR(sn));

    while (1) {
        uint8_t tmp = getSn_IR(sn);
        if (tmp & Sn_IR_SENDOK) {
            setSn_IR(sn, Sn_IR_SENDOK);
            break;
        } else if (tmp & Sn_IR_TIMEOUT) {
            setSn_IR(sn, Sn_IR_TIMEOUT);
            return SOCKERR_TIMEOUT;
        }
    }
    return (int32_t)len;
}

void __not_in_flash_func(core0_wait)(void) {
    // Drop stale completion tokens from a previous interrupted save.
    while (multicore_fifo_rvalid()) {
        (void)multicore_fifo_pop_blocking();
    }

    absolute_time_t wait_deadline = make_timeout_time_ms(1000);
    while (!multicore_fifo_wready()) {
        if (time_reached(wait_deadline)) {
            printf("Core0 unable to acknowledge flash request.\n");
            return;
        }
        tight_loop_contents();
    }

    printf("Core0 is ready to write...\n");
    // Keep core0 fully out of flash while core1 erases/programs XIP.
    uint32_t irq_state = save_and_disable_interrupts();
    multicore_fifo_push_blocking(0xFEEDFACE);
    wait_deadline = make_timeout_time_ms(2000);
    while (!multicore_fifo_rvalid()) {
        if (time_reached(wait_deadline)) {
            printf("Core0 flash handshake timeout, resuming.\n");
            restore_interrupts(irq_state);
            return;
        }
        tight_loop_contents();
    }
    uint32_t signal = multicore_fifo_pop_blocking();
    restore_interrupts(irq_state);
    if (signal == 0xDEADBEEF) {
        watchdog_reboot(0, 0, 1000);
    }
}

int32_t __time_critical_func(_recvfrom)(uint8_t sn, uint8_t *buf, uint16_t len, uint8_t *addr, uint16_t *port) {
    static uint8_t head[8];
    static uint16_t pack_len = 0;

    while ((pack_len = getSn_RX_RSR(sn)) == 0) {
        if (getSn_SR(sn) == SOCK_CLOSED) return SOCKERR_SOCKCLOSED;
    }

    wiz_recv_data(sn, head, 8);
    setSn_CR(sn, Sn_CR_RECV);
    while (getSn_CR(sn));

    memset(addr, 0, 4);

    addr[0] = head[0];
    addr[1] = head[1];
    addr[2] = head[2];
    addr[3] = head[3];
    *port = (head[4] << 8) | head[5];

    uint16_t data_len = (head[6] << 8) | head[7];

    if (len < data_len) pack_len = len;
    else pack_len = data_len;

    wiz_recv_data(sn, buf, pack_len);
    setSn_CR(sn, Sn_CR_RECV);
    while (getSn_CR(sn));

    return (int32_t)pack_len;
}

void handle_data(){
    tx_buffer->jitter = get_absolute_time() - last_packet_time;
    last_packet_time = get_absolute_time();

    if (rx_buffer->packet_id != rx_counter ) {
        printf("packet loss: %d != %d  syncronising.... \n", rx_buffer->packet_id, rx_counter);
        // try to syncronise packet counter
        rx_counter = rx_buffer->packet_id;
    }
    if (!rx_checksum_ok(rx_buffer)) {
        printf("Checksum error: %d != %d\n", rx_buffer->checksum, checksum_index_in);
        checksum_error = 1;
    }

    if (!checksum_error) {

        #if stepgens > 0
        // Buffer only the step commands when timer-interrupt mode is enabled.
            #if use_timer_interrupt == 1
            enqueue_stepgen_commands((const uint32_t *)rx_buffer->stepgen_command);
            #else
            stepgen_update_handler();
            #endif
        #endif

#if use_pwm == 1
        #if pwm_count < 1
            #pragma error "Defined to use pwm but not defined any PWM pins"
        #endif
        // update pwm
        for (int i=0;i<pwm_count;i++){
            pwm_freq_buffer[i] = rx_buffer->pwm_frequency[i];
            ninja_pwm_set_duty(pwm_pins[i].gpio, (uint16_t)rx_buffer->pwm_duty[i]);
        }
    }
#else
    // no pwm
    }
#endif

    #if encoders > 0
        #if use_stepcounter == 0
        // update encoders
            for (int i = 0; i < encoders; i++) {
                #if encoder_pio_version == ENCODER_PIO_SUBSTEP
                tx_buffer->encoder_timestamp[i] = time_us_32();
                substep_update(&substep_state[i]);
                tx_buffer->encoder_counter[i] = substep_state[i].raw_step;
                tx_buffer->encoder_velocity[i] = 0;
                #else
                int32_t encoder_count = quadrature_encoder_get_count(encoder_pio[i].pio, encoder_pio[i].sm);
                tx_buffer->encoder_timestamp[i] = time_us_32();
                tx_buffer->encoder_counter[i] = encoder_count;
                tx_buffer->encoder_velocity[i] = encoder_count - (int32_t)encoder[i];
                encoder[i] = (uint32_t)encoder_count;
                #endif
                //tx_buffer->encoder_latched[i] = encoder_latched[i];
            }
        #else
            // update step counters
            for (int i = 0; i < encoders; i++) {
                encoder[i] = step_counter_get_count(pio1, i);
                tx_buffer->encoder_counter[i] = encoder[i];
                tx_buffer->encoder_velocity[i] = 0;
                tx_buffer->encoder_timestamp[i] = time_us_32();
                //tx_buffer->encoder_latched[i] = encoder_latched[i];
            }
        #endif

        uint32_t irq_state = save_and_disable_interrupts();
        tx_buffer->interrupt_data = index_reset_flags;
        index_reset_flags = 0;
        restore_interrupts(irq_state);
    #endif

    // --- Standard GPIO path ---
    flexgpio_sync_inputs(tx_buffer->inputs);
    tx_buffer->inputs[0] = gpio_get_all64() & 0xFFFFFFFF; // Read all GPIO inputs
    tx_buffer->inputs[1] = gpio_get_all64() >> 32;
    input_buffer[0] = tx_buffer->inputs[0];
    input_buffer[1] = tx_buffer->inputs[1];
    input_buffer[2] = tx_buffer->inputs[2];
    input_buffer[3] = tx_buffer->inputs[3];
    if (sizeof(output_pins) / sizeof(output_pins[0])>0){
        //set output pins
        for (uint8_t i = 0; i < sizeof(output_pins) / sizeof(output_pins[0]); i++) {
            if (output_pins[i] == PIN_NULL) continue;
            if (output_pins[i] < 32){
                gpio_put(output_pins[i], (rx_buffer->outputs[0] >> i) & 1);
            } else {
                gpio_put(output_pins[i], (rx_buffer->outputs[1] >> (i & 31)) & 1);
            }
        }
    }
    flexgpio_push_outputs(rx_buffer->outputs);

    tx_buffer->step_ring_fill = 0;
    tx_buffer->step_ring_status = 0;
    #if use_timer_interrupt == 1 && stepgens > 0
        tx_buffer->step_ring_fill = step_ring_count;
        if (timer_started) {
            tx_buffer->step_ring_status |= STEP_RING_STATUS_ACTIVE;
        }
        if (step_ring_underflow) {
            tx_buffer->step_ring_status |= STEP_RING_STATUS_UNDERFLOW;
        }
        if (step_ring_overflow) {
            tx_buffer->step_ring_status |= STEP_RING_STATUS_OVERFLOW;
        }
    #endif
    
    tx_buffer->packet_id = rx_counter;
    tx_buffer->checksum = calculate_checksum(tx_buffer, tx_size - 1);
}

void printbuf(uint8_t *buf, size_t len) {
    size_t i;
    for (i = 0; i < len; ++i) {
        printf("%02x", buf[i]);
    }
    putchar('\n');
}

#if encoders > 0
void __not_in_flash_func(gpio_callback)(uint gpio, uint32_t events) {
    for (int i=0;i<encoder_count;i++){
        if (gpio == encoder_config[i].index_pin) {
            if (events & (GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL)) {
                // Reset the selected encoder exactly on the index edge.
                reset_encoder_counter((uint8_t)i);
                index_reset_flags |= (uint8_t)(1u << i);

                if (enc_index_enabled[i] == 1) {
                    if (encoder_config[i].index_active_level == high){
                        gpio_set_irq_enabled_with_callback(encoder_config[i].index_pin, GPIO_IRQ_EDGE_RISE, false, &gpio_callback);
                    } else {
                        gpio_set_irq_enabled_with_callback(encoder_config[i].index_pin, GPIO_IRQ_EDGE_FALL, false, &gpio_callback);
                    }
                    enc_index_enabled[i] = 0;
                }
            }
        }
    }
}
#endif

#if use_timer_interrupt == 1 && stepgens > 0
void __time_critical_func(timer_callback)(uint triggered_alarm_num) {
    uint32_t step_commands[stepgens] = {0,};

    if ((int)triggered_alarm_num != alarm_num) {
        return;
    }

    if (step_ring_count == 0) {
        hardware_alarm_cancel(triggered_alarm_num);
        timer_started = 0;
        step_ring_underflow = true;
        return;
    }

    for (int i = 0; i < stepgens; i++) {
        step_commands[i] = step_command_ring[step_ring_tail][i];
    }
    step_ring_tail = (uint8_t)((step_ring_tail + 1u) % STEP_RING_BUFFER_SIZE);
    step_ring_count--;

    next_step_alarm_us = time_us_64() + step_timer_period_us;
    hardware_alarm_set_target(triggered_alarm_num, from_us_since_boot(next_step_alarm_us));

    apply_stepgen_commands(step_commands);
}
#endif

// Timer stop
void stop_timer() {
    #if use_timer_interrupt == 1 && stepgens > 0
        if (alarm_num >= 0) {
            hardware_alarm_cancel((uint)alarm_num);
            hardware_alarm_set_callback((uint)alarm_num, NULL);
            hardware_alarm_unclaim((uint)alarm_num);
            alarm_num = -1;
        }
        timer_started = 0;
        reset_step_ring_buffer();
        printf("Timer stopped\n");
    #endif
}

// -------------------------------------------
// UDP handler
// -------------------------------------------
static void __not_in_flash_func(spi_read_fulldup)(uint8_t *pBuf, uint8_t *sBuf, uint16_t len)
{
    // Pre-fill TX FIFO completely BEFORE any read
    uint16_t tx_done = 0, rx_done = 0;
    while (tx_done < len && spi_is_writable(SPI_PORT)) {
        spi_get_hw(SPI_PORT)->dr = sBuf[tx_done++];
    }

    while (rx_done < len) {
        if (spi_is_readable(SPI_PORT)) {
            pBuf[rx_done++] = (uint8_t)spi_get_hw(SPI_PORT)->dr;
        }
        if (tx_done < len && spi_is_writable(SPI_PORT)) {
            spi_get_hw(SPI_PORT)->dr = sBuf[tx_done++];
        }
    }

    while (spi_is_readable(SPI_PORT)) {
        (void)spi_get_hw(SPI_PORT)->dr;
    }
}

void __not_in_flash_func(handle_udp)() {
    gpio_pull_up(GPIO_INT);
    uint8_t *packet_buffer;
    packet_buffer = malloc(SPI_TRANSFER_SIZE);
    if (packet_buffer == NULL) {
        printf("SPI packet buffer allocation failed\n");
        while (1) {
            sleep_ms(1000);
        }
    }
    memset(packet_buffer, 0, SPI_TRANSFER_SIZE);
    last_packet_time = get_absolute_time();
    while (1){
        if (consume_save_config_request()) {
            save_config_to_flash();
        }
        if (!gpio_get(GPIO_INT)){
            #ifndef RASPBERRY_PI_SPI
                setSn_IR(0, Sn_IR_RECV);
                int len = _recvfrom(0, (uint8_t *)rx_buffer, rx_size, src_ip, &src_port);
            #else 
                memset(packet_buffer, 0, SPI_TRANSFER_SIZE);
                memcpy(packet_buffer, (uint8_t *)tx_buffer, tx_size);
                spi_read_fulldup(spi_rx_frame, packet_buffer, SPI_TRANSFER_SIZE);
                memcpy((uint8_t *)rx_buffer, spi_rx_frame, rx_size);
                int len = rx_size; // for compatibility
            #endif
            if (len == rx_size) {
                handle_data();
                #ifndef RASPBERRY_PI_SPI
                    if (!checksum_error){
                        _sendto(0, (uint8_t *)tx_buffer, tx_size, src_ip, src_port);
                    }
                #endif
                rx_counter += 1;
            }
        }
        if (multicore_fifo_rvalid()) {
            uint32_t signal = multicore_fifo_pop_blocking();
            printf("Core1 signal: %08X\n", signal);
            if (signal == 0xCAFEBABE) {
                core0_wait();
            }
        }
    }
}

#if _WIZCHIP_ == W5500
void w5500_interrupt_init() {
    gpio_init(GPIO_INT);
    gpio_set_dir(GPIO_INT, GPIO_IN);
    gpio_pull_up(GPIO_INT);
    gpio_set_input_hysteresis_enabled(GPIO_INT, false); 
    setSIMR(0x01);
    setSn_IMR(SOCKET_DHCP, Sn_IMR_RECV);
}
#endif
// -------------------------------------------
// Wiznet chip Init
// -------------------------------------------
void w5100s_init() {

    gpio_init(GPIO_CS);
    gpio_set_dir(GPIO_CS, GPIO_OUT);
    cs_deselect();

    gpio_init(GPIO_RESET);
    gpio_set_dir(GPIO_RESET, GPIO_OUT);
    gpio_put(GPIO_RESET, 1);

    gpio_set_function(GPIO_SCK, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_MISO, GPIO_FUNC_SPI);
   
    reg_wizchip_cs_cbfunc(cs_select, cs_deselect);
    reg_wizchip_spi_cbfunc(spi_read, spi_write);
    
    reg_wizchip_spiburst_cbfunc(spi_read_burst, spi_write_burst);

    gpio_put(GPIO_RESET, 0);
    sleep_ms(100);
    gpio_put(GPIO_RESET, 1);
    sleep_ms(500);

    dma_tx = dma_claim_unused_channel(true);
    dma_rx = dma_claim_unused_channel(true);

    dma_channel_config_tx = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&dma_channel_config_tx, DMA_SIZE_8);
    channel_config_set_dreq(&dma_channel_config_tx, DREQ_SPI0_TX);

    dma_channel_config_rx = dma_channel_get_default_config(dma_rx);
    channel_config_set_transfer_data_size(&dma_channel_config_rx, DMA_SIZE_8);
    channel_config_set_dreq(&dma_channel_config_rx, DREQ_SPI0_RX);
    channel_config_set_read_increment(&dma_channel_config_rx, false);
    channel_config_set_write_increment(&dma_channel_config_rx, true);

    uint8_t tx[_WIZCHIP_SOCK_NUM_] = {0,};
    uint8_t rx[_WIZCHIP_SOCK_NUM_] = {0,};
    memset(tx, 2, _WIZCHIP_SOCK_NUM_);
    memset(rx, 2, _WIZCHIP_SOCK_NUM_);
    wizchip_init(tx, rx);

    wiz_PhyConf phyconf;
    phy_conf = malloc(sizeof(wiz_PhyConf));
    wizphy_getphyconf(phy_conf);
    phy_conf->mode = PHY_MODE_MANUAL;
    phy_conf->speed = PHY_SPEED_100;
    phy_conf->duplex = PHY_DUPLEX_FULL;
    wizphy_setphyconf(phy_conf);
    wizchip_setnetinfo(&net_info);
    printf("Network Init Done\n");
    wizchip_getnetinfo(&net_info);
    wizphy_getphyconf(&phyconf);
    printf("**************Network Info read from WIZCHIP\n");
    printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", net_info.mac[0], net_info.mac[1], net_info.mac[2], net_info.mac[3], net_info.mac[4], net_info.mac[5]);
    printf("IP: %d.%d.%d.%d\n", net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
    printf("Subnet: %d.%d.%d.%d\n", net_info.sn[0], net_info.sn[1], net_info.sn[2], net_info.sn[3]);
    printf("Gateway: %d.%d.%d.%d\n", net_info.gw[0], net_info.gw[1], net_info.gw[2], net_info.gw[3]);
    printf("DNS: %d.%d.%d.%d\n", net_info.dns[0], net_info.dns[1], net_info.dns[2], net_info.dns[3]);
    printf("DHCP: %d   (1-Static, 2-Dinamic)\n", net_info.dhcp);
    printf("PORT: %d\n", port);
    printf("*******************PHY status**************\n");
    printf("PHY Mode: %s\n", phyconf.mode == PHY_MODE_AUTONEGO ? "Auto" : "Manual");
    printf("PHY Duplex: %s\n", phyconf.duplex == PHY_DUPLEX_FULL ? "Full" : "Half");
    printf("PHY Speed: %s\n", phyconf.speed == PHY_SPEED_100 ? "100Mbps" : "10Mbps");
    printf("*******************************************\n");

}

// -------------------------------------------
// Network Init
// -------------------------------------------
void network_init() {
    setSn_CR(0, Sn_CR_CLOSE);
    setSn_CR(0, Sn_CR_OPEN);
    uint8_t sock_num = 0;
    socket(sock_num, Sn_MR_UDP, port, 0);
    }

static void spi_read_burst(uint8_t *pBuf, uint16_t len)
{
    uint8_t dummy_data = 0xFF;

    channel_config_set_read_increment(&dma_channel_config_tx, false);
    channel_config_set_write_increment(&dma_channel_config_tx, false);
    channel_config_set_transfer_data_size(&dma_channel_config_tx, DMA_SIZE_8);
    dma_channel_configure(dma_tx, &dma_channel_config_tx,
                          &spi_get_hw(SPI_PORT)->dr,
                          &dummy_data,              
                          len,                      
                          false);                   

    channel_config_set_read_increment(&dma_channel_config_rx, false);
    channel_config_set_write_increment(&dma_channel_config_rx, true);
    channel_config_set_transfer_data_size(&dma_channel_config_tx, DMA_SIZE_8);
    dma_channel_configure(dma_rx, &dma_channel_config_rx,
                          pBuf,                     
                          &spi_get_hw(SPI_PORT)->dr,
                          len,                      
                          false);                   

    dma_start_channel_mask((1u << dma_tx) | (1u << dma_rx));
    dma_channel_wait_for_finish_blocking(dma_rx);
}

static void spi_write_burst(uint8_t *pBuf, uint16_t len)
{
    uint8_t dummy_data;

    channel_config_set_read_increment(&dma_channel_config_tx, true);
    channel_config_set_write_increment(&dma_channel_config_tx, false);
    dma_channel_configure(dma_tx, &dma_channel_config_tx,
                          &spi_get_hw(SPI_PORT)->dr,
                          pBuf,                     
                          len,                      
                          false);                   

    channel_config_set_read_increment(&dma_channel_config_rx, false);
    channel_config_set_write_increment(&dma_channel_config_rx, false);
    dma_channel_configure(dma_rx, &dma_channel_config_rx,
                          &dummy_data,              
                          &spi_get_hw(SPI_PORT)->dr,
                          len,                      
                          false);                   

    dma_start_channel_mask((1u << dma_tx) | (1u << dma_rx));
    dma_channel_wait_for_finish_blocking(dma_rx);
}
