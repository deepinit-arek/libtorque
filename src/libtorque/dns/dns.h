#ifndef LIBTORQUE_DNS_DNS
#define LIBTORQUE_DNS_DNS

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <libtorque/libtorque.h>

#ifndef LIBTORQUE_WITHOUT_ADNS
#include <adns.h>
typedef adns_state dns_state;
#else
typedef struct {
	int fd;
} dns_state;
#endif

struct evqueue;
struct evhandler;
struct dnsmarshal;
struct libtorque_ctx;

int libtorque_dns_init(dns_state *)
	__attribute__ ((warn_unused_result))
	__attribute__ ((nonnull(1)));

int load_dns_fds(struct libtorque_ctx *,dns_state *,const struct evqueue *)
	__attribute__ ((warn_unused_result))
	__attribute__ ((nonnull(1,2,3)));

int restore_dns_fds(dns_state,const struct evhandler *);

void libtorque_dns_shutdown(dns_state *);

struct dnsmarshal *create_dnsmarshal(libtorquednscb,void *)
	__attribute__ ((warn_unused_result))
	__attribute__ ((malloc));

static inline void
free_dnsmarshal(struct dnsmarshal *dm){
	free(dm);
}

#ifdef __cplusplus
}
#endif

#endif