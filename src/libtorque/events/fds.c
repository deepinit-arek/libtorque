#include <pthread.h>
#include <libtorque/events/fds.h>
#include <libtorque/events/sysdep.h>
#include <libtorque/events/sources.h>

static inline int
add_fd_event(struct evectors *ev,int fd,libtorquecb rfxn,libtorquecb tfxn){
#ifdef LIBTORQUE_LINUX
	struct epoll_ctl_data ecd;
	struct epoll_event ee;
	struct kevent k;

	memset(&ee.data,0,sizeof(ee.data));
	k.events = &ee;
	ee.data.fd = fd;
	k.ctldata = &ecd;
	ecd.op = EPOLL_CTL_ADD;
	// We automatically wait for EPOLLERR/EPOLLHUP; according to
	// epoll_ctl(2), "it is not necessary to add set [these] in ->events"
	ee.events = EPOLLET | EPOLLRDHUP | EPOLLPRI;
	if(rfxn){
		ee.events |= EPOLLIN;
	}
	if(tfxn){
		ee.events |= EPOLLOUT;
	}
	if(add_evector_kevents(ev,&k,1)){
		return -1;
	}
#elif defined(LIBTORQUE_FREEBSD)
	struct kevent k[2];

	if(rfxn){
		EV_SET(&k[0],fd,EVFILT_READ,EV_ADD | EV_CLEAR,0,0,NULL);
	}
	if(tfxn){
		EV_SET(&k[1],fd,EVFILT_WRITE,EV_ADD | EV_CLEAR,0,0,NULL);
	}
	if(add_evector_kevents(ev,k,!!tfxn + !!rfxn)){
		return -1;
	}
#else
#error "No fd event implementation on this OS"
#endif
	return 0;
}

int add_fd_to_evcore(evhandler *eh,struct evectors *ev,int fd,libtorquecb rfxn,
					libtorquecb tfxn,void *cbstate){
	if((unsigned)fd >= eh->evsources->fdarraysize){
		return -1;
	}
	if(add_fd_event(ev,fd,rfxn,tfxn)){
		return -1;
	}
	setup_evsource(eh->evsources->fdarray,fd,rfxn,tfxn,cbstate);
	return 0;
}

int add_fd_to_evhandler(evhandler *eh,int fd,libtorquecb rfxn,
					libtorquecb tfxn,void *cbstate){
	if(pthread_mutex_lock(&eh->lock) == 0){
		struct evectors *ev = eh->externalvec;

		if(add_fd_to_evcore(eh,ev,fd,rfxn,tfxn,cbstate) == 0){
			// flush_evector_changes unlocks on all paths
			return flush_evector_changes(eh,ev);
		}
		pthread_mutex_unlock(&eh->lock);
	}
	return -1;
}
