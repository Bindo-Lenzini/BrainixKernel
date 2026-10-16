// BrainixKernel (BKOS) — media.h
// Медиа-диск: второй virtio-blk, на который пользователь может скидывать
// картинки с macOS-хоста. Образ собирается scripts/mkmedia.py.
//
// Формат MEDI (внутри образа):
//   сектор 0          : суперблок (magic, version, num_files, data_start)
//   сектора 1..15     : таблица файлов (по 64 байта, до 120 записей)
//   сектор 16 и далее : данные файлов
#ifndef BKOS_MEDIA_H
#define BKOS_MEDIA_H

#include <stdint.h>

#define MEDIA_MAX_NAME  48
#define MEDIA_MAX_FILES 120

// Возвращает 1 если медиа-диск есть и валиден. Lazy-init.
int media_is_ready(void);

// Кол-во файлов (0 если диск не готов).
int media_count(void);

// Перебор: idx=0..count-1, возвращает 1 если запись есть.
int media_list(int idx, char *name_out, uint32_t name_max, uint32_t *size_out);

// Размер файла по имени, -1 если нет.
int media_size(const char *name);

// Читает файл. Возвращает число прочитанных байт или -1.
int media_read(const char *name, void *buf, uint32_t max);

#endif