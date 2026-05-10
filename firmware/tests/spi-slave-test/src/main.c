#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

// Pin definitions (matching main firmware)
#define SPI_PORT        spi1
#define GPIO_MISO       43
#define GPIO_CS         41
#define GPIO_SCK        42
#define GPIO_MOSI       40
#define GPIO_INT        8
#define LED_GPIO        7
#define TRANSFER_SIZE   32

// Debug GPIOs for logic analyzer
#define DBG_PREFILL     2
#define DBG_EXCHANGE    3

static uint8_t tx_pattern[TRANSFER_SIZE];
static uint8_t rx_buffer[TRANSFER_SIZE];

static void prepare_pattern(uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
        buf[i] = (i & 1) ? 0xAA : 0x55;
}

static uint16_t prefill_fifo(const uint8_t *buf, uint16_t len)
{
    uint16_t count = 0;
    while (count < len && (spi_get_hw(SPI_PORT)->sr & (1 << 1))) {
        spi_get_hw(SPI_PORT)->dr = buf[count++];
    }
    return count;
}

static void do_exchange(uint8_t *rx, const uint8_t *tx, uint16_t len, uint16_t pre_filled)
{
    gpio_put(DBG_EXCHANGE, 1);
    uint16_t tx_done = pre_filled;
    uint16_t rx_done = 0;
    while (rx_done < len) {
        if (spi_get_hw(SPI_PORT)->sr & (1 << 2)) {
            rx[rx_done++] = (uint8_t)spi_get_hw(SPI_PORT)->dr;
        }
        if (tx_done < len && (spi_get_hw(SPI_PORT)->sr & (1 << 1))) {
            spi_get_hw(SPI_PORT)->dr = tx[tx_done++];
        }
    }
    while (spi_get_hw(SPI_PORT)->sr & (1 << 2)) {
        (void)spi_get_hw(SPI_PORT)->dr;
    }
    gpio_put(DBG_EXCHANGE, 0);
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);
    printf("SPI Slave Test (RP2350)\n");

    gpio_init(DBG_PREFILL);
    gpio_set_dir(DBG_PREFILL, GPIO_OUT);
    gpio_init(DBG_EXCHANGE);
    gpio_set_dir(DBG_EXCHANGE, GPIO_OUT);

    gpio_init(GPIO_INT);
    gpio_set_dir(GPIO_INT, GPIO_IN);
    gpio_pull_up(GPIO_INT);

    gpio_init(LED_GPIO);
    gpio_set_dir(LED_GPIO, GPIO_OUT);


    gpio_init(GPIO_MOSI);
    gpio_set_dir(GPIO_MOSI, GPIO_OUT);
    gpio_put(GPIO_MOSI, 1);
    sleep_ms(50);
    gpio_put(GPIO_MOSI, 0);
    sleep_ms(50);
    gpio_put(GPIO_MOSI, 1);
    sleep_ms(50);
    gpio_put(GPIO_MOSI, 0);
    sleep_ms(100);

    prepare_pattern(tx_pattern, TRANSFER_SIZE);
    printf("TX pattern: ");
    for (int i = 0; i < TRANSFER_SIZE; i++)
        printf("%02x ", tx_pattern[i]);
    printf("\n");

    printf("Initializing SPI slave...\n");
    spi_init(SPI_PORT, 4000 * 1000);
    spi_set_slave(SPI_PORT, true);

    spi_get_hw(SPI_PORT)->cpsr = 12;
    hw_write_masked(&spi_get_hw(SPI_PORT)->cr0, 0, SPI_SSPCR0_SCR_BITS);

    spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    gpio_set_function(GPIO_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(GPIO_MISO, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_CS,   GPIO_FUNC_SPI);
    gpio_set_pulls(GPIO_CS, true, false);

    printf("SPI slave initialized.\n");

    if (!(spi_get_hw(SPI_PORT)->cr1 & SPI_SSPCR1_SSE_BITS))
        printf("ERROR: SSE=0!\n");

    uint32_t transaction_count = 0;

    while (1) {
        gpio_put(DBG_PREFILL, 1);
        gpio_put(LED_GPIO, 1);

        uint16_t n = prefill_fifo(tx_pattern, TRANSFER_SIZE);

        gpio_put(DBG_PREFILL, 0);

        while (gpio_get(GPIO_INT))
            tight_loop_contents();

        memset(rx_buffer, 0, TRANSFER_SIZE);
        do_exchange(rx_buffer, tx_pattern, TRANSFER_SIZE, n);

        transaction_count++;
        printf("Tx#%lu: RX=", (unsigned long)transaction_count);
        for (int i = 0; i < TRANSFER_SIZE; i++)
            printf("%02x ", rx_buffer[i]);
        printf("\n");

        gpio_put(LED_GPIO, 0);

        sleep_us(10);
    }
}
