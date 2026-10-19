#ifndef _PROTOSW_H_
#define _PROTOSW_H_

#include <stdint.h>

#define AF_INET     2
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define PR_CONNREQUIRED 0x01
#define PR_ATOMIC       0x02
#define PR_ADDR         0x04

struct sockaddr {
    uint8_t  sa_len;
    uint8_t  sa_family;
    char     sa_data[14];
};

struct sockaddr_in {
    uint8_t         sin_len;
    uint8_t         sin_family;
    uint16_t        sin_port;
    uint32_t        sin_addr;
    char            sin_zero[8];
};

struct protosw {
    short   pr_type;
    short   pr_protocol;
    int     pr_flags;

    int     (*pr_attach)(void *ctx);
    void    (*pr_detach)(void *ctx);
    int     (*pr_bind)(void *ctx, struct sockaddr *sa);
    int     (*pr_connect)(void *ctx, struct sockaddr *sa);
    int     (*pr_listen)(void *ctx, int backlog);
    int     (*pr_send)(void *ctx, const void *data, int len, struct sockaddr *to);
    int     (*pr_recv)(void *ctx, void *buf, int max_len, struct sockaddr *from);
    int     (*pr_close)(void *ctx);
    void    (*pr_input)(void *pkt, int len); 
};

struct domain {
    int     dom_family;
    const char *dom_name;
    void    (*dom_init)(void);
    int     dom_protosw_count;
    struct protosw *dom_protosw;
    struct domain *dom_next;
};

void domain_add(struct domain *dom);
void inetdomain_register(void);
void domaininit(void);
struct domain *pffinddomain(int family);
struct protosw *pffindproto(int family, int type, int protocol);
struct protosw *pffindtype(int family, int type);

extern struct domain *domains;

#endif
