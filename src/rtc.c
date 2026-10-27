// BrainixKernel — rtc.c
// PL031 RTC. Доступ через KVA (TTBR1).

#include <stdint.h>
#include "rtc.h"
#include "mmu.h"

#define PL031_BASE  (0x09010000ULL + KERNEL_VA_OFFSET)
#define PL031_DR    ((volatile uint32_t *)(PL031_BASE + 0x00))

// Из алгоритма Howard Hinnant (public domain).
static void civil_from_days(int64_t z, int *y, int *m, int *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint64_t doe = (uint64_t)(z - era * 146097);
    uint64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    int64_t  yy  = (int64_t)yoe + era * 400;
    uint64_t doy = doe - (365*yoe + yoe/4 - yoe/100);
    uint64_t mp  = (5*doy + 2) / 153;
    uint64_t dd  = doy - (153*mp+2)/5 + 1;
    uint64_t mm  = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yy + (mm <= 2));
    *m = (int)mm;
    *d = (int)dd;
}

void rtc_get(int tz_hours,
             int *y, int *mo, int *d,
             int *h, int *mi, int *s)
{
    uint32_t base = *PL031_DR;
    int64_t t = (int64_t)base + (int64_t)tz_hours * 3600;

    int64_t days = t / 86400;
    int secs     = (int)(t % 86400);
    if (secs < 0) { secs += 86400; days -= 1; }

    int hh = secs / 3600;
    int mm = (secs % 3600) / 60;
    int ss = secs % 60;

    int yy, mo_, dd;
    civil_from_days(days, &yy, &mo_, &dd);

    *y = yy; *mo = mo_; *d = dd;
    *h = hh; *mi = mm; *s = ss;
}

static void put2(char *buf, int *i, int v) {
    buf[(*i)++] = (char)('0' + (v / 10) % 10);
    buf[(*i)++] = (char)('0' + (v % 10));
}

void rtc_format_hm_utc3(char *buf, int max) {
    int y, mo, d, h, mi, s;
    rtc_get(3, &y, &mo, &d, &h, &mi, &s);
    int i = 0;
    put2(buf, &i, h);
    buf[i++] = ':';
    put2(buf, &i, mi);
    buf[i] = 0;
    (void)max;
}

void rtc_format_full_utc3(char *buf, int max) {
    int y, mo, d, h, mi, s;
    rtc_get(3, &y, &mo, &d, &h, &mi, &s);
    int i = 0;
    put2(buf, &i, d);
    buf[i++] = '/';
    put2(buf, &i, mo);
    buf[i++] = ' ';
    put2(buf, &i, h);
    buf[i++] = ':';
    put2(buf, &i, mi);
    buf[i] = 0;
    (void)max;
}