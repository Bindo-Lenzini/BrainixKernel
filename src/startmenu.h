// BrainixKernel — startmenu.h
#ifndef BRAINIX_STARTMENU_H
#define BRAINIX_STARTMENU_H

#include <stdint.h>

#define STARTMENU_ITEM_SHUTDOWN 0
#define STARTMENU_ITEM_REBOOT   1
#define STARTMENU_ITEM_SLEEP    2
#define STARTMENU_ITEM_HIBER    3
#define STARTMENU_ITEM_COUNT    4

void startmenu_toggle(void);
void startmenu_close(void);
int  startmenu_is_open(void);

int  startmenu_hit(int x, int y);

// mx, my — координаты мыши для hover.
void startmenu_draw(int mx, int my);

void system_shutdown(void);
void system_reboot(void);

#endif