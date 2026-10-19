// BrainixKernel (BKOS) — icmp.h
#ifndef BKOS_ICMP_H
#define BKOS_ICMP_H

#include <stdint.h>
#include "net.h"

// Отправить ICMP Echo Request и подождать Echo Reply.
// 1 — получили reply, 0 — timeout/ошибка.
int icmp_ping(ipv4_t target, mac_t target_mac, uint16_t seq);

// Обработка входящего Ethernet-фрейма, если это ICMP.
// Возвращает 1 если фрейм был ICMP (обработан), иначе 0.
int icmp_handle_frame(const uint8_t *frame, uint32_t len);

#endif
