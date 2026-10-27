// BrainixKernel — ifnet.h
// Абстракция сетевого интерфейса. Вдохновлено FreeBSD.
#ifndef BRAINIX_IFNET_H
#define BRAINIX_IFNET_H

#include <stdint.h>
#include "mbuf.h"

#define IFNAMSIZ 16

struct ifnet {
    struct ifnet *if_next;      // Следующий интерфейс в глобальном списке
    char          if_name[IFNAMSIZ]; // Имя (например, "vnet0")
    uint8_t       if_mac[6];    // MAC-адрес
    uint32_t      if_mtu;       // MTU (обычно 1500)

    // Отправить пакет в железо. Возвращает 0 при успехе, -1 при ошибке.
    int  (*if_transmit)(struct ifnet *ifp, struct mbuf *m);
    
    // Вызывается драйвером при получении пакета. Передает mbuf наверх.
    void (*if_input)(struct ifnet *ifp, struct mbuf *m);
};

// Зарегистрировать новый интерфейс в ядре.
void ifnet_register(struct ifnet *ifp);

// Получить первый (или единственный) интерфейс.
struct ifnet *ifnet_get_default(void);

// Найти интерфейс по имени.
struct ifnet *ifnet_find(const char *name);

#endif
