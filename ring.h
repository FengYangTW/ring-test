#ifndef _RING_H
#define _RING_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <pthread.h>

/* To get NI_MAXHOST and NI_MAXSERV
      definitions from <netdb.h> */
#ifndef _BSD_SOURCE
#define _BSD_SOURCE             
#endif 

#define BUF_SIZE 1024

/* Suggested length for string buffer that caller
 * should pass to inetAddressStr(). Must be greater
 * than (NI_MAXHOST + NI_MAXSERV + 4) 
 */
#define IS_ADDR_STR_LEN 4096
                                   
/* a pointer to a RING object */                                   
typedef struct RING *ring_p;

/* a pointer to a Soecket object */
typedef struct Socket *socket_p;

/* a pointer to a Soecket object */
typedef struct pipe *pipe_p;

/** The pipe used for thread wakeup */
struct pipe {
    int in;  /**< read incoming data (opaque data), used for wakeup */
    int out; /**< write opaque data (single byte),
                  used for wakeup signaling */
};
 
/* The server data object container */
struct Socket {
    struct SocketSettings *settings;
    struct sockaddr_in servaddr; /* the server's full addr */
    socklen_t len;
    struct sockaddr_storage claddr; /* the client's addr */
    int epfd;
    struct epoll_event event;
    ring_p ring;
    struct pipe pipe; /* The pipe used for socket thread wake up*/
    uint16_t buff[];
};

/*
 * The Socket Settings
 *
 * These settings will be used to setup socket behavior. Missing settings
 * will be filled in with default values. 
 */
 
struct SocketSettings {
    int is_udp_server; /*server:1 client:0*/
    char *service; /*a string to identify the socket's service*/
    int port; /* the port to listen to. default to 8080. */
    char *host;
    char *address; /* the address to bind to. Default to NULL
                        (all localhost addresses). */
    int timeout_ms;  /**< set the timeout for receiving data.Default to 500ms. */
    ring_p ring;
    void (*on_open)(socket_p, int fd); /* called when a connection is opened. */
    void (*on_data)(socket_p,int fd); /* called when a data is available. */
    void (*on_close)(socket_p, int fd); /* called when connection was closed. */
};	

/*
 * A simple thread pool utilizing POSIX threads
 *
 * The thread pool can take any function and split it across a set number
 * of threads. It uses a combination of a pipe (for wakeup signals) and
 * mutexes (for managing the task queue) to give a basic layer of protection
 * to any function implementation.
 *
 */
extern const struct __THREAD_API__ {
    /*
     * Create a new ring object (thread pool)
     *        a pointer using the Async pointer type.
     * param1 threads the number of new threads to be initialized, 
     * param2 array of pointer to function returning pointer to function returning pointer to void
     */
    ring_p (*create)(int threads, void *(**tasks)(void *));

    /*
     * Signal an Thread object to finish up.
     */
    void (*signal)(ring_p);

    /*
     * \brief Waits for an ring object to finish up (joins all the threads
     *        in the thread pool).
     *
     * This function will wait forever or until a signal is received and
     * all the tasks in the queue have been processed.
     */
    void (*wait)(ring_p);

    /*
     * Schedules a task to be performed by an ring thread pool group.
     */
    int (*run)(ring_p, pipe_p);

    /**
     * Both signals for an ring object to finish up and waits
     *        for it to finish.
     *
     * This is akin to calling both `signal` and `wait` in succession:
     *   - Async.signal(async);
     *   - Async.wait(async);
     *
     * @return  0 on success.
     * @return -1 on error.
     */
    void (*finish)(ring_p);
} Thread;

/**
* Socket API
*
* The simple design of Socket API is based on Protocol structure and callbacks,
* so that we use this to create UDP server or client
* The API and helper functions described here are accessed using the global
* `Socket` object.
*/
extern const struct __SOCKET_API__ {
	/* called when the Server starts */ 
    int (*start_server)(struct SocketSettings);
    
    /* connect to server */
    int (*connect)(socket_p);
	/*
     * Read up to `max_len` of data from a socket.
     *
     * the data is stored in the `buffer` and the number of bytes received
     * is returned.
     *
     * return -1 if an error was raised and the connection was closed.
     * return the number of bytes written to the buffer.
     * return 0 if no data was available.
     */
    ssize_t (*read)(socket_p socket, int sockfd, void *buffer,
              size_t max_len, struct sockaddr *);

    /*
     * Copy and write data to the socket.
     *
     * return 0 on success. success means that the data is in a buffer
     *           waiting to be written. If the socket is forced to close
     *           at this point, the buffer will be destroyed.
     * return -1 on error.
     */
    ssize_t (*write)(socket_p socket, int sockfd, void *data, size_t len,
               struct sockaddr *);
               
   /* Close the connection. */ 		
    int (*close)(socket_p socket, int fd);
    
   /* Initialize socket*/
    socket_p (*init)(struct SocketSettings settings, int buf_len);
} Socket;

struct RING {
    struct {
        int voltage; /* read via ADC. Units read are in units of millivolts. */
        /*
         * USB battery charger interface, which may be plugged and unplugged 
         * at any time to charge or discharge the battery. 
         * Assume that all battery charging functionality is independent 
         * and properly working. 
         */    
        int charging;
        int minimum_vol; /* While the battery voltage is < minimum_vol, the system shall be put in a non-functional state */
	    struct pipe pipe; /* The pipe used for battery thread wake up*/ 
    } battery;
    struct {
        int white_led_on; /* white LED, controllable by GPIO */
        volatile int red_led_gpio; /* red LED GPIO, controllable by GPIO */
	    struct pipe pipe; /* The pipe used for led thread wake up*/ 
    } led;
    socket_p socket;
    int press_button; /* spring-loaded button that may be pushed and held by a user. */
    pthread_mutex_t lock; /**< a mutex for data integrity */
    struct pipe pipe; /* The pipe used for main func wake up*/
    int count; /**< the number of initialized threads */    
    unsigned run : 1; /**< the running flag */
    pthread_t threads[]; /** the thread pool */
};

#endif
