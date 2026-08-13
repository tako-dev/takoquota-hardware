#include "serial_config.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SERIAL_LINE_MAX 128
#define SERIAL_POLL_MS  200

static volatile bool s_abort;

void serial_config_abort(void)
{
    s_abort = true;
}

/*
 * Read one line over USB-Serial-JTAG, echoing printable characters and
 * handling backspace. Returns the byte count (excluding the terminator), or
 * -1 when aborted.
 */
static int read_line(char *buf, size_t cap)
{
    size_t n = 0;
    while (n + 1 < cap) {
        uint8_t c;
        int got = usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(SERIAL_POLL_MS));
        if (s_abort) {
            return -1;
        }
        if (got <= 0) {
            continue;
        }
        if (c == '\n' || c == '\r') {
            printf("\r\n");
            break;
        }
        if (c == 0x08 || c == 0x7F) {  /* backspace / delete */
            if (n > 0) {
                n--;
                printf("\b \b");
            }
            continue;
        }
        if (c < 0x20) {
            continue;  /* skip other control characters */
        }
        buf[n++] = (char)c;
        usb_serial_jtag_write_bytes(&c, 1, pdMS_TO_TICKS(100));
    }
    buf[n] = '\0';
    return (int)n;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return s;
}

static void print_help(void)
{
    printf("Commands:\r\n"
           "  help              show this help\r\n"
           "  show              show pending config\r\n"
           "  ssid <text>       set Wi-Fi SSID\r\n"
           "  pass <text>       set Wi-Fi password\r\n"
           "  apikey <text>     set Tako API key\r\n"
           "  interval <min>    set refresh minutes (%d..%d)\r\n"
           "  save              validate and save\r\n",
           DEVICE_REFRESH_MINUTES_MIN, DEVICE_REFRESH_MINUTES_MAX);
}

static void print_masked(const char *label, const char *value)
{
    printf("%s: ", label);
    if (value[0] == '\0') {
        printf("(unset)\r\n");
    } else {
        printf("%.2s****\r\n", value);
    }
}

static void show_config(const device_config_t *config)
{
    printf("ssid:     %s\r\n", config->wifi_ssid[0] ? config->wifi_ssid : "(unset)");
    print_masked("password", config->wifi_password);
    print_masked("apikey  ", config->tako_api_key);
    printf("interval: %" PRIu32 " min\r\n", config->refresh_minutes);
}

static bool parse_interval(const char *arg, uint32_t *out)
{
    if (arg[0] == '\0') {
        return false;
    }
    for (const char *p = arg; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }
    unsigned long value = strtoul(arg, NULL, 10);
    if (value < DEVICE_REFRESH_MINUTES_MIN || value > DEVICE_REFRESH_MINUTES_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

/* Set a string field, rejecting values that do not fit the destination. */
static bool set_field(char *dest, size_t cap, const char *arg)
{
    if (arg[0] == '\0' || strlen(arg) >= cap) {
        return false;
    }
    strcpy(dest, arg);
    return true;
}

esp_err_t serial_config_run(device_config_t *config, uint32_t timeout_ms)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_abort = false;

    /* The console already installs this driver; ignore a second-install error. */
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&usb);

    print_help();
    printf("> ");

    TickType_t start = xTaskGetTickCount();
    char line[SERIAL_LINE_MAX];
    while (!s_abort) {
        int n = read_line(line, sizeof(line));
        if (n < 0) {
            break;
        }

        char *cmd = line;
        char *arg = cmd;
        while (*arg != '\0' && !isspace((unsigned char)*arg)) {
            arg++;
        }
        if (*arg != '\0') {
            *arg++ = '\0';
        }
        arg = trim(arg);
        for (char *p = cmd; *p != '\0'; p++) {
            *p = (char)tolower((unsigned char)*p);
        }

        bool saved = false;
        if (cmd[0] == '\0') {
            /* empty line */
        } else if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "show") == 0) {
            show_config(config);
        } else if (strcmp(cmd, "ssid") == 0) {
            if (!set_field(config->wifi_ssid, sizeof(config->wifi_ssid), arg)) {
                printf("error: ssid required, max %d chars\r\n",
                       (int)sizeof(config->wifi_ssid) - 1);
            } else {
                printf("ssid set\r\n");
            }
        } else if (strcmp(cmd, "pass") == 0) {
            if (strlen(arg) >= sizeof(config->wifi_password)) {
                printf("error: password too long\r\n");
            } else {
                strcpy(config->wifi_password, arg);
                printf("password set\r\n");
            }
        } else if (strcmp(cmd, "apikey") == 0) {
            if (!set_field(config->tako_api_key, sizeof(config->tako_api_key), arg)) {
                printf("error: apikey required, max %d chars\r\n",
                       (int)sizeof(config->tako_api_key) - 1);
            } else {
                config->tako_api_id[0] = '\0';  /* stale user id after key change */
                printf("apikey set\r\n");
            }
        } else if (strcmp(cmd, "interval") == 0) {
            uint32_t value;
            if (!parse_interval(arg, &value)) {
                printf("error: interval must be %d..%d minutes\r\n",
                       DEVICE_REFRESH_MINUTES_MIN, DEVICE_REFRESH_MINUTES_MAX);
            } else {
                config->refresh_minutes = value;
                printf("interval set\r\n");
            }
        } else if (strcmp(cmd, "save") == 0) {
            if (!device_config_is_valid(config)) {
                printf("error: missing field (need ssid + apikey)\r\n");
            } else if (device_config_save(config) != ESP_OK) {
                printf("error: save failed\r\n");
            } else {
                printf("saved\r\n");
                saved = true;
            }
        } else {
            printf("unknown command: %s (try 'help')\r\n", cmd);
        }

        if (saved) {
            return ESP_OK;
        }
        if (timeout_ms != 0 &&
            xTaskGetTickCount() - start >= pdMS_TO_TICKS(timeout_ms)) {
            return ESP_ERR_TIMEOUT;
        }
        printf("> ");
    }
    return ESP_ERR_TIMEOUT;
}
