#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <libtorque/buffers.h>
#include <libtorque/libtorque.h>

static int
echo_server(int fd,libtorque_cbctx *cbctx,void *v __attribute__ ((unused))){
	const char *buf;
	size_t len,w;

	buf = rxbuffer_valid(cbctx->rxbuf,&len);
	if(len == 0){
		fprintf(stdout,"[%4d] closed\n",fd);
		return -1; // FIXME could still need to transmit
	}
	fprintf(stdout,"[%4d] Read %zub\n",fd,len);
	w = 0;
	while(w < len){
		ssize_t r;

		r = write(fd,buf + w,len - w);
		if(r < 0){
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				break;
			}else if(errno != EINTR){
				fprintf(stderr,"[%4d] Error %d (%s) writing %zub\n",fd,
						errno,strerror(errno),len);
				return -1;
			}
		}else{
			w += r;
		}
	}
	printf("wrote %zu/%zu\n",w,len);
	rxbuffer_advance(cbctx->rxbuf,w);
	return 0;
}

static void
conn_handler(int fd,libtorque_cbctx *cbctx __attribute__ ((unused)),
				void *v __attribute__ ((unused))){

	fprintf(stdout,"Got a connection on %d\n",fd);
	do{
		struct sockaddr_in sina;
		socklen_t slen;
		int sd;

		slen = sizeof(sina);
		while((sd = accept(fd,(struct sockaddr *)&sina,&slen)) >= 0){
			int flags;

			fprintf(stdout,"Got new connection on sd %d\n",sd);
			if(((flags = fcntl(sd,F_GETFL)) < 0) ||
					fcntl(sd,F_SETFL,flags | (long)O_NONBLOCK)){
				close(sd);
			}else if(libtorque_addfd(libtorque_getcurctx(),sd,
					echo_server,echo_server,NULL)){
				fprintf(stderr,"Couldn't add client sd %d\n",sd);
				close(sd);
			}
		}
	}while(errno == EINTR);
	if(errno != EAGAIN && errno != EWOULDBLOCK){
		fprintf(stdout,"Accept() callback errno %d (%s)\n",
				errno,strerror(errno));
	}
}

static int
make_echo_fd(int domain,const struct sockaddr *saddr,socklen_t slen){
	int sd,reuse = 1,flags;

	if((sd = socket(domain,SOCK_STREAM,0)) < 0){
		return -1;
	}
	if(setsockopt(sd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse))){
		close(sd);
		return -1;
	}
	if(bind(sd,saddr,slen)){
		close(sd);
		return -1;
	}
	if(listen(sd,SOMAXCONN)){
		close(sd);
		return -1;
	}
	if(((flags = fcntl(sd,F_GETFL)) < 0) ||
			fcntl(sd,F_SETFL,flags | (long)O_NONBLOCK)){
		close(sd);
		return -1;
	}
	return sd;
}

#define DEFAULT_PORT ((uint16_t)4007)

static void
print_version(void){
	fprintf(stderr,"echoserver from libtorque " LIBTORQUE_VERSIONSTR "\n");
}

static void
usage(const char *argv0){
	fprintf(stderr,"usage: %s [ options ]\n",argv0);
	fprintf(stderr,"available options:\n");
	fprintf(stderr,"\t-h: print this message\n");
	fprintf(stderr,"\t-p port: specify TCP port for service (default: %hu)\n",DEFAULT_PORT);
	fprintf(stderr,"\t--version: print version info\n");
}

static int
parse_args(int argc,char **argv,uint16_t *port){
#define SET_ARG_ONCE(opt,arg,val) do{ if(!*(arg)){ *arg = val; }\
	else{ fprintf(stderr,"Provided '%c' twice\n",(opt)); goto err; }} while(0)
	int lflag;
	const struct option opts[] = {
		{	 .name = "version",
			.has_arg = 0,
			.flag = &lflag,
			.val = 'v',
		},
		{	 .name = NULL, .has_arg = 0, .flag = 0, .val = 0, },
	};
	const char *argv0 = *argv;
	int c;

	while((c = getopt_long(argc,argv,"p:h",opts,NULL)) >= 0){
		switch(c){
		case 'p': { int p = atoi(optarg);
			if(p > 0xffff || p == 0){
				goto err;
			}
			SET_ARG_ONCE('p',port,p);
			break;
		}
		case 'h':
			usage(argv0);
			exit(EXIT_SUCCESS);
		case 0: // long option
			switch(lflag){
				case 'v':
					print_version();
					exit(EXIT_SUCCESS);
				default:
					goto err;
			}
		default:
			goto err;
		}
	}
	return 0;

err:
	usage(argv0);
	return -1;
#undef SET_ARG_ONCE
}

int main(int argc,char **argv){
	struct libtorque_ctx *ctx = NULL;
	struct sockaddr_in sin;
	libtorque_err err;
	sigset_t termset;
	int sig,sd = -1;

	sigemptyset(&termset);
	sigaddset(&termset,SIGINT);
	sigaddset(&termset,SIGTERM);
	memset(&sin,0,sizeof(sin));
	if(parse_args(argc,argv,&sin.sin_port)){
		return EXIT_FAILURE;
	}
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(sin.sin_port ? sin.sin_port : DEFAULT_PORT);
	if(pthread_sigmask(SIG_SETMASK,&termset,NULL)){
		fprintf(stderr,"Couldn't set signal mask\n");
		return EXIT_FAILURE;
	}
	if((ctx = libtorque_init(&err)) == NULL){
		fprintf(stderr,"Couldn't initialize libtorque (%s)\n",
				libtorque_errstr(err));
		goto err;
	}
	if((sd = make_echo_fd(AF_INET,(struct sockaddr *)&sin,sizeof(sin))) < 0){
		fprintf(stderr,"Couldn't create server sd\n");
		goto err;
	}
	printf("Registering server sd %d, port %hu\n",sd,ntohs(sin.sin_port));
	if(libtorque_addfd_unbuffered(ctx,sd,conn_handler,NULL,NULL)){
		fprintf(stderr,"Couldn't add server sd %d\n",sd);
		goto err;
	}
	printf("Waiting on termination signal...\n");
	if(sigwait(&termset,&sig)){
		fprintf(stderr,"Couldn't wait on signals\n");
		goto err;
	}
	printf("Got signal %d (%s), closing down...\n",sig,strsignal(sig));
	if( (err = libtorque_stop(ctx)) ){
		fprintf(stderr,"Couldn't shutdown libtorque (%s)\n",
				libtorque_errstr(err));
		return EXIT_FAILURE;
	}
	printf("Successfully cleaned up.\n");
	return EXIT_SUCCESS;

err:
	if( (err = libtorque_stop(ctx)) ){
		fprintf(stderr,"Couldn't shutdown libtorque (%s)\n",
				libtorque_errstr(err));
		return EXIT_FAILURE;
	}
	if((sd >= 0) && close(sd)){
		fprintf(stderr,"Couldn't close server socket %d\n",sd);
		return EXIT_FAILURE;
	}
	return EXIT_FAILURE;
}
