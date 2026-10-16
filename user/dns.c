// BrainixKernel (BKOS) — user/dns.c
// Минимальный DNS-клиент: A-запросы, UDP, один ответ.

#include "dns.h"
#include "user.h"
#include "libc.h"

static const unsigned char DNS_SERVER[4] = {10, 0, 2, 3};
#define DNS_PORT 53

// Собрать DNS-запрос. Возвращает длину или 0.
static int build_query(const char *name, unsigned char *buf, int buf_size,
                       unsigned short id)
{
    if (buf_size < 12 + 256 + 4) return 0;

    int i = 0;
    buf[i++] = (id >> 8) & 0xFF;
    buf[i++] = id & 0xFF;
    buf[i++] = 0x01; buf[i++] = 0x00;   // flags: RD=1
    buf[i++] = 0x00; buf[i++] = 0x01;   // QDCOUNT = 1
    buf[i++] = 0x00; buf[i++] = 0x00;   // ANCOUNT
    buf[i++] = 0x00; buf[i++] = 0x00;   // NSCOUNT
    buf[i++] = 0x00; buf[i++] = 0x00;   // ARCOUNT

    // QNAME: разбить по точкам, каждую метку префиксовать длиной.
    const char *p = name;
    while (*p) {
        const char *q = p;
        while (*q && *q != '.') q++;
        int len = (int)(q - p);
        if (len == 0) { p++; continue; }
        if (len > 63) return 0;
        if (i + 1 + len + 5 > buf_size) return 0;
        buf[i++] = (unsigned char)len;
        for (int k = 0; k < len; k++) buf[i++] = (unsigned char)p[k];
        p = q;
        if (*p == '.') p++;
    }
    buf[i++] = 0;                        // QNAME terminator
    buf[i++] = 0x00; buf[i++] = 0x01;    // QTYPE = A
    buf[i++] = 0x00; buf[i++] = 0x01;    // QCLASS = IN
    return i;
}

// Пропустить NAME в ответе (label-ы или compression pointer).
// Возвращает новую позицию или -1.
static int skip_name(const unsigned char *buf, int pos, int len) {
    while (pos < len) {
        unsigned char b = buf[pos];
        if (b == 0) return pos + 1;
        if ((b & 0xC0) == 0xC0) return pos + 2;   // pointer
        pos += 1 + b;
    }
    return -1;
}

int dns_resolve(const char *hostname, unsigned char out_ip[4]) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    if (bind(fd, 40001) < 0) { close(fd); return 0; }

    unsigned short id = 0x4242;
    unsigned char query[512];
    int qlen = build_query(hostname, query, sizeof(query), id);
    if (qlen <= 0) { close(fd); return 0; }

    if (sendto(fd, query, qlen, DNS_PORT, DNS_SERVER) < 0) {
        close(fd);
        return 0;
    }

    for (int attempt = 0; attempt < 30000; attempt++) {
        unsigned char resp[512];
        unsigned short src_port = 0;
        unsigned char src_ip[4];

        int r = recvfrom(fd, resp, sizeof(resp), &src_port, src_ip);
        if (r <= 0) { yield(); continue; }
        if (r < 12) { yield(); continue; }

        unsigned short rid = ((unsigned short)resp[0] << 8) | resp[1];
        if (rid != id) { yield(); continue; }

        int qdcount = ((int)resp[4] << 8) | resp[5];
        int ancount = ((int)resp[6] << 8) | resp[7];
        if (ancount < 1) { close(fd); return 0; }

        // Пропустить секцию вопросов.
        int pos = 12;
        for (int k = 0; k < qdcount; k++) {
            pos = skip_name(resp, pos, r);
            if (pos < 0) break;
            pos += 4;
        }
        if (pos < 0) { close(fd); return 0; }

        // Пройти по ответам, найти первую A-запись.
        for (int k = 0; k < ancount; k++) {
            pos = skip_name(resp, pos, r);
            if (pos < 0 || pos + 10 > r) break;

            int type = ((int)resp[pos]     << 8) | resp[pos + 1];
            int cls  = ((int)resp[pos + 2] << 8) | resp[pos + 3];
            int rdlen = ((int)resp[pos + 8] << 8) | resp[pos + 9];
            pos += 10;

            if (pos + rdlen > r) break;

            if (type == 1 && cls == 1 && rdlen == 4) {
                out_ip[0] = resp[pos];
                out_ip[1] = resp[pos + 1];
                out_ip[2] = resp[pos + 2];
                out_ip[3] = resp[pos + 3];
                close(fd);
                return 1;
            }
            pos += rdlen;
        }

        close(fd);
        return 0;   // ответ был, но A-записи нет
    }

    close(fd);
    return 0;
}
