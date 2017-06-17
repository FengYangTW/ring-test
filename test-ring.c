#include <sched.h>
#include "ring.h"

#ifndef MAX_EVENTS
#define MAX_EVENTS 32
#endif


static void on_open(socket_p socket, int srvfd)
{
	memset(socket->buff,0, BUF_SIZE);
	socket->epfd = epoll_create1(0);
    if(socket->epfd == -1) perror("epoll_create");
    socket->event.data.fd = srvfd;
    socket->event.events = EPOLLIN; /* Only interested in input events */
    
    if(epoll_ctl(socket->epfd, EPOLL_CTL_ADD, srvfd, &socket->event) == -1)
        perror("epoll_ctl");
}
/*
 * For every 2-byte UDP packet sent to the server, 
 * the server shall return back a 2-byte packet on 
 * the same port echoing the counter. 
 * If no echo is received within 500ms or the value 
 * returned from the server is not equal to the value sent from the device, 
 * the device shall re-send the current value. 
 * This shall continue until the correct value is received from the server, 
 * at which point the device will increment the counter and proceed as normal.
 */
static void on_data(socket_p socket, int srvfd)
{
    /* the device shall send a packet of 2 bytes to the server with a counter */
    ssize_t num_rw = 2; 
    uint16_t w_data = 0, *buff = socket->buff;
    /* Receive datagrams and return copies to senders */
	ring_p ring = socket->ring;
    while (ring->battery.voltage >= ring->battery.minimum_vol && ring->press_button && ring->run == 1) {
        socket->len = sizeof(socket->servaddr);	
    	/* Write data to Server */
    	if (num_rw != Socket.write(socket, srvfd, &w_data,
    		          2, (struct sockaddr*)&socket->servaddr)) {
    		perror("write cnt != 2");
    		continue;
        }
        
        if(epoll_wait(socket->epfd, &socket->event, MAX_EVENTS,
                      socket->settings->timeout_ms) <= 0) {
            printf("\n500ms timeout to re-send package\n");
       	    /* Empty buff */
    	    Socket.read(socket, srvfd, buff, 
    		          2, (struct sockaddr*)&socket->servaddr);
            continue;
        }
        
        /* Read data from Server */
    	if (num_rw != Socket.read(socket, srvfd, buff, 
    		          2, (struct sockaddr*)&socket->servaddr)) {
    		perror("read cnt != 2");
    		continue;
    	}
    	
    	printf("%d ",(uint16_t)*buff);
    	fflush(stdout);
        
        /* Compare 2 bytes data */
    	if(*buff == w_data) {
    	    w_data++;
    	} else {
    	    printf("\ncompare failed, re-send value\n");
    	    continue;
    	}
    	
    	if(w_data == 65535) printf("\ncounter overflow\n");
    	sleep(1);/* send a packet of 2 bytes */
    }
    
}

static void * network_task(void *arg)
{
    /* setup signal and thread's local-storage async variable. */
    ring_p ring = arg;
    char sig_buf;
    
    /* pause for signal for as long as we're active. */
    while (ring->run && (read(ring->socket->pipe.in, &sig_buf, 1) >= 0)) {
        if(ring->battery.voltage >= ring->battery.minimum_vol)
            Socket.connect(ring->socket);
        sched_yield();
    }
    
    return NULL;
}

static void on_close(socket_p socket, int sockfd)
{
    
}

/* In order to test, suppose charging increased by 100mV per second 
 * Maximum voltage: 4200mV, Minimum voltage: 3200mV
 * While current voltage is as low as minimum voltage, 
 * program will close all the threads and exit itself. 
 */
static void * battery_task(void *arg)
{
    /* setup signal and thread's local-storage async variable. */
    ring_p ring = arg;
    char sig_buf;

    /* pause for signal for as long as we're active. */
    while (ring->run && (read(ring->battery.pipe.in, &sig_buf, 1) >= 0)) {
        while( 3200 < ring->battery.voltage && (ring->battery.voltage < 4200 || !ring->battery.charging)) {
            if(ring->battery.charging)
                ring->battery.voltage += 100;
            else        
                ring->battery.voltage -= 100;
            printf("Battery voltage:%dmV\n",ring->battery.voltage);
            if(ring->battery.voltage < ring->battery.minimum_vol) { /* wake up socket and led tasks */
                Thread.run(ring,&(ring->led.pipe));
	            Thread.run(ring,&(ring->socket->pipe));
            }
            sleep(1);
        }
        if(ring->battery.voltage <= 3200) {/* shutdown device */
            printf("Low battery, power off device\n");
            Thread.finish(ring);
        }
        fflush(stdout);
        sched_yield();
    }
    printf("%s exit\n",__func__);
    return NULL;
}

void red_led_blink(ring_p ring, int hz, float duty)
{
    float hi = duty / hz; /* calculate pull-up time */
    float low = (1 - duty) / hz; /* calculate pull-down time */
    printf("Red LED is blinking.\n");
    printf("GPIO for red LED is in a cycle of %.3fs high and %.3fs low.\n", hi, low);
    do {
        ring->led.red_led_gpio = 1;
        usleep(hi * 1000); // sleep ms
        ring->led.red_led_gpio = 0;
        usleep(low * 1000); // sleep ms
    } while(ring->battery.voltage < ring->battery.minimum_vol && ring->run == 1);
    printf("Red LED stops blinking\n");
}

/*
 * While the battery voltage is <3.5V,  white LED shall not illuminate, 
 * even if the button is pushed. While the battery voltage is <3.5V, 
 * the red LED shall blink at a rate of 2Hz with a 25% duty cycle.
 */

static void * led_task(void *arg)
{
    /* setup signal and thread's local-storage async variable. */
    ring_p ring = arg;
    char sig_buf;
    int old_stae = 0;
    /* pause for signal for as long as we're active. */
    
    while (ring->run && (read(ring->led.pipe.in, &sig_buf, 1) >= 0)) {
        if(ring->battery.voltage < ring->battery.minimum_vol) {
            ring->led.white_led_on = 0;
            red_led_blink(ring, 2, 0.25);            
        } 
        if(ring->press_button) {
            ring->led.white_led_on = 1;
            if(old_stae != ring->led.white_led_on)
                printf("White LED illuminated\n");
        } else {
            ring->led.white_led_on = 0;
            if(old_stae != ring->led.white_led_on)
                printf("White LED didn't illuminate\n");
        }
        old_stae = ring->led.white_led_on;
        fflush(stdout);
        sched_yield();
        //printf("ring->press_button:%d\n",ring->press_button);
    }
    printf("%s exit\n",__func__);
    return NULL;
}

void press_button(ring_p ring)
{    
    ring->press_button = 1;
    printf("\nPush button\n");
	Thread.run(ring,&(ring->led.pipe));
	Thread.run(ring,&(ring->socket->pipe));
	
}

void release_button(ring_p ring)
{    
    ring->press_button = 0;
    printf("\nRelease button\n");
    Thread.run(ring,&(ring->led.pipe));
	Thread.run(ring,&(ring->socket->pipe));
}

void charge_on(ring_p ring, int on)
{
    ring->battery.charging = on;
    printf("charging %s\n", on ? "on" : "off");
    Thread.run(ring,&(ring->battery.pipe));
}
static void * event_task(void *arg)
{
    /* setup signal and thread's local-storage async variable. */
    ring_p ring = arg;
    
    /* pause for signal for as long as we're active. */
    while (ring->run) {
        charge_on(ring, 1);
		press_button(ring);
		sleep(5);
		release_button(ring);
		
		charge_on(ring, 0);
		
		sleep(3);
		press_button(ring);
		sleep(4);
		release_button(ring);
		sleep(2);
    }
    printf("%s exit\n",__func__);
    return NULL;
}


int main()
{
	socket_p socket = 
	Socket.init((struct SocketSettings) {
	            .port = 13469,
	            .host = "test.ring.com",
	            .on_open = on_open,
	            .on_data = on_data,
	            .on_close = on_close,
	            .timeout_ms = 500,
	            }, BUF_SIZE);
	        
    void * (*worker_thread_func[])(void *arg) = { 
        network_task, battery_task, led_task, event_task} ;
    ring_p ring = Thread.create(sizeof(worker_thread_func)/ sizeof(void *), 
                  worker_thread_func);
    ring->socket = socket;
    socket->ring = ring;
    
    {
        char sig_buf;
        while(ring->run && read(ring->pipe.in, &sig_buf, 1) >= 0);
        printf("Bye\n");
    }
    return 0;
}
