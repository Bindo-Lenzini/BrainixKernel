// BrainixKernel (BKOS) — arp.h
#ifndef BKOS_ARP_H
#define BKOS_ARP_H

#include <stdint.h>
#include "net.h"

void arp_init(void);

// Узнать MAC для IP.
// 1 — успех и заполняет out_mac (из кэша или после обмена с сетью).
// 0 — нет ответа.
int  arp_resolve(ipv4_t ip, mac_t *out_mac);

// Обработать входящий Ethernet-фрейм, если это ARP.
// Возвращает 1 если фрейм был ARP (обработан), иначе 0.
int  arp_handle_frame(const uint8_t *frame, uint32_t len);

// Заглянуть в кэш без сетевой активности.
int  arp_lookup(ipv4_t ip, mac_t *out_mac);

#endif
