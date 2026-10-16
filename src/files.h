// BrainixKernel (BKOS) — files.h
// Приложение "Files": список файлов BKFS со скроллбаром.
#ifndef BKOS_FILES_H
#define BKOS_FILES_H

#include <stdint.h>

#define FILES_MAX_ENTRIES 64
#define FILES_NAME_MAX    64

typedef struct {
    char     names[FILES_MAX_ENTRIES][FILES_NAME_MAX];
    uint32_t sizes[FILES_MAX_ENTRIES];
    int      count;
    int      selected;
    int      status_msg;
    int      cursor_visible;
} files_t;

void files_init(files_t *f);
void files_refresh(files_t *f);
void files_draw_content(files_t *f, int cx, int cy, int cw, int ch);
void files_key(files_t *f, char c);

// Клик по content-области. Возвращает 1, если клик обработан.
int  files_click(files_t *f, int mx, int my,
                 int cx, int cy, int cw, int ch);

#endif