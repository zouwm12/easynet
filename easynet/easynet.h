#ifndef EASY_NET_H
#define EASY_NET_H

#include <sys/select.h>
#include "queue.h"

typedef char CHAR;
typedef unsigned char BYTE;

enum{
	ENET_STATE_CLOSED,
	ENET_STATE_LISTENING,
	ENET_STATE_CONNECTING,
	ENET_STATE_CONNECTED,
};

enum{
	ENET_FLAG_READ_ONLY,
	ENET_FLAG_WRITE_ONLY,
	ENET_FLAG_READ_WRITE,
};

typedef struct Enet_buf_t {
  char* base;
  int len;
}Enet_buf_t;


typedef struct Enet_Stream Enet_Stream_t;
typedef struct Enet_Client Enet_Client_t;

typedef void (*stream_connection_cb)(Enet_Stream_t* server, int status);
typedef void (*stream_request_cb)(Enet_Client_t *client,int status);
typedef void (*client_read_cb)(Enet_Client_t* client,int nread,Enet_buf_t* buf);


struct Enet_Client
{
	int sockfd;
	BYTE flags;
	CHAR state;
	stream_request_cb request_cb;
	client_read_cb read_cb;
	Enet_Stream_t *server;

	void *write_req_queue[2];
	void *destroy_queue[2];
};

struct Enet_Stream
{
	int sockfd;
	fd_set read_fdset;
	fd_set write_fdset;
	BYTE flags;
	CHAR state;
	BYTE loop;

	//client
	int accept_fd;
	void ** watchers;
	unsigned int nwatchers;
	unsigned int nfds;
	void *destroy_queue[2];

	stream_connection_cb connection_cb;
};

typedef struct Enet_write_s
{
	Enet_buf_t buf;
	int send_offset;

	void *write_req_queue[2];
	
}Enet_write_t;


void easynet_init_stream(Enet_Stream_t *stream);
int easynet_stream_bind(Enet_Stream_t *stream,char *ip,int port);
int easynet_stream_listen(Enet_Stream_t *stream,int backlog,stream_connection_cb cb);
int easynet_stream_loop_run(Enet_Stream_t *stream);
void easynet_stream_loop_stop(Enet_Stream_t *stream);

void easynet_client_init(Enet_Client_t *client,int sockfd);
int easynet_client_start(Enet_Stream_t *server,Enet_Client_t *client,client_read_cb cb);
int easynet_client_stop(Enet_Stream_t *server,Enet_Client_t *client);
void easynet_client_destroy(Enet_Client_t *client);

//write
int easynet_init_buf(Enet_buf_t *buf,unsigned int write_size);
void easynet_uninit_buf(Enet_buf_t *buf);

int easynet_stream_write(Enet_Client_t *client,Enet_buf_t *data);

#endif
