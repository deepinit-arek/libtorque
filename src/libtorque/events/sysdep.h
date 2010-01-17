#ifndef LIBTORQUE_EVENTS_SYSDEP
#define LIBTORQUE_EVENTS_SYSDEP

#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>

#define EVTHREAD_TERM	SIGTERM

#include <errno.h>

// To emulate FreeBSD's kevent interface, supply a marshalling of two vectors.
// One's the epoll_event vector we feed directly to epoll_wait(), the other the
// data necessary to iterate over epoll_ctl() upon entry. evchanges and events
// may (but needn't) alias.
//
// See kevent(7) on FreeBSD.
#ifdef LIBTORQUE_LINUX
#include <sys/epoll.h>

#define PTR_TO_EVENTV(ev) (&(ev)->eventv)
typedef struct epoll_event kevententry;
#define KEVENTENTRY_FD(k) ((k)->data.fd)

struct kevent { // each element an array, each array the same number of members
	struct epoll_ctl_data {
		int op;
	} *ctldata; // array of ctldata
	kevententry *events; // array of kevententries
};

// Emulation of FreeBSD's kevent(2) notification mechanism. Timeout values are
// specified differently between the two, so we just disable them entirely
// (since we're never using them anyway). If they need be restored, just
// pass (t ? (t->tv_sec * 1000 + t->tv_nsec / 1000000) : -1) to epoll_wait().
// signalfd was introduced to glibc in 2.8
#if __GLIBC__ > 2 || __GLIBC__ == 2 && __GLIBC_MINOR__ > 8
#define LIBTORQUE_LINUX_TIMERFD
#include <sys/timerfd.h>
#define LIBTORQUE_LINUX_SIGNALFD
#include <sys/signalfd.h>
struct libtorque_cbctx;

void signalfd_demultiplexer(int,struct libtorque_cbctx *,void *);
#else
#if __GLIBC__ == 2 && __GLIBC_MINOR__ > 5
#define LIBTORQUE_LINUX_PWAIT
#endif
extern const sigset_t *epoll_sigset;

int init_epoll_sigset(void);
int add_epoll_sigset(const sigset_t *,unsigned);
#endif
static inline int
kevent_retrieve(int epfd,struct kevent *eventlist,int nevents){
	int ret;

#if defined(LIBTORQUE_LINUX_SIGNALFD)
	do{
#else
	sigset_t tmp;

	if(pthread_sigmask(SIG_SETMASK,epoll_sigset,&tmp)){
		return -1;
	}
#endif
		ret = epoll_wait(epfd,eventlist->events,nevents,-1);
#if defined(LIBTORQUE_LINUX_SIGNALFD)
	}while(ret < 0 || errno == EINTR);
#else
	pthread_sigmask(SIG_SETMASK,&tmp,NULL);
#endif
	return ret;
}

static inline int
Kevent(int epfd,struct kevent *changelist,int nchanges,
		struct kevent *eventlist,int nevents){
	int n,ret = 0;

	for(n = 0 ; n < nchanges ; ++n){
		if(epoll_ctl(epfd,changelist->ctldata[n].op,
				changelist->events[n].data.fd,
				&changelist->events[n]) < 0){
			// FIXME let's get things precisely defined...divine
			// and emulate a precise definition of bsd's kevent()
			ret = -1;
		}
	}
	if(ret){
		return ret;
	}
	// Calling epoll_wait() with nevents <= 0 is an error (EINVAL), but
	// not for our kevent() emulation semantics. Return 0 early.
	if(nevents == 0){
		return 0;
	}
	// We don't block on EINTR unless we're using signalfd's, since an
	// EINTR in that case actually represents processing (hence counting
	// it as an event), and thus we ought do per-round things (such as
	// checking for termination). Furthermore, we can't do most processing
	// inside an actual signal handler, so we must do it outside.
#ifdef LIBTORQUE_LINUX_PWAIT
	// The kernel might not provide epoll_pwait() despite glibc having
	// support for it. In that case, fall back to epoll_wait(). We will
	// waste the overhead calling into glibc each time, but this ought be a
	// sufficiently rare case that it doesn't matter...FIXME
	ret = epoll_pwait(epfd,eventlist->events,nevents,-1,epoll_sigset);
	if(ret < 0 && errno == ENOSYS)
#endif
		ret = kevent_retrieve(epfd,eventlist,nevents);
	return ret;
}

#elif defined(LIBTORQUE_FREEBSD)
#include <stdint.h>
#include <sys/types.h>
#include <sys/event.h>

#define PTR_TO_EVENTV(ev) ((ev)->eventv)
typedef struct kevent kevententry;
#define KEVENTENTRY_FD(k) ((int)(k)->ident)
#define KEVENTENTRY_SIG(k) ((int)(k)->ident) // Doesn't exist on Linux

#include <pthread.h>

static inline int
Kevent(int kq,struct kevent *changelist,int nchanges,
		struct kevent *eventlist,int nevents){
	int ret;

	while((ret = kevent(kq,changelist,nchanges,eventlist,nevents,NULL)) < 0){
		if(errno != EINTR){ // loop on EINTR
			break;
		}
	}
	return ret;
}
#endif

// State necessary for changing the domain of events and/or having them
// reported. One is required to do any event handling, under any scheme, and
// many plausible schemes will employ multiple evectors.
typedef struct evectors {
	// compat-<OS>.h provides a kqueue-like interface (in terms of change
	// vectorization, which linux doesn't support) for non-BSD's
#ifdef LIBTORQUE_LINUX
	struct kevent eventv,changev;
#elif defined(LIBTORQUE_FREEBSD)
	struct kevent *eventv,*changev;
#else
#error "No operating system support for event notification"
#endif
	int vsizes,changesqueued;
} evectors;

#ifdef LIBTORQUE_FREEBSD
#define EVECTOR_AUTOS(count,name,n2) struct kevent n2[count]; evectors name = \
 { .eventv = n2, .changev = n2, .vsize = (count), .changesqueued = 0, }
#else
#define EVECTOR_AUTOS(count,name,n2) \
	struct epoll_ctl_data ctlarr[count]; \
	kevententry evarr[count]; \
	evectors name = { .eventv = { .ctldata = ctlarr, .events = evarr, }, \
		.changev = { .ctldata = ctlarr, .events = evarr, }, \
		.vsizes = (count), .changesqueued = 0, }
#endif

struct evhandler;

int add_evector_kevents(struct evectors *,const struct kevent *,int);
int flush_evector_changes(struct evhandler *,evectors *);

#ifdef __cplusplus
}
#endif

#endif
