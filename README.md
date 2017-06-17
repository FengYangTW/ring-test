# Ring Test 

-----------------------------------------------------------------------------------------------------------------------
System Requirements:

1.) An embedded electronic device with the following peripherals:
      a.) 1 spring-loaded button that may be pushed and held by a user.
      b.) 1 Ethernet interface. Assume that the driver is functional, and the device has a valid IP address and is Internet-accessible. BSD sockets are available to use in the application.
      c.) 1 white LED, controllable by GPIO.
      d.) 1 red LED, controllable by GPIO.
      e.) 1 Battery monitor, read via ADC. Units read are in units of millivolts.
      f.) 1 USB battery charger interface, which may be plugged and unplugged at any time to charge or discharge the battery. Assume that all battery charging functionality is independent and properly working.

2.) When a user pushes the button, the white LED shall illuminate. The white LED shall be illuminated for as long as the button is held down. When the button is released, the white LED shall turn off.

3.) Network Application
      a.) While the button is pushed, a UDP socket shall be opened with a server at the domain "test.ring.com" on port 13469. Every 1 second, the device shall send a packet of 2 bytes to the server with a counter, starting at value 0 and increasing by 1 every packet.
      b.) For every 2-byte UDP packet sent to the server, the server shall return back a 2-byte packet on the same port echoing the counter. If no echo is received within 500ms or the value returned from the server is not equal to the value sent from the device, the device shall re-send the current value. This shall continue until the correct value is received from the server, at which point the device will increment the counter and proceed as normal.
      c.) When the button is released, the socket shall be closed and the counter shall be reset.

4.) Battery Operation
      a.) While the battery voltage is <3.5V, the system shall be put in a non-functional state, meaning the white LED shall not illuminate and the Network Application shall not be executed, even if the button is pushed.
      b.) While the battery voltage is <3.5V, the red LED shall blink at a rate of 2Hz with a 25% duty cycle.
      c.) If the battery voltage returns to an operable level (>= 3.5V), the red LED shall stop blinking and operation shall resume as normal.
 
-----------------------------------------------------------------------------------------------------------------------
It is simple to use the API to build UDP service.
It is based on Protocol structure and callbacks, so that we can dynamically
change protocols, system service and simulate user behavior.

The program consists of the following components:

* [`Thread`](ring.h): A native POSIX thread pool.
  - It uses a combination of a pipe (for wakeup signals)
  - Generate thread functions to handle system event.
* [`socket`](ring.h): UDP server/Client construction library
  - socket manages everything that makes a UDP client/server run and setting up
    the initial protocol.
  - Supported network events: ready to opened, read/write, closed and exit.
* [`ring`](ring.h): system construction.
  - Struct ring describes devices information, including baatery, LED, UDP client
    socket.
* [`ring-udp-echo`](ring-udp-echo.c): Simple UDP server.
  - server made timeout in the third packaet and error data in sixth packet 
    (counter starts at 0) every 20 packets.  
* [`network_task`](test-ring.c): a UDP client to manager read/write behavior.
  -  Linux epoll system call abstraction
* [`led_task`](test-ring.c): LED event hanlder.
* [`battery_task`](test-ring.c): Battery event hanlder.
* [`event_task`](test-ring.c): Simulate user behavior to triger various event.
  
Here is a simple example to creare UDP echo server:
```c
#include "ring.h"
                            
/* simple echo, the main callback */
static void on_data(socket_p socket, int srvfd)
{
	ssize_t num_read;
    char buff[BUF_SIZE];
    int event_counter = 0;
    /* Receive datagrams and return copies to senders */
    socket->len = sizeof(struct sockaddr_storage);
    while ((num_read = Socket.read(socket, srvfd, buff, BUF_SIZE,
                       (struct sockaddr *)&socket->claddr)) > 0) {
    	if(event_counter % 20 == 3)
    	    usleep(600000); /* Triger event of timeout*/
    	if(event_counter % 20 == 6)
    	    buff[0] += 1; /* Triger event of error data*/
    	    
    	/* since the data is stack allocated, we'll write a copy */
            Socket.write(socket, srvfd, buff, num_read, 
                        (struct sockaddr *)&socket->claddr);
        
    	if (!memcmp(buff, "bye", 3)) {
            /* close the connection automatically AFTER buffer was sent */
            Socket.close(socket, srvfd);
        }
        socket->len = sizeof(struct sockaddr_storage);
        event_counter++;
    }
}

int main()
{
    printf("Simple UDP Echo Server on \"test.ring.com\" port 13469\n");
    /* create the echo protocol object with the settings we provide.*/
	Socket.start_server((struct SocketSettings){
	    .is_udp_server = 1, 
	    .service = "echo",
	    .port = 13469,
	    .on_data = on_data,
	    });
}
```
![image](https://github.com/FengYangTW/ring-test/blob/master/ring-flow.JPG?raw=true)
