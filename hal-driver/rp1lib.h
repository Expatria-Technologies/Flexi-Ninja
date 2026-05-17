#ifndef RP1_H
#define RP1_H

#include <stdint.h>
#include <stddef.h>

#ifndef DEBUG
#define DEBUG 1
#endif

#include "gpiochip_rp1.h"
#include "spi-dw.h"

#define RP1_BAR1 0x1F00000000
#define RP1_BAR1_LEN 0x400000

#define SPI_DEV 6
#define RP1_SPI_SPEED 200000000

#define RP1_SPI0_BASE 0x050000
#define RP1_SPI1_BASE 0x054000
#define RP1_SPI2_BASE 0x058000
#define RP1_SPI3_BASE 0x05C000
#define RP1_SPI4_BASE 0x060000
#define RP1_SPI5_BASE 0x064000
#define RP1_SPI6_BASE 0x068000
#define RP1_SPI7_BASE 0x06C000
#define RP1_SPI8_BASE 0x04C000

#define NUM_HDR_PINS 40
#define MAX_GPIO_PINS 300

#define GPIO_INVALID (~0U)
#define GPIO_GND (~1U)
#define GPIO_5V (~2U)
#define GPIO_3V3 (~3U)
#define GPIO_1V8 (~4U)
#define GPIO_OTHER (~5U)

typedef struct {
} SPI_DEVICE_T;

typedef struct {
    const GPIO_CHIP_T *chip;
    const char *name;
    int mem_fd;
    void *priv;
    uint64_t phys_addr;
    unsigned num_gpios;
    uint64_t base;
    struct dw_spi spi[SPI_DEV];
    struct dw_spi_cfg spi_cfg[SPI_DEV];
    struct spi_device spi_dev[SPI_DEV];
} RP1_DEVICE_T;

int rp1lib_init(void);
int rp1lib_deinit(void);

int rp1spi_init(uint8_t spi_num, uint8_t chip_select, uint8_t mode, uint32_t freq);
rp1_gpio_fsel_result rp1_get_gpio_fsel_from_name(const char *pin_name);
void rp1spi_transfer(uint8_t spi_num, const void *txbuf, void *rxbuf, uint8_t len);

int gpio_num_is_valid(unsigned gpio);
GPIO_DIR_T gpio_get_dir(unsigned gpio);
void gpio_set_dir(unsigned gpio, GPIO_DIR_T dir);
GPIO_FSEL_T gpio_get_fsel(unsigned gpio);
void gpio_set_fsel(unsigned gpio, const GPIO_FSEL_T func);
void gpio_set_drive(unsigned gpio, GPIO_DRIVE_T drv);
void gpio_set(unsigned gpio);
void gpio_clear(unsigned gpio);
int gpio_get_level(unsigned gpio);
GPIO_DRIVE_T gpio_get_drive(unsigned gpio);
GPIO_PULL_T gpio_get_pull(unsigned gpio);
void gpio_set_pull(unsigned gpio, GPIO_PULL_T pull);

void gpio_get_pin_range(unsigned *first, unsigned *last);
unsigned gpio_for_pin(int pin);
int gpio_to_pin(unsigned gpio);
unsigned gpio_get_gpio_by_name(const char *name, int namelen);
const char *gpio_get_name(unsigned gpio);
const char *gpio_get_gpio_fsel_name(unsigned gpio, GPIO_FSEL_T fsel);
const char *gpio_get_fsel_name(GPIO_FSEL_T fsel);
const char *gpio_get_pull_name(GPIO_PULL_T pull);
const char *gpio_get_drive_name(GPIO_DRIVE_T drive);

#endif
