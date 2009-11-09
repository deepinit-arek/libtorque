#ifndef LIBTORQUE_EVENTS_SOURCES
#define LIBTORQUE_EVENTS_SOURCES

#ifdef __cplusplus
extern "C" {
#endif

#include <libtorque/libtorque.h>

typedef libtorque_evcbfxn evcbfxn;

struct evsource;

struct evsource *create_evsources(unsigned)
	__attribute__ ((warn_unused_result)) __attribute__ ((malloc));

// Central assertion: we can't generally know when a file descriptor is closed
// among the going-ons of callback processing (examples: library code with
// funky interfaces, errors, other infrastructure). Thus, setup_evsource()
// always knocks out any currently-registered handling for an fd. This is
// possible because closing an fd does remove it from event queue
// implementations for both epoll and kqueue. Since we can't base anything on
// having the fd cleared, we design to not care about it at all -- there is no
// feedback from the callback functions, and nothing needs to call anything
// upon closing an fd.
void setup_evsource(struct evsource *,int,evcbfxn,evcbfxn,void *);
int handle_evsource_read(struct evsource *,unsigned);

int destroy_evsources(struct evsource *,unsigned);

#ifdef __cplusplus
}
#endif

#endif
