/*
 * SPI Master Test for Raspberry Pi 4
 *
 * Communicates with Pico 2 SPI slave.
 * Uses BCM2835 library for direct register access.
 *
 * Wiring (Pi 4 -> Pico 2):
 *   GPIO22       -> Pico GP08  (INT, "data ready" signal)
 *   GPIO9  (MISO) <- Pico GP43  (MISO)
 *   GPIO10 (MOSI) -> Pico GP40  (MOSI)
 *   GPIO11 (SCLK) -> Pico GP42  (SCK)
 *   GND           -> Pico GND
 *
 * The Pico's SPI CS pin (GP41) should be tied to GND or driven
 * by another Pi GPIO. This test uses the SPI peripheral's CE
 * lines automatically, but drives INT via a separate GPIO.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <bcm2835.h>

#define TRANSFER_SIZE   32
#define REPEAT_COUNT    5

#define PIN_INT 22  // Pi GPIO22 -> Pico GP08

static uint8_t tx_buf[TRANSFER_SIZE];
static uint8_t rx_buf[TRANSFER_SIZE];

int main(int argc, char **argv)
{
    int repeat = REPEAT_COUNT;
    if (argc > 1)
        repeat = atoi(argv[1]);

    if (!bcm2835_init()) {
        fprintf(stderr, "bcm2835_init failed (run as root?)\n");
        return 1;
    }

    // Configure SPI0 as master, mode 3 (CPOL=1, CPHA=1)
    bcm2835_spi_begin();
    bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE3);
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_64);  // ~1.7 MHz

    // Configure INT pin (GPIO22) as output, initially high
    bcm2835_gpio_fsel(PIN_INT, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_write(PIN_INT, HIGH);

    printf("SPI Master Test (%d transactions, %d bytes each)\n", repeat, TRANSFER_SIZE);
    printf("Speed: ~1.7 MHz (divider 64), Mode 3\n");
    printf("INT pin: GPIO%d\n\n", PIN_INT);

    // Prepare TX pattern: incrementing bytes
    for (int i = 0; i < TRANSFER_SIZE; i++)
        tx_buf[i] = (uint8_t)i;

    for (int t = 0; t < repeat; t++) {
        memset(rx_buf, 0, TRANSFER_SIZE);

        // Assert INT to tell Pico to prepare
        printf("Driving INT low...\n");
        bcm2835_gpio_write(PIN_INT, LOW);
        printf("INT readback: %d\n", bcm2835_gpio_lev(PIN_INT));
        usleep(1000);

        // Do full-duplex SPI transfer
        uint8_t rx_first = 0;
        bcm2835_spi_transfernb((char *)tx_buf, (char *)rx_buf, TRANSFER_SIZE);

        // Deassert INT
        bcm2835_gpio_write(PIN_INT, HIGH);
        printf("INT released, readback: %d\n", bcm2835_gpio_lev(PIN_INT));

        // Print results
        printf("Tx#%d: TX=", t);
        for (int i = 0; i < TRANSFER_SIZE; i++)
            printf("%02x ", tx_buf[i]);
        printf("\n       RX=");
        for (int i = 0; i < TRANSFER_SIZE; i++)
            printf("%02x ", rx_buf[i]);
        printf("  (first RX byte: %02x)\n\n", rx_buf[0]);

        usleep(50000);
    }

    bcm2835_spi_end();
    bcm2835_close();
    return 0;
}
