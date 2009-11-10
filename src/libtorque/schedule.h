#ifndef LIBTORQUE_SCHEDULE
#define LIBTORQUE_SCHEDULE

#ifdef __cplusplus
extern "C" {
#endif

#if defined(LIBTORQUE_LINUX)
#include <sched.h>
#elif defined(LIBTORQUE_FREEBSD)
#include <sys/param.h>
#include <sys/cpuset.h>
typedef cpuset_t cpu_set_t;
#endif

unsigned libtorque_affinitymapping(unsigned) __attribute__ ((visibility("default")));

// Remaining declarations are internal to libtorque via -fvisibility=hidden
unsigned detect_cpucount(cpu_set_t *);
int associate_affinityid(unsigned,unsigned);
int pin_thread(unsigned);
int unpin_thread(void);
int spawn_threads(void);
int reap_threads(void);

#ifdef __cplusplus
}
#endif

#endif
