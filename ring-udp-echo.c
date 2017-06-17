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
	Socket.start_server((struct SocketSettings) {
	    .is_udp_server = 1, 
	    .service = "echo",
	    .port = 13469,
	    .on_data = on_data,
	    });
}