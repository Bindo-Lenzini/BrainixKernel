// BrainixKernel (BKOS) — desktop.h
#ifndef BKOS_DESKTOP_H
#define BKOS_DESKTOP_H

#define DI_COMPUTER     0
#define DI_RECYCLE      1
#define DI_TERMINAL     2
#define DI_FILES        3
#define DI_EDITOR       4
#define DI_PERSONALIZE  5
#define DI_COUNT        6

int  desktop_hit(int x, int y);

void desktop_select(int idx);
int  desktop_selected(void);
void desktop_clear_selection(void);

const char *desktop_label(int idx);

void desktop_draw(void);

#endif