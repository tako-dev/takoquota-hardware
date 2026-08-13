#pragma once

/*
 * Battery voltage measurement on the Waveshare ESP32-S3-Touch-ePaper-1.54.
 *
 * The LiPo feeds a 1/2 divider into ADC1 channel 3 (GPIO4); the ADC reading is
 * scaled back up by 2 and mapped onto a rough LiPo discharge curve to a 0..100
 * percentage. See battery.c for the divider and curve constants.
 */

/* Estimated state of charge, 0..100. Returns -1 when no battery is present. */
int battery_read_percent(void);
