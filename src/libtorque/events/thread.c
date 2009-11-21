#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/resource.h>
#include <libtorque/events/sysdep.h>
#include <libtorque/events/thread.h>
#include <libtorque/events/signals.h>
#include <libtorque/events/sources.h>

static __thread evhandler *tsd_evhandler;

static void
handle_event(evhandler *eh,const kevententry *e){
	int ret = 0;

#ifdef LIBTORQUE_LINUX
	if(e->events & EPOLLIN){
#else
	if(e->filter == EVFILT_READ){
#endif
		ret |= handle_evsource_read(eh->evsources->fdarray,KEVENTENTRY_FD(e));
	}
#ifdef LIBTORQUE_LINUX
	if(e->events & EPOLLOUT){
#else
	if(e->filter == EVFILT_WRITE){
#endif
		ret |= handle_evsource_write(eh->evsources->fdarray,KEVENTENTRY_FD(e));
	}
}

static int
rxsignal(int sig,torquercbstate *nullv __attribute__ ((unused))){
	if(sig == EVTHREAD_TERM){
		void *ret = PTHREAD_CANCELED;
		evhandler *e = tsd_evhandler;
		struct rusage ru;
		int r;

		getrusage(RUSAGE_THREAD,&ru);
		e->stats.utimeus = ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec;
		e->stats.stimeus = ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec;
		pthread_kill(e->nexttid,EVTHREAD_TERM);
		// We rely on EDEADLK to cut off our circular join()list
		if((r = pthread_join(e->nexttid,&ret)) && r != EDEADLK){
			ret = NULL;
		}
		if(destroy_evhandler(e)){
			ret = NULL;
		}
		pthread_exit(ret);
	}
	return 0;
}

void event_thread(evhandler *e){
	tsd_evhandler = e;
	while(1){
		int events;

		events = Kevent(e->efd,PTR_TO_CHANGEV(&e->evec),e->evec.changesqueued,
					PTR_TO_EVENTV(&e->evec),e->evec.vsizes);
		++e->stats.rounds;
		if(events <= 0){
			continue;
		}
		e->stats.events += events;
		do{
#ifdef LIBTORQUE_LINUX
		handle_event(e,&PTR_TO_EVENTV(&e->evec)->events[--events]);
#else
		handle_event(e,&PTR_TO_EVENTV(&e->evec)[--events]);
#endif
		}while(events);
	}
}

static int
#ifdef LIBTORQUE_LINUX
create_evector(struct kevent *kv,int n){
	if((kv->events = malloc(n * sizeof(*kv->events))) == NULL){
		return -1;
	}
	if((kv->ctldata = malloc(n * sizeof(*kv->ctldata))) == NULL){
		free(kv->events);
		return -1;
	}
	return 0;
#else
create_evector(struct kevent **kv,int n){
	if((*kv = malloc(n * sizeof(**kv))) == NULL){
		return -1;
	}
	return 0;
#endif
}

static void
#ifdef LIBTORQUE_LINUX
destroy_evector(struct kevent *kv){
	free(kv->events);
	free(kv->ctldata);
#else
destroy_evector(struct kevent **kv){
	free(*kv);
#endif
}

static int
init_evectors(evectors *ev){
	// We probably want about a half (small) page's worth...? FIXME
	ev->vsizes = 512;
	if(create_evector(&ev->eventv,ev->vsizes)){
		return -1;
	}
	if(create_evector(&ev->changev,ev->vsizes)){
		destroy_evector(&ev->eventv);
		return -1;
	}
	ev->changesqueued = 0;
	return 0;
}

static void
destroy_evectors(evectors *e){
	if(e){
		destroy_evector(&e->changev);
		destroy_evector(&e->eventv);
	}
}

static int
add_evhandler_baseevents(evhandler *e){
	sigset_t s;

	if(init_evectors(&e->evec)){
		return -1;
	}
	if(sigemptyset(&s) || sigaddset(&s,EVTHREAD_SIGNAL) || sigaddset(&s,EVTHREAD_TERM)){
		destroy_evectors(&e->evec);
		return -1;
	}
	if(add_signal_to_evhandler(e,&s,rxsignal,NULL)){
		destroy_evectors(&e->evec);
		return -1;
	}
	return 0;
}

static int
initialize_evhandler(evhandler *e,evtables *evsources,int fd){
	memset(e,0,sizeof(*e));
	e->evsources = evsources;
	e->efd = fd;
	if(add_evhandler_baseevents(e)){
		return -1;
	}
	return 0;
}

evhandler *create_evhandler(evtables *evsources){
	evhandler *ret;
	int fd,flags;

	if(evsources == NULL){
		return NULL;
	}
// Until the epoll API stabilizes a bit... :/
#ifdef LIBTORQUE_LINUX
#ifdef EPOLL_CLOEXEC
#define SAFE_EPOLL_CLOEXEC EPOLL_CLOEXEC
#else // otherwise, it wants a size hint in terms of fd's
#include <linux/limits.h>
#define SAFE_EPOLL_CLOEXEC NR_OPEN
#endif
	if((fd = epoll_create(SAFE_EPOLL_CLOEXEC)) < 0){
		return NULL;
	}
	if(SAFE_EPOLL_CLOEXEC == 0){
		if(((flags = fcntl(fd,F_GETFD)) < 0) ||
				fcntl(fd,F_SETFD,flags | FD_CLOEXEC)){
			close(fd);
			return NULL;
		}
	}
#undef SAFE_EPOLL_CLOEXEC
#elif defined(LIBTORQUE_FREEBSD)
	if((fd = kqueue()) < 0){
		return NULL;
	}
	if(((flags = fcntl(fd,F_GETFD)) < 0) || fcntl(fd,F_SETFD,flags | FD_CLOEXEC)){
		close(fd);
		return NULL;
	}
#endif
	if( (ret = malloc(sizeof(*ret))) ){
		if(initialize_evhandler(ret,evsources,fd) == 0){
			return ret;
		}
		free(ret);
	}
	close(fd);
	return NULL;
}

static void print_evstats(const evthreadstats *stats){
#define PRINTSTAT(s,field) \
	do { if((s)->field){ printf(#field ": %ju\n",(s)->field); } }while(0)
#define STATDEF(field) PRINTSTAT(stats,field);
#include <libtorque/events/x-stats.h>
#undef STATDEF
#undef PRINTSTAT
}

int destroy_evhandler(evhandler *e){
	int ret = 0;

	if(e){
		print_evstats(&e->stats);
		destroy_evectors(&e->evec);
		ret |= close(e->efd);
		free(e);
	}
	return ret;
}
