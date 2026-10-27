// BrainixKernel — arp.c
// С отладочным логом.

#include <stdint.h>
#include "arp.h"
#include "net.h"
#include "virtio_net.h"

extern void uart_puts(const char *s);
extern void uart_putu(uint64_t v);
extern void uart_puthex(uint64_t v);

#define ARP_MAX 8

typedef struct {
    ipv4_t ip;
    mac_t  mac;
    int    valid;
} arp_entry_t;

static arp_entry_t g_table[ARP_MAX];

static void mem_copy(void *d, const void *s, uint32_t n) {
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    for (uint32_t i = 0; i < n; i++) dd[i] = ss[i];
}
static void mem_zero(void *d, uint32_t n) {
    uint8_t *dd = (uint8_t *)d;
    for (uint32_t i = 0; i < n; i++) dd[i] = 0;
}

static arp_entry_t *arp_find(ipv4_t ip) {
    for (int i = 0; i < ARP_MAX; i++)
        if (g_table[i].valid && ipv4_eq(g_table[i].ip, ip)) return &g_table[i];
    return 0;
}

static void arp_add(ipv4_t ip, const uint8_t *mac) {
    arp_entry_t *e = arp_find(ip);
    if (e) { mem_copy(e->mac.b, mac, 6); return; }
    for (int i = 0; i < ARP_MAX; i++) {
        if (!g_table[i].valid) {
            g_table[i].ip = ip;
            mem_copy(g_table[i].mac.b, mac, 6);
            g_table[i].valid = 1;
            return;
        }
    }
    g_table[0].ip = ip;
    mem_copy(g_table[0].mac.b, mac, 6);
    g_table[0].valid = 1;
}

static int arp_send_request(ipv4_t target) {
    uint8_t frame[64];
    mem_zero(frame, 64);

    mac_t my_mac = net_get_mac();
    uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    arp_pkt_t *arp = (arp_pkt_t *)net_build_eth(
        frame, bcast, my_mac.b, ETH_TYPE_ARP);

    arp->htype = htons16(1);
    arp->ptype = htons16(ETH_TYPE_IPV4);
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->oper  = htons16(1);

    ipv4_t my_ip = net_get_ip();
    mem_copy(arp->sha, my_mac.b, 6);
    mem_copy(arp->spa, my_ip.b, 4);
    mem_zero(arp->tha, 6);
    mem_copy(arp->tpa, target.b, 4);

    int r = vnet_send(frame, 64);
    uart_puts("  [arp] sent req, result=");
    uart_putu((uint64_t)r);
    uart_puts("\n");
    return r;
}

int arp_handle_frame(const uint8_t *frame, uint32_t len) {
    if (len < 14 + 28) return 0;
    const eth_hdr_t *eth = (const eth_hdr_t *)frame;
    if (ntohs16(eth->ethertype) != ETH_TYPE_ARP) return 0;

    const arp_pkt_t *arp = (const arp_pkt_t *)(frame + 14);
    if (ntohs16(arp->htype) != 1) return 0;
    if (ntohs16(arp->ptype) != ETH_TYPE_IPV4) return 0;
    if (arp->hlen != 6 || arp->plen != 4) return 0;

    uart_puts("  [arp] got ARP reply! oper=");
    uart_puthex(ntohs16(arp->oper));
    uart_puts("\n");

    ipv4_t sender_ip;
    mem_copy(sender_ip.b, arp->spa, 4);
    arp_add(sender_ip, arp->sha);

    uint16_t oper = ntohs16(arp->oper);
    if (oper == 1) {
        ipv4_t my_ip = net_get_ip();
        ipv4_t target_ip;
        mem_copy(target_ip.b, arp->tpa, 4);
        if (!ipv4_eq(target_ip, my_ip)) return 1;

        uint8_t reply[64];
        mem_zero(reply, 64);

        mac_t my_mac = net_get_mac();
        arp_pkt_t *rarp = (arp_pkt_t *)net_build_eth(
            reply, arp->sha, my_mac.b, ETH_TYPE_ARP);

        rarp->htype = htons16(1);
        rarp->ptype = htons16(ETH_TYPE_IPV4);
        rarp->hlen  = 6;
        rarp->plen  = 4;
        rarp->oper  = htons16(2);
        mem_copy(rarp->sha, my_mac.b, 6);
        mem_copy(rarp->spa, my_ip.b, 4);
        mem_copy(rarp->tha, arp->sha, 6);
        mem_copy(rarp->tpa, arp->spa, 4);

        vnet_send(reply, 64);
    }
    return 1;
}

void arp_init(void) {
    for (int i = 0; i < ARP_MAX; i++) g_table[i].valid = 0;
}

int arp_lookup(ipv4_t ip, mac_t *out_mac) {
    arp_entry_t *e = arp_find(ip);
    if (!e) return 0;
    *out_mac = e->mac;
    return 1;
}

int arp_resolve(ipv4_t ip, mac_t *out_mac) {
    if (arp_lookup(ip, out_mac)) return 1;
    if (!arp_send_request(ip)) return 0;

    uint8_t frame[1600];
    int total_frames = 0;
    for (int attempt = 0; attempt < 200; attempt++) {
        uint32_t flen = 0;
        while (vnet_recv(frame, sizeof(frame), &flen)) {
            total_frames++;

            // Лог каждого пакета
            if (flen >= 14) {
                const eth_hdr_t *eth = (const eth_hdr_t *)frame;
                uart_puts("  [net] RX frame #");
                uart_putu((uint64_t)total_frames);
                uart_puts(" len=");
                uart_putu(flen);
                uart_puts(" type=");
                uart_puthex(ntohs16(eth->ethertype));
                uart_puts("\n");
            }

            if (arp_handle_frame(frame, flen)) {
                if (arp_lookup(ip, out_mac)) {
                    uart_puts("  [arp] resolved!\n");
                    return 1;
                }
            }
        }
        // ~10 мс
        for (volatile int i = 0; i < 2000000; i++) {}
    }
    uart_puts("  [arp] timeout, frames_received=");
    uart_putu((uint64_t)total_frames);
    uart_puts("\n");
    return 0;
}