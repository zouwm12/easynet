#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include "easynet.h"

void echo_read(Enet_Client_t* client,int nread,Enet_buf_t *buf)
{
	if (nread > 0)
	{
		printf("echo_read-------------------------%s,len:%d\n",buf->base,buf->len);		
		
		//write
		Enet_buf_t write_buf = {0};
		easynet_init_buf(&write_buf,buf->len);
		memcpy(write_buf.base,buf->base,buf->len);
		easynet_stream_write(client,&write_buf);
		easynet_uninit_buf(&write_buf);

		free(buf->base);
		return;
    }
    if (nread < 0)
	{
		if (nread != EOF)
		{
        	fprintf(stderr, "Read error %d\n", nread);
			printf("error------------------------  %d:%s\n",errno,strerror(errno));
		}
		easynet_client_stop(client->server,client);
		easynet_client_destroy(client);
		
		printf("client close-------------nread:%d\n",nread);
    }

    free(buf->base);
}

void on_new_connection(Enet_Stream_t* server, int status)
{
	if (status < 0) 
	{
        fprintf(stderr, "New connection error %d\n", status);
        // error!
        return;
    }

	Enet_Client_t *client = (Enet_Client_t *)malloc(sizeof(Enet_Client_t));
	if(!client)
		return;
	easynet_client_init(client,server->accept_fd);
	printf("on_new_connection---------------------fd:%d\n",client->sockfd);
	easynet_client_start(server,client,echo_read);
}

int main()
{
	Enet_Stream_t server;

	signal(SIGPIPE,SIG_IGN);

	easynet_init_stream(&server);
	easynet_stream_bind(&server,NULL,8000);
	easynet_stream_listen(&server,10,on_new_connection);
	easynet_stream_loop_run(&server);

	return 0;
}
