#include "protosw.h"
#include <stddef.h>

struct domain *domains = 0;

void domain_add(struct domain *dom) {
    dom->dom_next = domains;
    domains = dom;
    if (dom->dom_init) {
        dom->dom_init();
    }
}

void domaininit(void) {
    // Регистрируем домен AF_INET
    inetdomain_register();
}

struct domain *pffinddomain(int family) {
    struct domain *dp;
    for (dp = domains; dp != NULL; dp = dp->dom_next) {
        if (dp->dom_family == family) {
            return dp;
        }
    }
    return 0;
}

struct protosw *pffindproto(int family, int type, int protocol) {
    struct domain *dp;
    struct protosw *pr;

    dp = pffinddomain(family);
    if (dp == 0) return 0;

    for (int i = 0; i < dp->dom_protosw_count; i++) {
        pr = &dp->dom_protosw[i];
        if (pr->pr_type == type) {
            if (protocol == 0 || pr->pr_protocol == protocol) {
                return pr;
            }
        }
    }
    return 0;
}

struct protosw *pffindtype(int family, int type) {
    return pffindproto(family, type, 0);
}
