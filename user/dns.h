// BrainixKernel (BKOS) — user/dns.h
// Минимальный DNS-клиент.

#ifndef USER_DNS_H
#define USER_DNS_H

// Резолвит A-запись для hostname через 10.0.2.3:53.
// Возврат: 1 при успехе, out_ip заполнен 4 байтами IPv4.
// 0 при ошибке/таймауте.
int dns_resolve(const char *hostname, unsigned char out_ip[4]);

#endif
