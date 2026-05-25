#ifndef IRQ_ENCODER_H
#define IRQ_ENCODER_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "config.h"

// Must be called early (before any encoder edge could arrive) to
// capture the initial pin state and register the GPIO IRQ callback.
// `callback` is the ISR handler shared with index-pin IRQs.
void irq_encoder_init(const EncoderPin config[], uint8_t count,
                      gpio_irq_callback_t callback);

// Called from the GPIO ISR for every edge.  If `gpio` is the A or B
// pin of the IRQ-counted encoder with index `i`, the internal counter
// and state are updated and the function returns true.
bool irq_encoder_handle_edge(uint8_t i, uint gpio);

// Read the current counter.  The value is stored in a volatile int32_t
// so cross-core reads are safe without explicit synchronisation.
int32_t irq_encoder_read_count(uint8_t i);

// Reset the counter to zero (e.g. on index pulse or debug-reset).
void irq_encoder_reset_count(uint8_t i);

// Enable or disable GPIO edge IRQs for an IRQ-based encoder.
// Enable re-initialises the internal state (prev_state + counter) so no
// stale edges produce a false count on re-arm.
void irq_encoder_enable(uint8_t i);
void irq_encoder_disable(uint8_t i);

// Returns true when the encoder at this index uses IRQ counting
// instead of a PIO state machine.
static inline bool irq_encoder_is_irq(const EncoderPin *cfg) {
    return cfg->base_pin >= 32;
}

#endif
