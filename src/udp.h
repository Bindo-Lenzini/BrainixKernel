// BrainixKernel (BKOS) — udp.h
#ifndef BKOS_UDP_H
#define BKOS_UDP_H

#include <stdint.h>

// Обработать IPv4-пакет с protocol=17 (UDP).
//   iph — указатель на начало IP-заголовка
//   ip_total_len — полная длина IP-пакета (header + payload)
// Возврат 1 если пакет был разобран (даже если порт не найден).
int udp_handle_ipv4(const uint8_t *iph, uint32_t ip_total_len);

#endif
