#ifndef FLEXGPIO_H
#define FLEXGPIO_H

#include <stdint.h>
#include <stdbool.h>

#define FLEXGPIO_BUF_SIZE 8

typedef struct __attribute__((packed)) {
    uint32_t value;
    uint16_t mcu_irq_mask;
    uint16_t probe_irq_mask;
} output_packet_t;

typedef struct __attribute__((packed)) {
    uint32_t value;
    uint16_t mcu_irq_mask;
    uint16_t probe_irq_mask;
} input_packet_t;

extern volatile bool flexgpio_pending;

void flexgpio_init(void);
void flexgpio_read(input_packet_t *pkt);
void flexgpio_write(const output_packet_t *pkt);
void flexgpio_sync_inputs(uint32_t *inputs);
void flexgpio_push_outputs(const uint32_t *outputs);

#endif
