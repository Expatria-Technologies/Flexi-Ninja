#include "rtapi.h"              /* RTAPI realtime OS API */
#include "rtapi_app.h"          /* RTAPI realtime module decls */
#include "rtapi_errno.h"        /* EINVAL etc */
#include "hal.h"                /* HAL public API decls */

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include "transmission.h"
#include "transmission.c"
#include "pio_settings.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* name of the module */
#ifndef MODULE_NAME
    #define MODULE_NAME "flexi-ninja-eth"
#endif

#define module_name MODULE_NAME

#pragma message "Ethernet version"
char *ip_address;
RTAPI_MP_STRING(ip_address, "Ip address");

#define debug 1
#define PROBE_SELECT_BIT 31

#include "hal_pin_macros.h"

/* module information */
MODULE_DESCRIPTION(module_name " driver");
MODULE_LICENSE("MIT");
static uint16_t tx_size;

static uint16_t rx_size;

/* maximum number of channels */
#define MAX_CHAN 4

uint32_t total_cycles;

#define ANALOG_MAX 4095

/* do not modify */
#if use_timer_interrupt == 0 && stepgens > 0
#define dormant_cycles 6
#else
#define dormant_cycles 0
#endif

/*
 * Add a fixed offset to command positions to avoid simulator zero-crossing
 * issues. Real machines home at axis limits, so this is only relevant in the
 * simulator path.
 */
#define offset 10000

const GpioPin input_pins[] = INPUT_PINS;
const uint8_t output_pins[] = out_pins;
const uint8_t in_pins_no = sizeof(input_pins) / sizeof(input_pins[0]);
const uint8_t out_pins_no = sizeof(output_pins);
const ExpanderPin ex_input_pins[] = EX_INPUT_PINS;
const ExpanderPin ex_output_pins[] = EX_OUTPUT_PINS;
const uint8_t ex_in_count = sizeof(ex_input_pins) / sizeof(ex_input_pins[0]);
const uint8_t ex_out_count = sizeof(ex_output_pins) / sizeof(ex_output_pins[0]);
const EncoderPin encoder_config[] = ENCODER_CONFIG;

typedef struct {
    char ip[16];
    int port;
} IpPort;

typedef struct {
    float y;
    float alpha;
} LowPassFilter;

typedef struct {
    #if stepgens > 0
    hal_float_t *command[stepgens];
    hal_float_t *feedback[stepgens];
    hal_float_t *scale[stepgens];
    hal_bit_t *mode[stepgens];
    hal_bit_t *enable[stepgens];
    hal_u32_t *pulse_width;
    hal_u32_t *dir_setup_ns[stepgens];
    #endif
    #if encoders > 0
    hal_s32_t *raw_count[encoders];
    hal_float_t *enc_scale[encoders];
    hal_float_t *enc_position[encoders];
    hal_float_t *enc_velocity[encoders];
    hal_bit_t *enc_index[encoders];
    hal_bit_t *enc_reset[encoders];
    hal_float_t *enc_rpm[encoders];
    hal_float_t *enc_filter_tau[encoders];
    hal_float_t *enc_filter_dt[encoders];
    hal_float_t *enc_vel_threshold[encoders];
    hal_bit_t *enc_enabled[encoders];
    #endif
    #if use_pwm == 1
    hal_bit_t *pwm_enable[pwm_count];
    hal_u32_t *pwm_output[pwm_count];
    hal_u32_t *pwm_frequency[pwm_count];
    hal_u32_t *pwm_maxscale[pwm_count];
    hal_u32_t *pwm_min_limit[pwm_count];
    #endif 
    
    #if ANALOG_CH > 0
    hal_float_t *analog_value[ANALOG_CH];
    hal_float_t *analog_min[ANALOG_CH];
    hal_float_t *analog_max[ANALOG_CH];
    hal_bit_t *analog_enable[ANALOG_CH];
    hal_s32_t *analog_offset[ANALOG_CH];
    #endif

    hal_s32_t *jitter;
    hal_u32_t *step_ring_fill;
    hal_bit_t *step_ring_active;
    hal_bit_t *step_ring_underflow;
    hal_bit_t *step_ring_overflow;
    hal_bit_t *input[96];
    hal_bit_t *input_not[96];
    hal_bit_t *output[64];
    hal_bit_t *ex_input[32];
    hal_bit_t *ex_input_not[32];
    hal_bit_t *ex_output[32];

#if toolchanger_encoder == 1
    hal_bit_t *toolchanger_bit0;
    hal_bit_t *toolchanger_bit1;
    hal_bit_t *toolchanger_bit2;
    hal_bit_t *toolchanger_bit3;
    hal_bit_t *toolchanger_strobe;
    hal_bit_t *toolchanger_parity;
    hal_bit_t *toolchanger_even_or_odd_parity;
    hal_u32_t *toolchanger_position;
    hal_bit_t *toolchanger_error;
#endif

#if debug == 1
    hal_float_t *debug_freq;
    hal_s32_t *debug_steps[stepgens];
    hal_bit_t *debug_steps_reset;
#endif
    hal_u32_t *period;
    hal_bit_t *probe_select;
    hal_bit_t *connected;
    hal_bit_t *io_ready_in;
    hal_bit_t *io_ready_out;
    IpPort ip_address;
    int sockfd;
    struct sockaddr_in local_addr, remote_addr;
    long long last_received_time;
    long long watchdog_timeout;
    int watchdog_expired;
    long long current_time;
    int index;
    uint8_t checksum_index;
    uint8_t checksum_index_in;
    uint8_t checksum_error;
    float enc_prev_pos[encoders];
    bool index_triggered[encoders];
    uint32_t enc_timestamp[encoders];
    uint32_t delta_time[encoders];
    int64_t prev_pos[stepgens];
    int64_t curr_pos[stepgens];
    bool watchdog_running;
    bool error_triggered;
    bool first_data[stepgens];
    float delta_pos[encoders];
    int32_t delta_count[encoders];
    int32_t delta_count_accum[encoders];
    uint8_t tx_counter;
} module_data_t;

static int instances = 1;
static int comp_id = -1;
static module_data_t *hal_data;

#if stepgens > 0
static uint32_t timing[1024] = {0, };
static uint32_t old_pulse_width = 0;

#endif

static uint32_t counter = 0;

float cycle_time_ns = 1.0f / pico_clock * 1000000000.0f;
transmission_pc_pico_t *tx_buffer;
transmission_pico_pc_t *rx_buffer;


#if encoders > 0
LowPassFilter filter[encoders];
#endif

uint16_t pwm_calculate_wrap(uint32_t freq)
{
    uint32_t sys_clock = pico_clock;

    uint32_t wrap = sys_clock / freq;
    if (wrap > 65535) wrap = 65535;

    return (uint16_t)wrap;
}

void lpf_init(LowPassFilter *f, float tau, float dt)
{
    f->alpha = dt / (tau + dt);
    f->y = 0.0f;
}

float lpf_update(LowPassFilter *f, float x)
{
    f->y += f->alpha * (x - f->y);

    return f->y;
}

static void update_encoder_velocity_from_deltas(module_data_t *d, uint8_t encoder_index)
{
    if (!*d->enc_enabled[encoder_index]) {
        *d->enc_velocity[encoder_index] = 0;
        *d->enc_rpm[encoder_index] = 0;
        return;
    }

    float tau = fmaxf(*d->enc_filter_tau[encoder_index], 1e-6f);
    float dt = fmaxf(*d->enc_filter_dt[encoder_index], 1e-6f);
    filter[encoder_index].alpha = dt / (tau + dt);

    if (d->delta_time[encoder_index] == 0) {
        d->delta_pos[encoder_index] = 0.0f;
    } else if (d->delta_time[encoder_index] > 2500000) {
        *d->enc_velocity[encoder_index] = 0;
        d->delta_pos[encoder_index] = 0.0f;
    } else {
        d->delta_pos[encoder_index] = (float)d->delta_count_accum[encoder_index] / *d->enc_scale[encoder_index];
        *d->enc_velocity[encoder_index] = lpf_update(
            &filter[encoder_index],
            d->delta_pos[encoder_index] * (1000000.0f / (float)d->delta_time[encoder_index])
        );
    }

    // Velocity is always computed from delta (encoder_velocity=0 from firmware).
    // Snap to exact zero when the filtered value drops below threshold.
    if (fabsf(*d->enc_velocity[encoder_index]) < *d->enc_vel_threshold[encoder_index]) {
        *d->enc_velocity[encoder_index] = 0.0f;
    }

    *d->enc_rpm[encoder_index] = (*d->enc_velocity[encoder_index]) * 60.0f;
}

static int module_init(void)
{
    rtapi_print_msg(RTAPI_MSG_INFO, module_name ": module_init\n");
    tx_size = sizeof(transmission_pc_pico_t);
    rx_size = sizeof(transmission_pico_pc_t);
    rtapi_print_msg(RTAPI_MSG_INFO, module_name ": tx_size: %d\n", tx_size);
    rtapi_print_msg(RTAPI_MSG_INFO, module_name ": rx_size: %d\n", rx_size);

    rx_buffer = (transmission_pico_pc_t *)malloc(rx_size);
    if (rx_buffer == NULL) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ": rx_buffer allocation failed\n");
        return -1;
    }
    memset(rx_buffer, 0, rx_size);
    tx_buffer = (transmission_pc_pico_t *)malloc(tx_size);
    if (tx_buffer == NULL) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ": tx_buffer allocation failed\n");
        free(rx_buffer);
        return -1;
    }
    memset(tx_buffer, 0, tx_size);
    #if encoders > 0
        for (int i = 0; i < encoders; i++) {
            filter[i].y = 0;
        }
    #endif
    return 0;
}

static void init_socket(module_data_t *arg)
{
    module_data_t *d = arg;
    uint32_t bufsize = 65535;

    if ((d->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: socket creation failed: %s\n",
            d->index, strerror(errno));
        return;
    }

    d->local_addr.sin_family = AF_INET;
    d->local_addr.sin_port = htons(d->ip_address.port);
    d->local_addr.sin_addr.s_addr = INADDR_ANY;

    rtapi_print_msg(RTAPI_MSG_INFO, module_name ".%d: binding to %s:%d\n",
        d->index, d->ip_address.ip, d->ip_address.port);

    if (bind(d->sockfd, (struct sockaddr *)&d->local_addr, sizeof(d->local_addr)) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: bind failed: %s\n",
            d->index, strerror(errno));
        if (close(d->sockfd) < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: close failed after bind error: %s\n",
                d->index, strerror(errno));
        }
        d->sockfd = -1;
        return;
    }

    int flags = fcntl(d->sockfd, F_GETFL, 0);
    if (fcntl(d->sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: fcntl(F_SETFL O_NONBLOCK) failed: %s\n",
            d->index, strerror(errno));
    }

    d->remote_addr.sin_family = AF_INET;
    d->remote_addr.sin_port = htons(d->ip_address.port);
    if (inet_pton(AF_INET, d->ip_address.ip, &d->remote_addr.sin_addr) <= 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: invalid IP address: %s\n",
            d->index, d->ip_address.ip);
        if (close(d->sockfd) < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: close failed after IP error: %s\n",
                d->index, strerror(errno));
        }
        d->sockfd = -1;
        return;
    }
    setsockopt(d->sockfd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
    setsockopt(d->sockfd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
}

void watchdog_process(void *arg, long period)
{
    module_data_t *d = arg;

    d->current_time += 1;
    d->watchdog_running = 1;

    long long elapsed = d->current_time - d->last_received_time;
    if (elapsed < 0) {
        elapsed = 0;
    }
    if (elapsed > d->watchdog_timeout) {
        if (d->watchdog_expired == 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: Communication error. Check connections and restart LinuxCNC.\n", d->index);
            d->checksum_index_in = 1;
            d->checksum_index = 1;
        }
        d->watchdog_expired = 1;
    } else {
        d->watchdog_expired = 0;
    }
}

#if stepgens > 0
#define PIO_SETTINGS_COUNT (sizeof(pio_settings) / sizeof(pio_settings[0]))
static uint16_t nearest(uint32_t period)
{
    uint16_t min_diff = 65535;
    uint16_t value = (uint16_t)(period / cycle_time_ns);
    int16_t calc = 0;
    uint16_t index = 0;

    if (value < pio_settings[0].high_cycles) {
        return 0;
    }
    if (value > pio_settings[PIO_SETTINGS_COUNT - 1].high_cycles) {
        return PIO_SETTINGS_COUNT - 1;
    }

    for (uint16_t i = 0; i < PIO_SETTINGS_COUNT; i++) {
        calc = abs((int)pio_settings[i].high_cycles - (int)value);
        if (calc < min_diff) {
            min_diff = calc;
            index = i;
        }
    }
    return index;
}
#endif

#if debug == 1
static void printbuf(uint8_t *buf, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        printf("%02x", buf[i]);
    }

    printf("\n");
}
#endif

// ==================== I/O PIN HAL SETUP ====================
static int bb_hal_setup_pins(module_data_t *d, int j, int comp_id,
                             char *name, uint32_t nsize)
{
    int r;
    if (in_pins_no > 96) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: in_pins_no (%d) exceeds max 96\n", j, in_pins_no);
        return -1;
    }
    if (out_pins_no > 64) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: out_pins_no (%d) exceeds max 64\n", j, out_pins_no);
        return -1;
    }
    char prefix[64];
    snprintf(prefix, sizeof(prefix), instances > 1 ? module_name ".%d" : module_name, j);

    for (int i = 0; i < in_pins_no; i++) {
        memset(name, 0, nsize);
        snprintf(name, nsize, "%s.input.%s", prefix, input_pins[i].name);
        r = hal_pin_bit_newf(HAL_OUT, &d->input[i], comp_id, name, j);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                module_name ".%d: ERROR: pin connected export failed with err=%i\n", j, r);
            return r;
        }

        memset(name, 0, nsize);
        snprintf(name, nsize, "%s.input.%s-not", prefix, input_pins[i].name);
        r = hal_pin_bit_newf(HAL_OUT, &d->input_not[i], comp_id, name, j);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                module_name ".%d: ERROR: pin connected export failed with err=%i\n", j, r);
            return r;
        }
    }

    for (int i = 0; i < out_pins_no; i++) {
        if (output_pins[i] == GP_NULL) continue;
        memset(name, 0, nsize);
        snprintf(name, nsize, "%s.output.gp%d", prefix, output_pins[i]);
        r = hal_pin_bit_newf(HAL_IN, &d->output[i], comp_id, name, j);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                module_name ".%d: ERROR: pin connected export failed with err=%i\n", j, r);
            return r;
        }
        *d->output[i] = 0;
    }

    for (int i = 0; i < ex_in_count; i++) {
        memset(name, 0, nsize);
        snprintf(name, nsize, "%s.input.%s", prefix, ex_input_pins[i].name);
        r = hal_pin_bit_newf(HAL_OUT, &d->ex_input[i], comp_id, name, j);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                module_name ".%d: ERROR: ex pin export failed with err=%i\n", j, r);
            return r;
        }
        memset(name, 0, nsize);
        snprintf(name, nsize, "%s.input.%s-not", prefix, ex_input_pins[i].name);
        r = hal_pin_bit_newf(HAL_OUT, &d->ex_input_not[i], comp_id, name, j);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                module_name ".%d: ERROR: ex pin export failed with err=%i\n", j, r);
            return r;
        }
    }

    for (int i = 0; i < ex_out_count; i++) {
        memset(name, 0, nsize);
        snprintf(name, nsize, "%s.output.%s", prefix, ex_output_pins[i].name);
        r = hal_pin_bit_newf(HAL_IN, &d->ex_output[i], comp_id, name, j);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                module_name ".%d: ERROR: ex pin export failed with err=%i\n", j, r);
            return r;
        }
        *d->ex_output[i] = 0;
    }

    return 0;
}

static void bb_hal_process_recv(module_data_t *d)
{
    for (uint8_t i = 0; i < in_pins_no; i++) {
        if (input_pins[i].gpio < 32) {
            *d->input[i] = (rx_buffer->inputs[0] >> (input_pins[i].gpio & 31)) & 1;
        } else {
            *d->input[i] = (rx_buffer->inputs[1] >> ((input_pins[i].gpio - 32) & 31)) & 1;
        }
        *d->input_not[i] = !(*d->input[i]);
    }

    for (int i = 0; i < ex_in_count; i++) {
        *d->ex_input[i] = (rx_buffer->inputs[2] >> ex_input_pins[i].ex_num) & 1;
        *d->ex_input_not[i] = !(*d->ex_input[i]);
    }
}

static void bb_hal_process_send(module_data_t *d)
{
    uint32_t outs0 = 0;
    uint32_t outs1 = 0;

    for (uint8_t i = 0; i < out_pins_no; i++) {
        if (output_pins[i] == GP_NULL) continue;
        if (i < 32) {
            outs0 |= *d->output[i] == 1 ? 1u << i : 0;
        } else {
            outs1 |= *d->output[i] == 1 ? 1u << (i & 31) : 0;
        }
    }

    for (int i = 0; i < ex_out_count; i++) {
        if (*d->ex_output[i]) {
            outs0 |= 1u << ex_output_pins[i].ex_num;
        }
    }

    if (*d->probe_select) outs0 |= (1u << PROBE_SELECT_BIT);

    tx_buffer->outputs[0] = outs0;
    tx_buffer->outputs[1] = outs1;
}
// ===========================================================

static int _send(void *arg)
{
    module_data_t *d = arg;
    return sendto(d->sockfd, tx_buffer, tx_size, MSG_DONTROUTE | MSG_DONTWAIT, &d->remote_addr, sizeof(d->remote_addr));
}

void udp_io_process_recv(void *arg, long period)
{
    module_data_t *d = arg;

    if (d->watchdog_expired) {
        *d->step_ring_fill = 0;
        *d->step_ring_active = 0;
        *d->step_ring_underflow = 0;
        *d->step_ring_overflow = 0;
        *d->io_ready_out = 0;
        return;
    }

    struct sockaddr_in from_addr;
    socklen_t addrlen = sizeof(from_addr);
    int len = recvfrom(d->sockfd, rx_buffer, rx_size, 0, (struct sockaddr *)&from_addr, &addrlen);

    if (len == rx_size &&
        from_addr.sin_addr.s_addr == d->remote_addr.sin_addr.s_addr) {
        if (!tx_checksum_ok(rx_buffer) && debug_mode == 0) {
            rtapi_print_msg(RTAPI_MSG_INFO, module_name ".%d: checksum error: %d != %d\n",
                d->index, rx_buffer->checksum, calculate_checksum(rx_buffer, rx_size - 1));
            #if debug == 1
            printbuf((uint8_t *)rx_buffer, rx_size);
            #endif
            d->checksum_error = 1;
            *d->connected = 0;
            *d->step_ring_fill = 0;
            *d->step_ring_active = 0;
            *d->step_ring_underflow = 0;
            *d->step_ring_overflow = 0;
            *d->io_ready_out = 0;
            return;
        }
        *d->connected = 1;
        d->last_received_time = d->current_time;
        *d->jitter = 1000 - rx_buffer->jitter;
        *d->step_ring_fill = rx_buffer->step_ring_fill;
        *d->step_ring_active = (rx_buffer->step_ring_status & STEP_RING_STATUS_ACTIVE) != 0;
        *d->step_ring_underflow = (rx_buffer->step_ring_status & STEP_RING_STATUS_UNDERFLOW) != 0;
        *d->step_ring_overflow = (rx_buffer->step_ring_status & STEP_RING_STATUS_OVERFLOW) != 0;
        #if encoders > 0
            for (uint8_t i = 0; i < encoders; i++) {
                #if debug == 1
                    if (*d->enc_reset[i] == 1) {
                        *d->enc_reset[i] = 0;
                    }
                #endif
                uint32_t encoder_ts = rx_buffer->encoder_timestamp[i];
                int32_t encoder_count = rx_buffer->encoder_counter[i];
                uint8_t index_reset_event = (rx_buffer->interrupt_data >> i) & 0x01u;
                d->index_triggered[i] = *d->enc_index[i];

                if (*d->enc_scale[i] < 1.0f) *d->enc_scale[i] = 1.0f;
                if (!*d->enc_enabled[i]) {
                    *d->raw_count[i] = 0;
                    *d->enc_position[i] = 0;
                    *d->enc_velocity[i] = 0;
                    *d->enc_rpm[i] = 0;
                    d->delta_count[i] = 0;
                    d->delta_count_accum[i] = 0;
                    d->delta_time[i] = 0;
                    d->delta_pos[i] = 0.0f;
                    d->enc_timestamp[i] = 0;
                    d->enc_prev_pos[i] = 0.0f;
                    continue;
                }

                *d->enc_position[i] = (float)encoder_count / *d->enc_scale[i];

                if (d->enc_timestamp[i] == 0) {
                    *d->raw_count[i] = encoder_count;
                    d->enc_timestamp[i] = encoder_ts;
                    d->delta_count[i] = 0;
                    d->delta_count_accum[i] = 0;
                    d->delta_time[i] = 0;
                    d->delta_pos[i] = 0.0f;
                    d->enc_prev_pos[i] = *d->enc_position[i];
                    continue;
                }

                if (index_reset_event) {
                    // Rebase encoder timing/count state on index-reset event so
                    // one-shot index-enable reset does not inject a velocity spike.
                    *d->raw_count[i] = encoder_count;
                    d->enc_timestamp[i] = encoder_ts;
                    d->delta_count[i] = 0;
                    d->delta_count_accum[i] = 0;
                    d->delta_time[i] = 0;
                    d->delta_pos[i] = 0.0f;
                    d->enc_prev_pos[i] = *d->enc_position[i];
                    d->index_triggered[i] = false;
                    continue;
                }

                if (d->index_triggered[i]) {
                    d->delta_count[i] = encoder_count - *d->raw_count[i];
                    if (d->delta_count[i] < -(*d->enc_scale[i] / 2)) {
                        d->delta_count[i] += (int32_t)*d->enc_scale[i];
                    } else if (d->delta_count[i] > (*d->enc_scale[i] / 2)) {
                        d->delta_count[i] -= (int32_t)*d->enc_scale[i];
                    }
                } else {
                    d->delta_count[i] = (int32_t)((uint32_t)encoder_count - (uint32_t)*d->raw_count[i]);
                }

                *d->raw_count[i] = encoder_count;
                d->delta_time[i] = encoder_ts - d->enc_timestamp[i];
                d->delta_count_accum[i] = d->delta_count[i];

                update_encoder_velocity_from_deltas(d, i);

                d->enc_timestamp[i] = encoder_ts;
                d->enc_prev_pos[i] = *d->enc_position[i];
            }
        #endif
        bb_hal_process_recv(d);
    } else {
        *d->connected = 0;
    }
}

static void udp_io_process_send(void *arg, long period)
{
    module_data_t *d = arg;
    int32_t steps;
    uint8_t sign = 0;

    memset(tx_buffer, 0, tx_size);

    if (d->watchdog_expired) {
        *d->io_ready_out = 0;
        return;
    }

    if (*d->io_ready_in == 1) {
        *d->io_ready_out = *d->io_ready_in;
    } else {
        *d->io_ready_out = 0;
    }

    #if encoders > 0
    tx_buffer->enc_control = 0;
    for (int i = 0; i < encoders; i++) {
        tx_buffer->enc_control |= (uint8_t)(1 * d->index_triggered[i]) << (CTRL_SPINDEX + i);
        tx_buffer->enc_control |= (*d->enc_enabled[i]) << (4 + i);
    }
    #endif

    if (d->watchdog_running == 1) {
        #if stepgens > 0
        double f_steps[stepgens] = {0,};
        uint32_t max_f = 0;
        if (*d->pulse_width > 0) {
            max_f = (uint32_t)(1.0 / ((*d->pulse_width * 2) * 1e-9));
        }
        #if debug == 1
        *d->debug_freq = (float)max_f / 1000.0;
        #endif
        if (old_pulse_width != *d->pulse_width) {
            old_pulse_width = *d->pulse_width;
            uint32_t step_counter;
            uint32_t pio_cmd;
            total_cycles = (uint32_t)((period * (pico_clock / 1000)) / 1000000UL);
            uint16_t pio_index = nearest(*d->pulse_width);
            rtapi_print_msg(RTAPI_MSG_INFO, "Max frequency: %.4f KHz\n", max_f / 1000.0);
            rtapi_print_msg(RTAPI_MSG_INFO, "max pulse_width: %dnS\n", pio_settings[PIO_SETTINGS_COUNT - 1].high_cycles * (int)cycle_time_ns);
            rtapi_print_msg(RTAPI_MSG_INFO, "min pulse_width: %dnS\n", pio_settings[0].high_cycles * (int)cycle_time_ns);
            memset(timing, 0, sizeof(timing));
            for (uint16_t i = 1; i < 1024; i++) {
                step_counter = (uint32_t)((float)(total_cycles / i) - pio_settings[pio_index].high_cycles - dormant_cycles);
                pio_cmd = (uint32_t)(step_counter << 10 | i);
                timing[i] = pio_cmd;
            }
        }

        int32_t cmd[stepgens] = {0,};
        for (int i = 0; i < stepgens; i++) {
            float f_command = *d->command[i] + offset;
            if (*d->enable[i] == 0) {
                d->first_data[i] = true;
                cmd[i] = 0;
                continue;
            }
            if (d->first_data[i]) {
                d->prev_pos[i] = offset * *d->scale[i];
                d->first_data[i] = false;
            }
            if (*d->mode[i] == 0) {
                d->curr_pos[i] = f_command * *d->scale[i];
                f_steps[i] = (d->prev_pos[i] - d->curr_pos[i]);
                steps = (int32_t)f_steps[i];

                #if debug == 1
                *d->debug_steps[i] -= steps;
                if (*d->debug_steps_reset == 1) {
                    *d->debug_steps[i] = 0;
                    if (i == stepgens - 1) {
                        *d->debug_steps_reset = 0;
                    }
                }
                #endif
                steps = abs(steps);
                if (steps < 0) steps = 1023;
                if (steps > 1023) steps = 1023;

                sign = 0;
                if (d->prev_pos[i] < d->curr_pos[i]) {
                    sign = 1;
                }
                d->prev_pos[i] = d->curr_pos[i];
                if (steps > 0) {
                    cmd[i] = (timing[steps] | (sign << 31));
                } else {
                    cmd[i] = 0;
                }
            } else {
                float velocity = *d->command[i];
                float steps_per_sec = velocity * *d->scale[i];
                sign = (velocity >= 0) ? 1 : 0;

                steps_per_sec = fabs(steps_per_sec);
                if (steps_per_sec > max_f) {
                    steps_per_sec = max_f;
                }
                uint32_t steps_per_cycle = (uint32_t)(steps_per_sec * (period / 1000000000.0));
                if (steps_per_cycle > 1023) steps_per_cycle = 1023;
                #if debug == 1
                *d->debug_steps[i] += (uint16_t)steps_per_cycle;
                if (*d->debug_steps_reset == 1) {
                    *d->debug_steps[i] = 0;
                    *d->debug_steps_reset = 0;
                }
                #endif
                if (steps_per_cycle > 0) {
                    cmd[i] = timing[steps_per_cycle] | (sign << 31);
                } else {
                    cmd[i] = 0;
                }
            }
            *d->feedback[i] = *d->command[i];
        }
        for (uint8_t i = 0; i < stepgens; i++) {
            tx_buffer->stepgen_command[i] = cmd[i];
            tx_buffer->dir_setup_ns[i] = (uint16_t)*d->dir_setup_ns[i];
        }
        tx_buffer->pio_timing = nearest(*d->pulse_width);
        #endif

    bb_hal_process_send(d);

    #if use_pwm == 1
    for (int i = 0; i < pwm_count; i++) {
        if (*d->pwm_enable[i]) {
            if (*d->pwm_frequency[i] > 0) {
                if (*d->pwm_frequency[i] > 1000000) {
                    *d->pwm_frequency[i] = 1000000;
                }
                if (*d->pwm_frequency[i] < 1908) {
                    *d->pwm_frequency[i] = 1908;
                }
                if (*d->pwm_min_limit[i] > 0 && *d->pwm_output[i] < *d->pwm_min_limit[i]) {
                    *d->pwm_output[i] = *d->pwm_min_limit[i];
                }
                uint16_t wrap = pwm_calculate_wrap(*d->pwm_frequency[i]);
                if (*d->pwm_maxscale[i] < 1.0f) *d->pwm_maxscale[i] = 1.0f;
                uint16_t duty_cycle = (uint16_t)(round(((float)*d->pwm_output[i] / *d->pwm_maxscale[i]) * wrap));
                tx_buffer->pwm_duty[i] = duty_cycle;
                tx_buffer->pwm_frequency[i] = *d->pwm_frequency[i];
            } else {
                tx_buffer->pwm_duty[i] = 0;
            }
        }
    }
    #endif

    tx_buffer->packet_id = d->tx_counter;
    tx_buffer->checksum = calculate_checksum(tx_buffer, tx_size - 1);
    _send(d);
    d->tx_counter++;

    } else {
        if (!d->error_triggered) {
            d->error_triggered = true;
            *d->io_ready_out = 0;
            rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: watchdog not running\n", d->index);
            return;
        }
    }
}

static int parse_ip_port(const char *input, IpPort *output, int max_count)
{
    if (input == NULL || output == NULL || max_count <= 0) {
        return -1;
    }

    char *input_copy = strdup(input);
    if (input_copy == NULL) {
        return -1;
    }

    char *saveptr1;
    char *entry;
    int count = 0;

    for (entry = strtok_r(input_copy, ";", &saveptr1);
         entry != NULL && count < max_count;
         entry = strtok_r(NULL, ";", &saveptr1)) {
        char *colon = strchr(entry, ':');

        if (colon == NULL) {
            rtapi_print_msg(RTAPI_MSG_ERR, module_name ": Invalid entry format: %s\n", entry);
            continue;
        }

        *colon = '\0';
        char *ip = entry;
        char *port_str = colon + 1;

        char *endptr;
        long port = strtol(port_str, &endptr, 10);

        if (*endptr != '\0' || port < 0 || port > 65535) {
            rtapi_print_msg(RTAPI_MSG_ERR, module_name ": Invalid port number: %s\n", port_str);
            continue;
        }

        snprintf(output[count].ip, sizeof(output[count].ip), "%s", ip);
        output[count].port = (int)port;

        count++;
    }

    free(input_copy);
    return count;
}

int rtapi_app_main(void)
{
    int r;

    rtapi_set_msg_level(RTAPI_MSG_INFO);

    if (module_init() < 0) return -1;

    if (ip_address == NULL) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ": ip_address not specified\n");
        return -1;
    }

    IpPort results[MAX_CHAN];
    instances = parse_ip_port((char *)ip_address, results, MAX_CHAN);

    if (instances <= 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ": no valid IP:port entries found\n");
        return -1;
    }

    for (int i = 0; i < instances; i++) {
        rtapi_print_msg(RTAPI_MSG_INFO, "Parsed IP: %s, Port: %d\n", results[i].ip, results[i].port);
    }

    if (instances > MAX_CHAN) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ": Too many channels, max %d allowed\n", MAX_CHAN);
        return -1;
    }

    hal_data = hal_malloc(instances * sizeof(module_data_t));
    if (hal_data == NULL) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ": hal_data allocation failed\n");
        return -1;
    }

    comp_id = hal_init(module_name);
    if (comp_id < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: hal_init failed: %d\n", 0, comp_id);
        return comp_id;
    }

    char name[64] = {0};

    for (int j = 0; j < instances; j++) {

        hal_data[j].checksum_index = 1;
        hal_data[j].checksum_index_in = 1;
        hal_data[j].index = j;
        hal_data[j].watchdog_timeout = 10;
        hal_data[j].current_time = 0;
        hal_data[j].last_received_time = 0;
        hal_data[j].watchdog_expired = 0;
        hal_data[j].watchdog_running = 0;
        for (int k = 0; k < stepgens; k++)
            hal_data[j].first_data[k] = true;
        hal_data[j].error_triggered = false;
        hal_data[j].sockfd = -1;

        hal_data[j].ip_address = results[j];
        rtapi_print_msg(RTAPI_MSG_INFO, module_name ".%d: init_socket\n", j);
        init_socket(&hal_data[j]);
        rtapi_print_msg(RTAPI_MSG_INFO, module_name ".%d: init_socket ready..\n", j);

        uint32_t nsize = sizeof(name);
        char prefix[64];
        snprintf(prefix, sizeof(prefix), instances > 1 ? module_name ".%d" : module_name, j);
        PIN_BIT_INIT(&hal_data[j].probe_select, HAL_IN, 0, "%s.probe-select", prefix);
        PIN_BIT(&hal_data[j].connected, HAL_IN, "%s.connected", prefix);

        #if stepgens > 0
        PIN_U32_INIT(&hal_data[j].pulse_width, HAL_IN, default_pulse_width, "%s.stepgen.pulse-width", prefix);

            #if debug == 1
            PIN_FLOAT(&hal_data[j].debug_freq, HAL_OUT, "%s.stepgen.max-freq-khz", prefix);
            PIN_BIT(&hal_data[j].debug_steps_reset, HAL_IN, "%s.stepgen.debug-steps-reset", prefix);
            #endif
        #endif

        PIN_S32(&hal_data[j].jitter, HAL_OUT, "%s.jitter", prefix);
        PIN_U32_INIT(&hal_data[j].step_ring_fill, HAL_OUT, 0, "%s.stepgen.ring-fill", prefix);
        PIN_BIT_INIT(&hal_data[j].step_ring_active, HAL_OUT, 0, "%s.stepgen.ring-active", prefix);
        PIN_BIT_INIT(&hal_data[j].step_ring_underflow, HAL_OUT, 0, "%s.stepgen.ring-underflow", prefix);
        PIN_BIT_INIT(&hal_data[j].step_ring_overflow, HAL_OUT, 0, "%s.stepgen.ring-overflow", prefix);


        r = bb_hal_setup_pins(&hal_data[j], j, comp_id, name, nsize);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, module_name ".%d: ERROR: board pin setup failed err=%i\n", j, r);
            hal_exit(comp_id);
            return r;
        }
        /* Breakout boards with analog channels register them in board helpers. */
        #if ANALOG_CH > 0
            for (int i = 0; i < ANALOG_CH; i++) {
                PIN_BIT_INIT(&hal_data[j].analog_enable[i], HAL_IN, 0, "%s.analog.%d.enable", prefix, i);
                PIN_FLOAT_INIT(&hal_data[j].analog_min[i], HAL_IN, 0.0, "%s.analog.%d.minimum", prefix, i);
                PIN_FLOAT_INIT(&hal_data[j].analog_max[i], HAL_IN, 0.0, "%s.analog.%d.maximum", prefix, i);
                PIN_FLOAT_INIT(&hal_data[j].analog_value[i], HAL_IN, 0.0, "%s.analog.%d.value", prefix, i);
            }
        #endif

        #if use_pwm == 1
            const PwmPin pwm_pin_cfg[pwm_count] = PWM_PINS;
            for (int i = 0; i < pwm_count; ++i) {
                PIN_BIT_INIT(&hal_data[j].pwm_enable[i], HAL_IN, 0, "%s.%s.enable", prefix, pwm_pin_cfg[i].name);
                PIN_U32(&hal_data[j].pwm_output[i], HAL_IN, "%s.%s.duty", prefix, pwm_pin_cfg[i].name);
                PIN_U32_INIT(&hal_data[j].pwm_frequency[i], HAL_IN, default_pwm_frequency, "%s.%s.frequency", prefix, pwm_pin_cfg[i].name);
                PIN_U32_INIT(&hal_data[j].pwm_min_limit[i], HAL_IN, 0, "%s.%s.min-limit", prefix, pwm_pin_cfg[i].name);
                PIN_U32_INIT(&hal_data[j].pwm_maxscale[i], HAL_IN, default_pwm_maxscale, "%s.%s.max-scale", prefix, pwm_pin_cfg[i].name);
            }
        #endif

        #if stepgens > 0
        for (int i = 0; i < stepgens; i++) {
            #if debug == 1
            PIN_S32_INIT(&hal_data[j].debug_steps[i], HAL_OUT, 0, "%s.stepgen.%d.debug-steps", prefix, i);
            #endif

            PIN_FLOAT(&hal_data[j].command[i], HAL_IN, "%s.stepgen.%d.command", prefix, i);
            PIN_FLOAT_INIT(&hal_data[j].scale[i], HAL_IN, default_step_scale, "%s.stepgen.%d.step-scale", prefix, i);
            PIN_FLOAT(&hal_data[j].feedback[i], HAL_OUT, "%s.stepgen.%d.feedback", prefix, i);
            PIN_BIT_INIT(&hal_data[j].mode[i], HAL_IN, 0, "%s.stepgen.%d.mode", prefix, i);
            PIN_BIT_INIT(&hal_data[j].enable[i], HAL_IN, 0, "%s.stepgen.%d.enable", prefix, i);
            PIN_U32_INIT(&hal_data[j].dir_setup_ns[i], HAL_IN, 2500, "%s.stepgen.%d.dir-setup", prefix, i);
        }
        #endif
        #if encoders > 0
        #if use_stepcounter == 1
        #define e_name ".stepcounter"
        #else
        #define e_name ".encoder"
        #endif
        for (int i = 0; i < encoders; i++) {
            hal_data[j].delta_time[i] = 0;
            hal_data[j].delta_count_accum[i] = 0;
            hal_data[j].enc_timestamp[i] = 0;
            PIN_S32(&hal_data[j].raw_count[i], HAL_OUT, "%s" e_name ".%s.raw-count", prefix, encoder_config[i].name);
            PIN_FLOAT(&hal_data[j].enc_position[i], HAL_OUT, "%s" e_name ".%s.position", prefix, encoder_config[i].name);
            PIN_FLOAT_INIT(&hal_data[j].enc_scale[i], HAL_IN, 1, "%s" e_name ".%s.scale", prefix, encoder_config[i].name);
            PIN_FLOAT(&hal_data[j].enc_velocity[i], HAL_OUT, "%s" e_name ".%s.velocity-rps", prefix, encoder_config[i].name);
            PIN_BIT(&hal_data[j].enc_index[i], HAL_IN, "%s" e_name ".%s.index-enable", prefix, encoder_config[i].name);
            PIN_FLOAT(&hal_data[j].enc_rpm[i], HAL_OUT, "%s" e_name ".%s.velocity-rpm", prefix, encoder_config[i].name);
            PIN_FLOAT_INIT(&hal_data[j].enc_filter_tau[i], HAL_IN, 0.020f, "%s" e_name ".%s.filter-tau", prefix, encoder_config[i].name);
            PIN_FLOAT_INIT(&hal_data[j].enc_filter_dt[i], HAL_IN, 0.001f, "%s" e_name ".%s.filter-dt", prefix, encoder_config[i].name);
            PIN_FLOAT_INIT(&hal_data[j].enc_vel_threshold[i], HAL_IN, 0.001f, "%s" e_name ".%s.vel-threshold", prefix, encoder_config[i].name);
            PIN_BIT_INIT(&hal_data[j].enc_enabled[i], HAL_IN, 1, "%s" e_name ".%s.enabled", prefix, encoder_config[i].name);
            #if debug == 1
            PIN_BIT_INIT(&hal_data[j].enc_reset[i], HAL_IN, 0, "%s" e_name ".%s.debug-reset", prefix, encoder_config[i].name);
            hal_data[j].enc_prev_pos[i] = 0;
            #endif
        }
        #endif

        PIN_U32(&hal_data[j].period, HAL_IN, "%s.period", prefix);
        PIN_BIT(&hal_data[j].io_ready_in, HAL_IN, "%s.io-ready-in", prefix);
        PIN_BIT(&hal_data[j].io_ready_out, HAL_OUT, "%s.io-ready-out", prefix);
        #pragma message "Adding export functions. (watchdog)"
        char watchdog_name[48] = {0};
        snprintf(watchdog_name, sizeof(watchdog_name), "%s.watchdog-process", prefix);
        rtapi_print_msg(RTAPI_MSG_INFO, module_name ".%d: hal_export_funct for watchdog-process: %d init...\n", j, r);
        r = hal_export_funct(watchdog_name, watchdog_process, &hal_data[j], 1, 1, comp_id);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, module_name ": hal_export_funct failed for watchdog-process: %d\n", r);
            hal_exit(comp_id);
            return r;
        }
        rtapi_print_msg(RTAPI_MSG_INFO, module_name ".%d: hal_export_funct for watchdog-process: %d\n", j, r);

        #pragma message "Adding export functions. (process-send)"
        char process_send[48] = {0};
        snprintf(process_send, sizeof(process_send), "%s.process-send", prefix);
        rtapi_print_msg(RTAPI_MSG_INFO, module_name ".%d: hal_export_funct for process-send %d init...\n", j, r);
        r = hal_export_funct(process_send, udp_io_process_send, &hal_data[j], 1, 1, comp_id);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, module_name ": hal_export_funct failed: %d\n", r);
            hal_exit(comp_id);
            return r;
        }
        rtapi_print_msg(RTAPI_MSG_INFO, module_name ".%d: hal_export_funct for process_send: %d\n", j, r);

        #pragma message "Adding export functions. (process-recv)"
        char process_recv[48] = {0};
        snprintf(process_recv, sizeof(process_recv), "%s.process-recv", prefix);
        rtapi_print_msg(RTAPI_MSG_INFO, module_name ".%d: hal_export_funct for process-recv: %d init...\n", j, r);
        r = hal_export_funct(process_recv, udp_io_process_recv, &hal_data[j], 1, 1, comp_id);
        if (r < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, module_name ": hal_export_funct failed: %d\n", r);
            hal_exit(comp_id);
            return r;
        }
        rtapi_print_msg(RTAPI_MSG_INFO, module_name ".%d: hal_export_funct for process_recv: %d\n", j, r);
    }

    r = hal_ready(comp_id);
    if (r < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, module_name ": hal_ready failed: %d\n", r);
        hal_exit(comp_id);
        return r;
    }

    rtapi_print_msg(RTAPI_MSG_INFO, module_name ": component ready.\n");
    return 0;
}

void rtapi_app_exit(void)
{
    if (hal_data == NULL) return;
    for (int i = 0; i < instances; i++) {
        rtapi_print_msg(RTAPI_MSG_INFO, module_name ".%d: Exiting component\n", i);
        if (hal_data[i].sockfd >= 0) close(hal_data[i].sockfd);
    }
    hal_exit(comp_id);
    free(rx_buffer);
    free(tx_buffer);
}
