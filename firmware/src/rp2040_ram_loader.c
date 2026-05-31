#include "rp2040_ram_loader.h"
#include "config.h"

#include <hardware/i2c.h>
#include <pico/stdlib.h>
#include <string.h>

extern const uint8_t flexgpio_ram_bin[];
extern const uint32_t flexgpio_ram_bin_len;

#define PICOBOOT3_ADDR         0x48
#define PICOBOOT3_ACTIVATE     0xA5
#define PICOBOOT3_LOAD_RAM     0x45
#define PICOBOOT3_EXEC_RAM     0x46
#define CHUNK_SIZE             4096
#define ACTIVATE_RETRIES       5
#define ACTIVATE_RETRY_MS      10

static const uint8_t activate_response[4] = {0x70, 0x62, 0x74, 0x33};

static void rp2040_i2c_init(void)
{
    i2c_init(i2c1, 1000 * 1000);
    gpio_set_function(FLEXGPIO_SDA, GPIO_FUNC_I2C);
    gpio_set_function(FLEXGPIO_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(FLEXGPIO_SDA);
    gpio_pull_up(FLEXGPIO_SCL);
}

static void rp2040_send_magic(void)
{
    printf("  sending magic...\n");
    uint8_t magic[8];
    memset(magic, 0xFF, 8);
    i2c_write_blocking(i2c1, PICOBOOT3_ADDR, magic, 8, false);
    printf("  magic sent\n");
}

static int rp2040_activate(void)
{
    printf("  activate: write\n");
    uint8_t cmd = PICOBOOT3_ACTIVATE;
    if (i2c_write_blocking(i2c1, PICOBOOT3_ADDR, &cmd, 1, false) != 1) {
        printf("  activate: write NACK\n");
        return -1;
    }

    printf("  activate: read\n");
    sleep_us(10);

    uint8_t resp[4];
    if (i2c_read_blocking(i2c1, PICOBOOT3_ADDR, resp, 4, false) != 4) {
        printf("  activate: read failed\n");
        return -1;
    }

    int ok = (memcmp(resp, activate_response, 4) == 0);
    printf("  activate: %s\n", ok ? "OK" : "bad response");
    return ok ? 0 : -1;
}

static int rp2040_ram_load_chunk(uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint8_t buf[7 + CHUNK_SIZE];
    buf[0] = PICOBOOT3_LOAD_RAM;
    buf[1] = addr & 0xFF;
    buf[2] = (addr >> 8) & 0xFF;
    buf[3] = (addr >> 16) & 0xFF;
    buf[4] = (addr >> 24) & 0xFF;
    buf[5] = len & 0xFF;
    buf[6] = (len >> 8) & 0xFF;
    memcpy(buf + 7, data, len);

    if (i2c_write_blocking(i2c1, PICOBOOT3_ADDR, buf, 7 + len, false) != 7 + len)
        return -1;

    sleep_us(10);

    uint8_t ready;
    if (i2c_read_blocking(i2c1, PICOBOOT3_ADDR, &ready, 1, false) != 1)
        return -1;

    return (ready == 1) ? 0 : -1;
}

static int rp2040_ram_execute(uint32_t entry)
{
    uint8_t buf[5];
    buf[0] = PICOBOOT3_EXEC_RAM;
    buf[1] = entry & 0xFF;
    buf[2] = (entry >> 8) & 0xFF;
    buf[3] = (entry >> 16) & 0xFF;
    buf[4] = (entry >> 24) & 0xFF;

    return (i2c_write_blocking(i2c1, PICOBOOT3_ADDR, buf, 5, false) == 5) ? 0 : -1;
}

static int rp2040_load_and_run(const uint8_t *data, uint32_t len)
{
    uint32_t loaded = 0;

    while (loaded < len) {
        uint16_t chunk = (len - loaded > CHUNK_SIZE) ? CHUNK_SIZE : (len - loaded);
        printf("  load chunk %d @ 0x%08x\n", chunk, 0x20002000 + loaded);
        if (rp2040_ram_load_chunk(0x20002000 + loaded, data + loaded, chunk) != 0)
            return -1;
        loaded += chunk;
    }

    printf("  exec ram @ 0x20002000\n");
    if (rp2040_ram_execute(0x20002000) != 0)
        return -1;

    sleep_ms(50);

    uint8_t test = 0;
    int ret = i2c_write_blocking(i2c1, PICOBOOT3_ADDR, &test, 1, true);
    printf("  post-exec i2c test: %s\n", ret < 0 ? "NACK (no FlexGPIO)" : "ACK (FlexGPIO alive)");
    return 0;
}

static int rp2040_enter_bootloader(bool try_activate_first)
{
    if (try_activate_first && rp2040_activate() == 0)
        return 0;

    rp2040_send_magic();

    for (int i = 0; i < ACTIVATE_RETRIES; i++) {
        if (rp2040_activate() == 0)
            return 0;
        sleep_ms(ACTIVATE_RETRY_MS);
    }

    return -1;
}

int rp2040_boot_flexgpio(void)
{
    printf("rp2040_boot: entering\n");
    rp2040_i2c_init();
    printf("rp2040_boot: i2c init done\n");

    printf("rp2040_boot: contacting picoboot3...\n");
    if (rp2040_enter_bootloader(true) != 0) {
        printf("rp2040_boot: enter failed\n");
        return -1;
    }
    printf("rp2040_boot: picoboot3 ready, loading...\n");

    int ret = rp2040_load_and_run(flexgpio_ram_bin, flexgpio_ram_bin_len);
    printf("rp2040_boot: done (%d)\n", ret);
    return ret;
}

int rp2040_reload_flexgpio(void)
{
    rp2040_i2c_init();
    if (rp2040_enter_bootloader(false) != 0)
        return -1;
    return rp2040_load_and_run(flexgpio_ram_bin, flexgpio_ram_bin_len);
}
