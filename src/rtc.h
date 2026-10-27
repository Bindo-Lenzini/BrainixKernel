// BrainixKernel — rtc.h
#ifndef BRAINIX_RTC_H
#define BRAINIX_RTC_H

#include <stdint.h>

// Заполняет дату и время (Unix epoch + сдвиг в часах).
void rtc_get(int tz_offset_hours,
             int *y, int *mo, int *d,
             int *h, int *mi, int *s);

// Форматирует "HH:MM" (UTC+3).
void rtc_format_hm_utc3(char *buf, int max);

// Форматирует "DD/MM HH:MM" (UTC+3).
void rtc_format_full_utc3(char *buf, int max);

#endif