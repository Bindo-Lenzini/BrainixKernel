// BrainixKernel (BKOS) — ifnet.c
#include "ifnet.h"

extern void uart_puts(const char *s);

static int if_strcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static struct ifnet *g_ifnet_list = 0;

void ifnet_register(struct ifnet *ifp) {
    if (!ifp) return;
    ifp->if_next = g_ifnet_list;
    g_ifnet_list = ifp;
    uart_puts("[ifnet] registered: ");
    uart_puts(ifp->if_name);
    uart_puts("\n");
}

struct ifnet *ifnet_get_default(void) {
    return g_ifnet_list;
}

struct ifnet *ifnet_find(const char *name) {
    struct ifnet *ifp = g_ifnet_list;
    while (ifp) {
        if (if_strcmp(ifp->if_name, name) == 0) return ifp;
        ifp = ifp->if_next;
    }
    return 0;
}
