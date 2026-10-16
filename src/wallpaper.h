// BrainixKernel (BKOS) — wallpaper.h
// Пресеты обоев + кастомные PNG/BMP/JPEG картинки из initrd и медиа-диска.
#ifndef BKOS_WALLPAPER_H
#define BKOS_WALLPAPER_H

#include <stdint.h>

int  wallpaper_count(void);
const char *wallpaper_name(int idx);
const char *wallpaper_current_name(void);

// Цвета градиента пресета (top/bot). Если idx вне диапазона — вернёт
// дефолтные цвета (синий), не падает.
void wallpaper_preset_colors(int idx, uint32_t *out_top, uint32_t *out_bot);

int  wallpaper_set_by_name(const char *name);
void wallpaper_set_by_index(int idx);
const char *wallpaper_last_reason(void);
void wallpaper_draw(void);

#endif