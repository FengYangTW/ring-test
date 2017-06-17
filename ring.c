#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "ring.h"

int init_pipe(pipe_p _pipe); 

static int bind_server_socket(struct SocketSettings *setting)
{
    int srvfd;
    /* setup the address */
    struct addrinfo hints;
    struct sockaddr_in addr; /* address of this service */
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;  /* Allows IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM;  /* UDP stream sockets */

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(setting->port);
     
    srvfd = socket(hints.ai_family, hints.ai_socktype, 0);
    if (srvfd <= 0) {
        perror("socket error");
        return -1;
    }
    
    /* prevent address from being taken */
    {
        int optval = 1;
        setsockopt(srvfd, SOL_SOCKET, SO_REUSEADDR,
                   &optval, sizeof(optval));
    }
    
    /* bind() failed: close this socket*/
    if (bind(srvfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(srvfd);
        return -1;
    }
    
    return srvfd;
}



static int start_server(struct SocketSettings settings)
{
    socket_p socket = malloc(sizeof(*socket));
    /* bind the server's socket - if relevant */
    int srvfd = 0;
    if (!settings.port)
        settings.port = 8080;
    srvfd = bind_server_socket(&settings);
    /* if we did not get a socket, quit now. */
    if (srvfd < 0) return -1;
    if(settings.on_data)
        settings.on_data(socket, srvfd);
    free(socket);
    return 0;
}

static ssize_t socket_read(socket_p socket, int fd, void *buffer,
               size_t max_len, struct sockaddr *addr)
{
    ssize_t num_read;

    num_read = recvfrom(fd, buffer, max_len, 0, addr, &socket->len);
    
    if (num_read > 0) {
    	/* return data */
        return num_read;
    } else {
        if (num_read && (errno & (EWOULDBLOCK | EAGAIN)))
            return 0;    	
    }
    return -1;
    
}

static ssize_t socket_write(socket_p socket, int fd, void *data,
               size_t data_len, struct sockaddr *addr)
{
	/* make sure the socket is alive */
	if(!fd)	return -1;
	
	ssize_t write = 0;
    if ((write = sendto(fd, (char *)data, data_len, 0, 
    	         addr, socket->len)) < 0) {
    	return -1;
	} 
	return write;
}

static int socket_close(socket_p socket, int fd)
{
    if(epoll_ctl(socket->epfd, EPOLL_CTL_DEL, fd, &socket->event) == -1)
        perror("epoll_ctl");
	close(fd);
	close(socket->epfd);
	printf("Close Socket\n");
	return 0;
}

static socket_p socket_init(struct SocketSettings settings, int buf_len)
{
	socket_p socket = malloc(sizeof(*socket) + buf_len * sizeof(int));
	struct SocketSettings *setting = malloc(sizeof(*setting));
	memcpy(setting, &settings, sizeof(*setting));
    socket->settings = setting;
    if(init_pipe(&socket->pipe)) {
        free(socket);
        free(setting);
        return NULL;
    }
    return socket;
}

static int connect_server(socket_p sock)
{
    int clfd;     /* fd into transport provider */
    
    struct sockaddr_in addr;   /* address that client uses */

    /*
     *  Get a socket into UDP
     */
    if ((clfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror ("socket failed!");
        return -1;
    }
    printf("Open Socket\n");
    /*
     * Bind to an arbitrary return address.
     */
     
    bzero((char *)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(0);
    if (bind(clfd, (struct sockaddr *)&addr, sizeof(addr)) <0) {
        perror("bind failed!");
        return -1;
    }
    
    /*
     * Fill in the server's UDP/IP address
     */
    bzero((char *)&sock->servaddr, sizeof(sock->servaddr));
    sock->servaddr.sin_family = AF_INET;
    sock->servaddr.sin_port = htons(sock->settings->port);
	if(sock->settings->on_open)
        sock->settings->on_open(sock, clfd);
 	if(sock->settings->on_data) 
       sock->settings->on_data(sock, clfd);
    
    socket_close(sock, clfd);
    
    return 0;
}

/* Socket API gateway */
const struct __SOCKET_API__ Socket = {
    .start_server = start_server,
    .connect = connect_server,
    .read = socket_read,
    .write = socket_write,
    .init = socket_init,
};

/* suppress compilation warnings */
static inline ssize_t write_wrapper(int fd, const void *buf, size_t count)
{
    ssize_t s;
    if ((s = write(fd, buf, count)) < count) perror("write");
    return s;
}

static void *join_thread(pthread_t thr)
{
    void *ret;
    pthread_join(thr, &ret);
    return ret;
}

/** Destroys the ring object, releasing its memory. */
static void ring_destroy(ring_p ring)
{
    pthread_mutex_lock(&ring->lock);

    /* close pipe */
    if (ring->battery.pipe.in) {
        close(ring->battery.pipe.in);
        ring->battery.pipe.in = 0;
    }
    if (ring->battery.pipe.out) {
        close(ring->battery.pipe.out);
        ring->battery.pipe.out = 0;
    }
    if (ring->led.pipe.in) {
        close(ring->led.pipe.in);
        ring->led.pipe.in = 0;
    }
    if (ring->led.pipe.out) {
        close(ring->led.pipe.out);
        ring->led.pipe.out = 0;
    }
    if (ring->socket->pipe.in) {
        close(ring->socket->pipe.in);
        ring->socket->pipe.in = 0;
    }
    if (ring->socket->pipe.out) {
        close(ring->socket->pipe.out);
        ring->socket->pipe.out = 0;
    }
    if (ring->pipe.in) {
        close(ring->pipe.in);
        ring->pipe.in = 0;
    }
    if (ring->pipe.out) {
        close(ring->pipe.out);
        ring->pipe.out = 0;
    }
    pthread_mutex_unlock(&ring->lock);
    pthread_mutex_destroy(&ring->lock);
    free(ring);
}

/* Signal and finish */
static void ring_signal(ring_p ring)
{
    ring->run = 0;
    /* send `ring->count` number of wakeup signales.
     * data content is irrelevant. */
    write_wrapper(ring->battery.pipe.out, ring, ring->count);
    write_wrapper(ring->led.pipe.out, ring, ring->count);
    write_wrapper(ring->socket->pipe.out, ring, ring->count);   
    write_wrapper(ring->pipe.out, ring, ring->count);
}

static void ring_wait(ring_p ring)
{
    if (!ring) return;
    
    /* wake threads (just in case) by sending `ring->count`
     * number of wakeups
     */
    if (ring->battery.pipe.out)
        write_wrapper(ring->battery.pipe.out, ring, ring->count);    
    if (ring->led.pipe.out)
    	write_wrapper(ring->led.pipe.out, ring, ring->count);
    if (ring->socket->pipe.out)	
    	write_wrapper(ring->socket->pipe.out, ring, ring->count);   
    if (ring->pipe.out)
    	write_wrapper(ring->pipe.out, ring, ring->count);
    
    /* join threads */
    for (int i = 0; i < ring->count; i++) {
        join_thread(ring->threads[i]);
    }
    if (!ring->socket) {
        free(ring->socket->settings);
        free(ring->socket);
    }
    /* release queue memory and resources */
    ring_destroy(ring);
}

static void ring_finish(ring_p ring)
{
    ring_signal(ring);
    ring_wait(ring);
}

static int create_thread(pthread_t *thr,
                         void *(*thread_func)(void *),
                         void *ring)
{
    return pthread_create(thr, NULL, thread_func, ring);
}

int init_pipe(pipe_p _pipe)
{
    if (pipe((int *)_pipe)) return -1;
    fcntl(_pipe->out, F_SETFL, O_NONBLOCK | O_WRONLY);	
    return 0;
}

static ring_p ring_create(int threads, void *(**tasks)(void *))
{
    ring_p ring = malloc(sizeof(*ring) + (threads * sizeof(pthread_t)));
    ring->led.white_led_on = 0;
    ring->led.red_led_gpio = 0;
    ring->led.pipe.in = 0;
    ring->led.pipe.out = 0;
    ring->battery.voltage = 4100; /* default 4100mV) */
    ring->battery.minimum_vol = 3500; /* default 3500mV */
    ring->battery.charging = 0;
    ring->battery.pipe.in = 0;
    ring->battery.pipe.out = 0;
    ring->pipe.in = 0;
    ring->pipe.out = 0;
    ring->press_button = 0;
        
    if (pthread_mutex_init(&(ring->lock), NULL)) {
        free(ring);
        return NULL;
    }
 	if(init_pipe(&ring->battery.pipe)) goto end;
    if(init_pipe(&ring->led.pipe)) goto end;
    if(init_pipe(&ring->pipe)) goto end;	
    ring->run = 1;
    /* create threads */
    for (ring->count = 0; ring->count < threads; ring->count++) {
        if (create_thread(ring->threads + ring->count,
                          *tasks++, ring)) {
            /* signal */
            ring_signal(ring);
            /* wait for threads and destroy object */
            ring_wait(ring);
            /* return error */
            return NULL;
        };
    }
    return ring;
end:
	free(ring);
    return NULL;    
}

/* Task Management - add a task and perform all tasks in queue */

static int ring_run(ring_p ring, pipe_p pipe)
{
    if (!ring) return -1;

    /* wake up any sleeping threads
     * any activated threads will ask to require the mutex
     * as soon as we write.
     * we need to unlock before we write, or we will have excess
     * context switches.
     */
    write_wrapper(pipe->out, ring, 1);
    return 0;
}


/* API gateway */
const struct __THREAD_API__ Thread = {
    .create = ring_create,
    .signal = ring_signal,
    .wait = ring_wait,
    .finish = ring_finish,
    .run = ring_run,
};
