#ifndef BRAINIX_VNC_H
#define BRAINIX_VNC_H

#include <stdint.h>

typedef enum {
    VNC_STATE_DISCONNECTED,
    VNC_STATE_WAIT_VERSION,
    VNC_STATE_SEND_VERSION,
    VNC_STATE_WAIT_SEC_TYPES,
    VNC_STATE_WAIT_CHALLENGE,
    VNC_STATE_WAIT_SEC_RESULT,
    VNC_STATE_SEND_CLIENT_INIT,
    VNC_STATE_WAIT_SERVER_INIT,
    VNC_STATE_RUNNING,
    VNC_STATE_WAIT_RECT_HEADER,
    VNC_STATE_WAIT_RECT_DATA
} vnc_state_t;

typedef struct {
    vnc_state_t state;
    int fd;
    uint32_t width;
    uint32_t height;
    
    uint8_t rx_buf[4096];
    int rx_len;
    
    uint32_t rect_x, rect_y, rect_w, rect_h;
    uint32_t *rect_buf;
    int rect_bytes_read;
} vnc_t;

void vnc_init(void);
void vnc_connect(const uint8_t *ip, uint16_t port);
void vnc_poll(void);
void vnc_draw(int cx, int cy, int cw, int ch);

#endif
