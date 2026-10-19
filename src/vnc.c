#include "vnc.h"
#include "wm.h"
#include "fb.h"
#include "tcp.h"
#include "heap.h"
#include "net.h"
extern void uart_puts(const char *s);
extern void uart_putc(char c);

extern void uart_puthex(uint32_t v);


// Простейший memset
static void vnc_memset(void *s, int c, unsigned long n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
}


static void vnc_print_hex(const char *label, const uint8_t *buf, int len) {
    uart_puts(label);
    char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < len; i++) {
        uart_putc(hex[buf[i] >> 4]);
        uart_putc(hex[buf[i] & 0x0F]);
        uart_putc(' ');
    }
    uart_puts("\n");
}

static vnc_t g_vnc;
static const char *VNC_PASSWORD = "9178"; // ПАРОЛЬ ОТ MAC!

/* ================= DES АЛГОРИТМ (FIXED) ================= */
typedef unsigned char des_byte;
typedef uint32_t des_u32;

static const int des_IP[64] = {58,50,42,34,26,18,10,2,60,52,44,36,28,20,12,4,62,54,46,38,30,22,14,6,64,56,48,40,32,24,16,8,57,49,41,33,25,17,9,1,59,51,43,35,27,19,11,3,61,53,45,37,29,21,13,5,63,55,47,39,31,23,15,7};
static const int des_FP[64] = {40,8,48,16,56,24,64,32,39,7,47,15,55,23,63,31,38,6,46,14,54,22,62,30,37,5,45,13,53,21,61,29,36,4,44,12,52,20,60,28,35,3,43,11,51,19,59,27,34,2,42,10,50,18,58,26,33,1,41,9,49,17,57,25};
static const int des_PC1[56] = {57,49,41,33,25,17,9,1,58,50,42,34,26,18,10,2,59,51,43,35,27,19,11,3,60,52,44,36,63,55,47,39,31,23,15,7,62,54,46,38,30,22,14,6,61,53,45,37,29,21,13,5,28,20,12,4};
static const int des_PC2[48] = {14,17,11,24,1,5,3,28,15,6,21,10,23,19,12,4,26,8,16,7,27,20,13,2,41,52,31,37,47,55,30,40,51,45,33,48,44,49,39,56,34,53,46,42,50,36,29,32};
static const int des_E[48] = {32,1,2,3,4,5,4,5,6,7,8,9,8,9,10,11,12,13,12,13,14,15,16,17,16,17,18,19,20,21,20,21,22,23,24,25,24,25,26,27,28,29,28,29,30,31,32,1};
static const int des_P[32] = {16,7,20,21,29,12,28,17,1,15,23,26,5,18,31,10,2,8,24,14,32,27,3,9,19,13,30,6,22,11,4,25};
static const des_byte des_S[8][64] = {
    {14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7,0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0,15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
    {15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10,3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15,13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
    {10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8,13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7,1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
    {7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15,13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4,3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
    {2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9,14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14,11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
    {12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11,10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6,4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
    {4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1,13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2,6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
    {13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7,1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8,2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11}
};
static const int des_shifts[16] = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};

static uint64_t des_permute(uint64_t in, const int* table, int n, int in_bits) {
    uint64_t out = 0;
    for (int i = 0; i < n; i++) {
        int bit = table[i] - 1;
        if (in & (1ULL << (in_bits - 1 - bit))) {
            out |= (1ULL << (n - 1 - i));
        }
    }
    return out;
}

static void des_key_setup(const des_byte* key, uint64_t* ks) {
    uint64_t k = 0;
    for (int i = 0; i < 8; i++) k = (k << 8) | key[i];
    
    uint64_t k56 = des_permute(k, des_PC1, 56, 64);
    uint32_t c = (k56 >> 28) & 0x0FFFFFFF;
    uint32_t d = k56 & 0x0FFFFFFF;
    
    for (int i = 0; i < 16; i++) {
        c = ((c << des_shifts[i]) | (c >> (28 - des_shifts[i]))) & 0x0FFFFFFF;
        d = ((d << des_shifts[i]) | (d >> (28 - des_shifts[i]))) & 0x0FFFFFFF;
        uint64_t cd = ((uint64_t)c << 28) | d;
        ks[i] = des_permute(cd, des_PC2, 48, 56);
    }
}

static void des_crypt_block(const uint64_t* ks, const des_byte* in, des_byte* out) {
    uint64_t block = 0;
    for (int i = 0; i < 8; i++) block = (block << 8) | in[i];
    
    uint64_t ip = des_permute(block, des_IP, 64, 64);
    uint32_t l = (ip >> 32) & 0xFFFFFFFF;
    uint32_t r = ip & 0xFFFFFFFF;
    
    for (int i = 0; i < 16; i++) {
        uint64_t exp = des_permute(r, des_E, 48, 32);
        exp ^= ks[i];
        
        uint32_t s_out = 0;
        for (int j = 0; j < 8; j++) {
            int b1 = (exp >> (47 - j*6)) & 1;
            int b6 = (exp >> (42 - j*6)) & 1;
            int row = (b1 << 1) | b6;
            
            int b2 = (exp >> (46 - j*6)) & 1;
            int b3 = (exp >> (45 - j*6)) & 1;
            int b4 = (exp >> (44 - j*6)) & 1;
            int b5 = (exp >> (43 - j*6)) & 1;
            int col = (b2 << 3) | (b3 << 2) | (b4 << 1) | b5;
            
            s_out |= (uint32_t)(des_S[j][row * 16 + col]) << (28 - j * 4);
        }
        
        uint32_t p_out = (uint32_t)des_permute(s_out, des_P, 32, 32);
        
        uint32_t temp = l ^ p_out;
        l = r;
        r = temp;
    }
    
    uint64_t preout = ((uint64_t)r << 32) | l;
    uint64_t fp = des_permute(preout, des_FP, 64, 64);
    
    for (int i = 0; i < 8; i++) {
        out[7 - i] = (fp >> (i * 8)) & 0xFF;
    }
}

static void vnc_des_encrypt(const char *password, des_byte *challenge, des_byte *response) {
    des_byte key[8] = {0};
    int len = 0;
    while (password[len] && len < 8) { key[len] = password[len]; len++; }
    
    for (int i = 0; i < 8; i++) {
        des_byte k = key[i];
        key[i] = ((k & 0x01) << 7) | ((k & 0x02) << 5) | ((k & 0x04) << 3) | ((k & 0x08) << 1) |
                 ((k & 0x10) >> 1) | ((k & 0x20) >> 3) | ((k & 0x40) >> 5) | ((k & 0x80) >> 7);
    }
    
    uint64_t ks[16];
    des_key_setup(key, ks);
    des_crypt_block(ks, challenge, response);
    des_crypt_block(ks, challenge + 8, response + 8);
}
/* ========================================================= */

void vnc_init(void) {
    vnc_memset(&g_vnc, 0, sizeof(g_vnc));
    g_vnc.state = VNC_STATE_DISCONNECTED;
    g_vnc.fd = -1;
}

void vnc_connect(const uint8_t *ip, uint16_t port) {
    uart_puts("[vnc] vnc_connect() started\n");
    int idx = tcp_alloc();
    if (idx < 0) { uart_puts("[vnc] ERROR: tcp_alloc FAILED\n"); return; }
    
    tcp_conn_t *c = tcp_get(idx);
    if (!c) { uart_puts("[vnc] ERROR: tcp_get FAILED\n"); return; }

    ipv4_t my_ip = net_get_ip();
    c->local_ip[0] = my_ip.b[0]; c->local_ip[1] = my_ip.b[1];
    c->local_ip[2] = my_ip.b[2]; c->local_ip[3] = my_ip.b[3];
    c->local_port = 40010 + idx;

    c->remote_ip[0] = ip[0]; c->remote_ip[1] = ip[1];
    c->remote_ip[2] = ip[2]; c->remote_ip[3] = ip[3];
    c->remote_port = port;

    g_vnc.fd = idx;
    g_vnc.state = VNC_STATE_WAIT_VERSION;

    uart_puts("[vnc] Calling tcp_send_syn()...\n");
    int res = tcp_send_syn(c);
    uart_puts("[vnc] tcp_send_syn returned: ");
    uart_puthex(res);
    uart_puts("\n");
}

static void vnc_send(int fd, const void *data, int len) {
    tcp_conn_t *c = tcp_get(fd);
    if (c) tcp_send_data(c, data, len);
}

static int vnc_recv(int fd, void *buf, int max_len) {
    tcp_conn_t *c = tcp_get(fd);
    if (!c || c->rx_len == 0) return 0;
    int len = c->rx_len > (uint32_t)max_len ? max_len : (int)c->rx_len;
    uint8_t *dst = (uint8_t *)buf;
    for (int i = 0; i < len; i++) dst[i] = c->rx_buf[i];
    for (uint32_t i = len; i < c->rx_len; i++) c->rx_buf[i - len] = c->rx_buf[i];
    c->rx_len -= len;
    return len;
}

void vnc_poll(void) {
    if (g_vnc.state == VNC_STATE_DISCONNECTED) return;
    static int poll_cnt = 0;
    if (poll_cnt++ % 1000 == 0) {
        uart_puts("[vnc_poll] state=");
        uart_puthex(g_vnc.state);
        uart_puts("\n");
    }

    tcp_conn_t *c = tcp_get(g_vnc.fd);
    if (!c || c->state == TCP_CLOSED) {
        g_vnc.state = VNC_STATE_DISCONNECTED;
        uart_puts("[vnc] Disconnected\n");
        return;
    }

    if (g_vnc.state == VNC_STATE_WAIT_VERSION) {
        if (c->rx_len >= 12) {
            char ver[13];
            vnc_recv(g_vnc.fd, ver, 12);
            vnc_send(g_vnc.fd, "RFB 003.008\n", 12);
            g_vnc.state = VNC_STATE_WAIT_SEC_TYPES;
        }
    }
    else if (g_vnc.state == VNC_STATE_WAIT_SEC_TYPES) {
        if (c->rx_len >= 2) {
            uint8_t num_types;
            vnc_recv(g_vnc.fd, &num_types, 1);
            uint8_t types[16];
            vnc_recv(g_vnc.fd, types, num_types);
            
            int selected = 0;
            uart_puts("[vnc] Server offered security types: ");
            for (int i = 0; i < num_types; i++) {
                uart_puthex(types[i]);
                uart_puts(" ");
                if (types[i] == 2) selected = 2; // Приоритет VNC Auth
                if (types[i] == 1 && selected != 2) selected = 1;
            }
            uart_puts("\n");
            
            vnc_send(g_vnc.fd, &selected, 1);
            uart_puts("[vnc] Sent type, waiting for challenge...\n");
            
            if (selected == 1) {
                g_vnc.state = VNC_STATE_SEND_CLIENT_INIT;
            } else if (selected == 2) {
                g_vnc.state = VNC_STATE_WAIT_CHALLENGE;
            } else {
                uart_puts("[vnc] No supported security type\n");
                g_vnc.state = VNC_STATE_DISCONNECTED;
            }
        }
    }
    else if (g_vnc.state == VNC_STATE_WAIT_CHALLENGE) {
        if (c->rx_len > 0) {
            uart_puts("[vnc] Receiving challenge... rx_len=");
            uart_puthex(c->rx_len);
            uart_puts("\n");
        }
        if (c->rx_len >= 16) {
            uint8_t challenge[16];
            uint8_t response[16];
            vnc_recv(g_vnc.fd, challenge, 16);
            vnc_print_hex("[vnc] Challenge: ", challenge, 16);
            uart_puts("[vnc] Encrypting challenge...\n");
            vnc_des_encrypt(VNC_PASSWORD, challenge, response);
            vnc_print_hex("[vnc] Response:  ", response, 16);
            vnc_send(g_vnc.fd, response, 16);
            uart_puts("[vnc] Sent response, waiting for result...\n");
            g_vnc.state = VNC_STATE_WAIT_SEC_RESULT;
        }
    }
    else if (g_vnc.state == VNC_STATE_WAIT_SEC_RESULT) {
        if (c->rx_len >= 4) {
            uint32_t result;
            vnc_recv(g_vnc.fd, &result, 4);
            if (result == 0) {
                uart_puts("[vnc] Auth OK!\n");
                g_vnc.state = VNC_STATE_SEND_CLIENT_INIT;
            } else {
                uart_puts("[vnc] Auth FAILED\n");
                g_vnc.state = VNC_STATE_DISCONNECTED;
            }
        }
    }
    else if (g_vnc.state == VNC_STATE_SEND_CLIENT_INIT) {
        uint8_t shared = 1;
        vnc_send(g_vnc.fd, &shared, 1);
        g_vnc.state = VNC_STATE_WAIT_SERVER_INIT;
    }
    else if (g_vnc.state == VNC_STATE_WAIT_SERVER_INIT) {
        if (c->rx_len >= 24) {
            uint8_t init[24];
            vnc_recv(g_vnc.fd, init, 24);
            g_vnc.width  = (init[0] << 8) | init[1];
            g_vnc.height = (init[2] << 8) | init[3];
            uart_puts("[vnc] Screen size: ");
            uart_puts("[vnc] Got ServerInit!"); uart_puts("\n");
            
            uint8_t set_pf[20] = { 0,0,0,0, 32,24,0,1, 0,255,0,255,0,255, 16,8,0 };
            vnc_send(g_vnc.fd, set_pf, 20);

            uint8_t set_enc[8] = { 2, 0, 0, 0, 0, 0, 0, 0 };
            vnc_send(g_vnc.fd, set_enc, 8);

            uint8_t fbur[10] = { 3, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
            fbur[6] = (g_vnc.width >> 8);  fbur[7] = (g_vnc.width & 0xFF);
            fbur[8] = (g_vnc.height >> 8); fbur[9] = (g_vnc.height & 0xFF);
            vnc_send(g_vnc.fd, fbur, 10);
            g_vnc.state = VNC_STATE_RUNNING;
        }
    }
    else if (g_vnc.state == VNC_STATE_RUNNING) {
        if (c->rx_len >= 4) {
            uint8_t msg_type;
            vnc_recv(g_vnc.fd, &msg_type, 1);
            if (msg_type == 0) {
                uint8_t pad; uint16_t num_rects;
                vnc_recv(g_vnc.fd, &pad, 1);
                vnc_recv(g_vnc.fd, &num_rects, 2);
                if (num_rects > 0) g_vnc.state = VNC_STATE_WAIT_RECT_HEADER;
                else g_vnc.state = VNC_STATE_RUNNING;
            } else {
                c->rx_len = 0;
            }
        }
    }
    else if (g_vnc.state == VNC_STATE_WAIT_RECT_HEADER) {
        if (c->rx_len >= 12) {
            uint8_t hdr[12];
            vnc_recv(g_vnc.fd, hdr, 12);
            g_vnc.rect_x = (hdr[0] << 8) | hdr[1];
            g_vnc.rect_y = (hdr[2] << 8) | hdr[3];
            g_vnc.rect_w = (hdr[4] << 8) | hdr[5];
            g_vnc.rect_h = (hdr[6] << 8) | hdr[7];
            uint32_t enc_type = (hdr[8] << 24) | (hdr[9] << 16) | (hdr[10] << 8) | hdr[11];
            
            if (enc_type == 0) {
                g_vnc.rect_buf = (uint32_t *)kmalloc(g_vnc.rect_w * g_vnc.rect_h * 4);
                g_vnc.rect_bytes_read = 0;
                g_vnc.state = VNC_STATE_WAIT_RECT_DATA;
            } else {
                g_vnc.state = VNC_STATE_RUNNING;
            }
        }
    }
    else if (g_vnc.state == VNC_STATE_WAIT_RECT_DATA) {
        int total_bytes = g_vnc.rect_w * g_vnc.rect_h * 4;
        int remaining = total_bytes - g_vnc.rect_bytes_read;
        
        if (c->rx_len > 0 && remaining > 0) {
            int to_read = c->rx_len > (uint32_t)remaining ? remaining : (int)c->rx_len;
            uint8_t *dst = (uint8_t *)g_vnc.rect_buf + g_vnc.rect_bytes_read;
            vnc_recv(g_vnc.fd, dst, to_read);
            g_vnc.rect_bytes_read += to_read;
        }

        if (g_vnc.rect_bytes_read >= total_bytes) {
            for (uint32_t i = 0; i < g_vnc.rect_w * g_vnc.rect_h; i++) {
                uint32_t px = g_vnc.rect_buf[i];
                uint32_t b = (px >> 24) & 0xFF;
                uint32_t g = (px >> 16) & 0xFF;
                uint32_t r = (px >> 8) & 0xFF;
                g_vnc.rect_buf[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }
            
            fb_blit_argb(g_vnc.rect_x, g_vnc.rect_y, g_vnc.rect_w, g_vnc.rect_h, g_vnc.rect_buf);
            
            kfree(g_vnc.rect_buf);
            g_vnc.state = VNC_STATE_RUNNING;
            
            uint8_t fbur[10] = { 3, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
            fbur[6] = (g_vnc.width >> 8);  fbur[7] = (g_vnc.width & 0xFF);
            fbur[8] = (g_vnc.height >> 8); fbur[9] = (g_vnc.height & 0xFF);
            vnc_send(g_vnc.fd, fbur, 10);
        }
    }
}

void vnc_draw(int cx, int cy, int cw, int ch) {
    (void)cx; (void)cy; (void)cw; (void)ch;
}
