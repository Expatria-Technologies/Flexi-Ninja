#include "irq_encoder.h"
#include "config.h"

static volatile int32_t counter[encoders] = {0};
static uint8_t prev_state[encoders]  = {0};

// Standard quadrature transition table: index = (old<<2)|new, A=bit0, B=bit1.
static const int8_t transition[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

static const EncoderPin *cfg;
static uint8_t cfg_count;
static gpio_irq_callback_t irq_cb;


void irq_encoder_init(const EncoderPin config[], uint8_t count,
                      gpio_irq_callback_t callback)
{
    cfg = config;
    cfg_count = count;
    irq_cb = callback;

    for (uint8_t i = 0; i < count; i++) {
        if (config[i].base_pin < 32) continue;

        // Capture the current pin state, then swap bits so the
        // stored value matches the standard {A=bit0,B=bit1}
        // encoding that the transition table expects.
        uint8_t raw = (gpio_get_all64() >> config[i].base_pin) & 3;
        prev_state[i] = ((raw & 2) >> 1) | ((raw & 1) << 1);
        counter[i] = 0;

        // Both edges on both A and B pins.
        gpio_set_irq_enabled_with_callback(config[i].base_pin,
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, callback);
        gpio_set_irq_enabled_with_callback(config[i].base_pin + 1,
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, callback);
    }
}


bool irq_encoder_handle_edge(uint8_t i, uint gpio)
{
    if (i >= cfg_count) return false;
    if (cfg[i].base_pin < 32) return false;
    if (gpio != cfg[i].base_pin && gpio != cfg[i].base_pin + 1) return false;

    // Read both pins, swap bits into {A=bit0, B=bit1}.
    uint8_t raw = (gpio_get_all64() >> cfg[i].base_pin) & 3;
    uint8_t cur = ((raw & 2) >> 1) | ((raw & 1) << 1);
    uint8_t idx = (prev_state[i] << 2) | cur;

    counter[i] += transition[idx];
    prev_state[i] = cur;
    return true;
}


int32_t irq_encoder_read_count(uint8_t i)
{
    return counter[i];
}


void irq_encoder_reset_count(uint8_t i)
{
    counter[i] = 0;
    if (i < cfg_count && cfg[i].base_pin >= 32) {
        uint8_t raw = (gpio_get_all64() >> cfg[i].base_pin) & 3;
        prev_state[i] = ((raw & 2) >> 1) | ((raw & 1) << 1);
    }
}


void irq_encoder_enable(uint8_t i)
{
    if (i >= cfg_count || cfg[i].base_pin < 32) return;
    uint8_t raw = (gpio_get_all64() >> cfg[i].base_pin) & 3;
    prev_state[i] = ((raw & 2) >> 1) | ((raw & 1) << 1);
    counter[i] = 0;
    gpio_set_irq_enabled_with_callback(cfg[i].base_pin,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, irq_cb);
    gpio_set_irq_enabled_with_callback(cfg[i].base_pin + 1,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, irq_cb);
}


void irq_encoder_disable(uint8_t i)
{
    if (i >= cfg_count || cfg[i].base_pin < 32) return;
    gpio_set_irq_enabled_with_callback(cfg[i].base_pin,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, irq_cb);
    gpio_set_irq_enabled_with_callback(cfg[i].base_pin + 1,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false, irq_cb);
}
