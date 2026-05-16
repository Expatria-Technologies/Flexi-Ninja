#include "flexgpio.h"
#include "config.h"

#if use_flexgpio == 1

#include <hardware/i2c.h>
#include <hardware/gpio.h>
#include <pico/stdlib.h>
#include <string.h>

#define EX_INPUT_MASK 0x07F8u

volatile bool flexgpio_pending = false;

static void flexgpio_irq_handler(void)
{
    flexgpio_pending = true;
}

void flexgpio_init(void)
{
    i2c_init(i2c1, 1000 * 1000);
    gpio_set_function(FLEXGPIO_SDA, GPIO_FUNC_I2C);
    gpio_set_function(FLEXGPIO_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(FLEXGPIO_SDA);
    gpio_pull_up(FLEXGPIO_SCL);

    gpio_init(FLEXGPIO_IRQ);
    gpio_set_dir(FLEXGPIO_IRQ, GPIO_IN);
    gpio_pull_up(FLEXGPIO_IRQ);
    gpio_set_irq_enabled_with_callback(FLEXGPIO_IRQ, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &flexgpio_irq_handler);

    output_packet_t out;
    memset(&out, 0, sizeof(out));
    out.mcu_irq_mask = EX_INPUT_MASK;
    out.probe_irq_mask = (1u << 4);
    flexgpio_write(&out);

    flexgpio_pending = true;
}

void flexgpio_read(input_packet_t *pkt)
{
    uint8_t offset = 0;
    i2c_write_blocking(i2c1, FLEXGPIO_ADDR, &offset, 1, true);
    i2c_read_blocking(i2c1, FLEXGPIO_ADDR, (uint8_t *)pkt, sizeof(input_packet_t), false);
}

void flexgpio_write(const output_packet_t *pkt)
{
    uint8_t offset = 0;
    i2c_write_blocking(i2c1, FLEXGPIO_ADDR, &offset, 1, true);
    i2c_write_blocking(i2c1, FLEXGPIO_ADDR, (const uint8_t *)pkt, sizeof(output_packet_t), false);
}

void flexgpio_sync_inputs(uint32_t *inputs)
{
    if (!flexgpio_pending)
        return;

    flexgpio_pending = false;

    input_packet_t in;
    flexgpio_read(&in);

    inputs[2] = in.value;
}

void flexgpio_push_outputs(const uint32_t *outputs)
{
    output_packet_t out;
    out.value = outputs[0];
    out.mcu_irq_mask = EX_INPUT_MASK;
    out.probe_irq_mask = (outputs[0] & (1u << 31)) ? (1u << 3) : (1u << 4);
    out.value &= ~(1u << 31);
    flexgpio_write(&out);
}

#endif
