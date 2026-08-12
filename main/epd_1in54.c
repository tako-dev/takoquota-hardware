#include "epd_1in54.h"

#include <string.h>

#include "board.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd";

static spi_device_handle_t s_spi;

static esp_err_t epd_wait_idle(int timeout_ms);

/* SSD1681 command set (subset used here). */
#define CMD_DRIVER_OUTPUT_CTRL   0x01
#define CMD_DEEP_SLEEP           0x10
#define CMD_DATA_ENTRY_MODE      0x11
#define CMD_SW_RESET             0x12
#define CMD_TEMP_SENSOR_CTRL     0x18
#define CMD_MASTER_ACTIVATE      0x20
#define CMD_DISPLAY_UPDATE_CTRL2 0x22
#define CMD_WRITE_RAM_BW         0x24
#define CMD_WRITE_RAM_RED        0x26
#define CMD_WRITE_VCOM           0x2C
#define CMD_WRITE_LUT            0x32
#define CMD_END_OPTION           0x3F
#define CMD_BORDER_WAVEFORM      0x3C
#define CMD_SET_RAM_X_RANGE      0x44
#define CMD_SET_RAM_Y_RANGE      0x45
#define CMD_SET_RAM_X_COUNTER    0x4E
#define CMD_SET_RAM_Y_COUNTER    0x4F

/* Official full-refresh waveform for the V2 board's GDEY0154D67 panel. */
static const uint8_t s_full_lut[159] = {
    0x80, 0x48, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x40, 0x48, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x80, 0x48, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x40, 0x48, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xA, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x8, 0x1,
    0x0, 0x2, 0xA, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0, 0x22, 0x17, 0x41,
    0x0, 0x32, 0x20,
};

/* Send one command byte (D/C low). */
static esp_err_t epd_cmd(uint8_t cmd)
{
    gpio_set_level(EPD_PIN_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    return spi_device_polling_transmit(s_spi, &t);
}

/* Send a data payload (D/C high). */
static esp_err_t epd_data(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }
    gpio_set_level(EPD_PIN_DC, 1);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t epd_data1(uint8_t byte)
{
    return epd_data(&byte, 1);
}

static esp_err_t epd_load_full_lut(void)
{
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_WRITE_LUT), TAG, "write lut");
    ESP_RETURN_ON_ERROR(epd_data(s_full_lut, 153), TAG, "lut waveform");
    ESP_RETURN_ON_ERROR(epd_wait_idle(2000), TAG, "lut busy");

    ESP_RETURN_ON_ERROR(epd_cmd(CMD_END_OPTION), TAG, "end option");
    ESP_RETURN_ON_ERROR(epd_data1(s_full_lut[153]), TAG, "end option val");

    ESP_RETURN_ON_ERROR(epd_cmd(0x03), TAG, "gate voltage");
    ESP_RETURN_ON_ERROR(epd_data1(s_full_lut[154]), TAG, "gate voltage val");

    ESP_RETURN_ON_ERROR(epd_cmd(0x04), TAG, "source voltage");
    ESP_RETURN_ON_ERROR(epd_data(&s_full_lut[155], 3), TAG, "source voltage val");

    ESP_RETURN_ON_ERROR(epd_cmd(CMD_WRITE_VCOM), TAG, "vcom");
    return epd_data1(s_full_lut[158]);
}

static uint8_t reverse_bits(uint8_t byte)
{
    byte = (uint8_t)((byte >> 4) | (byte << 4));
    byte = (uint8_t)(((byte & 0xCC) >> 2) | ((byte & 0x33) << 2));
    return (uint8_t)(((byte & 0xAA) >> 1) | ((byte & 0x55) << 1));
}

/*
 * Wait for the panel to finish an operation. BUSY is high while the controller
 * is working. A full refresh takes well under 2s; anything longer means the
 * panel is not responding and we would otherwise hang forever.
 */
static esp_err_t epd_wait_idle(int timeout_ms)
{
    const int poll_ms = 10;
    int waited = 0;

    while (gpio_get_level(EPD_PIN_BUSY) == 1) {
        if (waited >= timeout_ms) {
            ESP_LOGE(TAG, "BUSY stuck high after %dms", waited);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
        waited += poll_ms;
    }
    return ESP_OK;
}

static void epd_hw_reset(void)
{
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(EPD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

/* Point the controller's RAM window at the full 200x200 panel. */
static esp_err_t epd_set_window_full(void)
{
    /* X is addressed in bytes: 0..24 covers 200 columns. */
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_SET_RAM_X_RANGE), TAG, "x range");
    ESP_RETURN_ON_ERROR(epd_data1(0x00), TAG, "x start");
    ESP_RETURN_ON_ERROR(epd_data1(EPD_ROW_BYTES - 1), TAG, "x end");

    /* The V2 panel scans Y from 199 down to 0. */
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_SET_RAM_Y_RANGE), TAG, "y range");
    ESP_RETURN_ON_ERROR(epd_data1((EPD_HEIGHT - 1) & 0xFF), TAG, "y start lo");
    ESP_RETURN_ON_ERROR(epd_data1(((EPD_HEIGHT - 1) >> 8) & 0x01), TAG, "y start hi");
    ESP_RETURN_ON_ERROR(epd_data1(0x00), TAG, "y end lo");
    ESP_RETURN_ON_ERROR(epd_data1(0x00), TAG, "y end hi");

    return ESP_OK;
}

/* Reset the RAM address counters to the window origin. */
static esp_err_t epd_set_cursor_origin(void)
{
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_SET_RAM_X_COUNTER), TAG, "x cursor");
    ESP_RETURN_ON_ERROR(epd_data1(0x00), TAG, "x cursor val");

    ESP_RETURN_ON_ERROR(epd_cmd(CMD_SET_RAM_Y_COUNTER), TAG, "y cursor");
    ESP_RETURN_ON_ERROR(epd_data1((EPD_HEIGHT - 1) & 0xFF), TAG, "y cursor lo");
    ESP_RETURN_ON_ERROR(epd_data1(((EPD_HEIGHT - 1) >> 8) & 0x01), TAG, "y cursor hi");

    return ESP_OK;
}

/* Configure the control GPIOs and enable the panel's active-low power rail. */
static esp_err_t epd_gpio_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << EPD_PIN_DC) | (1ULL << EPD_PIN_RST) |
                        (1ULL << EPD_PIN_PWR_EN),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&out), TAG, "output pins");

    /*
     * Pull BUSY down so that if the panel is ever absent or unpowered the line
     * reads a stable 0 instead of floating, which would otherwise show up as a
     * spurious "busy forever" timeout.
     */
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << EPD_PIN_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&in), TAG, "busy pin");

    gpio_set_level(EPD_PIN_PWR_EN, 0);
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    return ESP_OK;
}

static esp_err_t epd_spi_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = EPD_PIN_MOSI,
        .miso_io_num = -1,          /* panel is write-only */
        .sclk_io_num = EPD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_BUF_SIZE + 8,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(EPD_SPI_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    spi_device_interface_config_t dev = {
        .clock_speed_hz = EPD_SPI_CLOCK_HZ,
        .mode = 0,                  /* SSD1681 samples on rising edge */
        .spics_io_num = EPD_PIN_CS,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(EPD_SPI_HOST, &dev, &s_spi),
                        TAG, "spi device");

    return ESP_OK;
}

esp_err_t epd_init(void)
{
    if (s_spi == NULL) {
        ESP_RETURN_ON_ERROR(epd_gpio_init(), TAG, "gpio init");
        ESP_RETURN_ON_ERROR(epd_spi_init(), TAG, "spi init");
    } else {
        gpio_set_level(EPD_PIN_PWR_EN, 0);
        gpio_set_level(EPD_PIN_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    epd_hw_reset();
    ESP_RETURN_ON_ERROR(epd_wait_idle(2000), TAG, "post-reset busy");

    /*
     * A software reset after the hardware one puts the controller in a known
     * state regardless of how the last session ended.
     */
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_SW_RESET), TAG, "sw reset");
    ESP_RETURN_ON_ERROR(epd_wait_idle(2000), TAG, "sw reset busy");

    /* 200 gate lines, scan top-to-bottom. */
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_DRIVER_OUTPUT_CTRL), TAG, "driver ctrl");
    ESP_RETURN_ON_ERROR(epd_data1((EPD_HEIGHT - 1) & 0xFF), TAG, "mux lo");
    ESP_RETURN_ON_ERROR(epd_data1(((EPD_HEIGHT - 1) >> 8) & 0x01), TAG, "mux hi");
    ESP_RETURN_ON_ERROR(epd_data1(0x01), TAG, "gate scan");

    /* X increments within a row; Y decrements after each row. */
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_DATA_ENTRY_MODE), TAG, "entry mode");
    ESP_RETURN_ON_ERROR(epd_data1(0x01), TAG, "entry mode val");

    ESP_RETURN_ON_ERROR(epd_set_window_full(), TAG, "window");

    /* Match the V2 board's official full-refresh border setting. */
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_BORDER_WAVEFORM), TAG, "border");
    ESP_RETURN_ON_ERROR(epd_data1(0x01), TAG, "border val");

    /* Use the controller's built-in temperature sensor to pick the waveform. */
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_TEMP_SENSOR_CTRL), TAG, "temp sensor");
    ESP_RETURN_ON_ERROR(epd_data1(0x80), TAG, "temp sensor val");

    /* Load temperature-dependent analog settings before installing the LUT. */
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_DISPLAY_UPDATE_CTRL2), TAG, "load waveform ctrl2");
    ESP_RETURN_ON_ERROR(epd_data1(0xB1), TAG, "load waveform mode");
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_MASTER_ACTIVATE), TAG, "load waveform");

    ESP_RETURN_ON_ERROR(epd_set_cursor_origin(), TAG, "cursor");
    ESP_RETURN_ON_ERROR(epd_wait_idle(6000), TAG, "init busy");
    ESP_RETURN_ON_ERROR(epd_load_full_lut(), TAG, "full lut");

    ESP_LOGI(TAG, "panel initialised (%dx%d)", EPD_WIDTH, EPD_HEIGHT);
    return ESP_OK;
}

/* Kick off a full refresh and block until the panel settles. */
static esp_err_t epd_refresh(void)
{
    ESP_RETURN_ON_ERROR(epd_cmd(CMD_DISPLAY_UPDATE_CTRL2), TAG, "update ctrl2");
    ESP_RETURN_ON_ERROR(epd_data1(0xC7), TAG, "update mode");  /* full update */

    ESP_RETURN_ON_ERROR(epd_cmd(CMD_MASTER_ACTIVATE), TAG, "activate");

    /* A full refresh on this panel takes roughly 1s; allow generous margin. */
    return epd_wait_idle(6000);
}

esp_err_t epd_display(const uint8_t *buf)
{
    ESP_RETURN_ON_FALSE(s_spi != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(buf != NULL, ESP_ERR_INVALID_ARG, TAG, "null buffer");

    ESP_RETURN_ON_ERROR(epd_set_window_full(), TAG, "window");
    ESP_RETURN_ON_ERROR(epd_set_cursor_origin(), TAG, "cursor");

    ESP_RETURN_ON_ERROR(epd_cmd(CMD_WRITE_RAM_BW), TAG, "write ram");
    for (int y = EPD_HEIGHT - 1; y >= 0; y--) {
        uint8_t row[EPD_ROW_BYTES];
        const uint8_t *src = &buf[y * EPD_ROW_BYTES];
        for (int x = 0; x < EPD_ROW_BYTES; x++) {
            row[x] = reverse_bits(src[EPD_ROW_BYTES - 1 - x]);
        }
        ESP_RETURN_ON_ERROR(epd_data(row, sizeof(row)), TAG, "frame row");
    }

    return epd_refresh();
}

esp_err_t epd_clear(void)
{
    ESP_RETURN_ON_FALSE(s_spi != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");

    ESP_RETURN_ON_ERROR(epd_set_window_full(), TAG, "window");
    ESP_RETURN_ON_ERROR(epd_set_cursor_origin(), TAG, "cursor");

    /*
     * Stream the all-white frame a row at a time so we do not need a second
     * 5KB buffer just to blank the screen.
     */
    uint8_t row[EPD_ROW_BYTES];
    memset(row, 0xFF, sizeof(row));

    ESP_RETURN_ON_ERROR(epd_cmd(CMD_WRITE_RAM_BW), TAG, "write ram");
    for (int y = 0; y < EPD_HEIGHT; y++) {
        ESP_RETURN_ON_ERROR(epd_data(row, sizeof(row)), TAG, "blank row");
    }

    return epd_refresh();
}

esp_err_t epd_sleep(void)
{
    ESP_RETURN_ON_FALSE(s_spi != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialised");

    ESP_RETURN_ON_ERROR(epd_cmd(CMD_DEEP_SLEEP), TAG, "deep sleep");
    ESP_RETURN_ON_ERROR(epd_data1(0x01), TAG, "sleep mode");
    vTaskDelay(pdMS_TO_TICKS(100));

    epd_power_off();

    /* The image persists after the active-low panel power rail is switched off. */
    ESP_LOGI(TAG, "panel asleep and powered off");
    return ESP_OK;
}

void epd_power_off(void)
{
    gpio_config_t power = {
        .pin_bit_mask = 1ULL << EPD_PIN_PWR_EN,
        .mode = GPIO_MODE_OUTPUT,
    };
    if (gpio_config(&power) == ESP_OK) {
        gpio_set_level(EPD_PIN_PWR_EN, 1);
    }
}
