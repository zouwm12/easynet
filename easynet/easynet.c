#include "easynet.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>


#define __ERR(x) (-(x))

int easynet_stream_write_block_data(Enet_Client_t *client);

void easynet_init_stream(Enet_Stream_t *stream)
{
	if(!stream)
		return;

	memset(stream,0,sizeof(Enet_Stream_t));
	stream->flags = ENET_FLAG_READ_WRITE;
	stream->state = ENET_STATE_CLOSED;
	QUEUE_INIT(&stream->destroy_queue);
	stream->loop = 1;
}

int fcntl_nonblock(int fd, int set) {
  int flags;
  int r;

  do
    r = fcntl(fd, F_GETFL);
  while (r == -1 && errno == EINTR);

  if (r == -1)
    return __ERR(errno);

  /* Bail out now if already set/clear. */
  if (!!(r & O_NONBLOCK) == !!set)
    return 0;

  if (set)
    flags = r | O_NONBLOCK;
  else
    flags = r & ~O_NONBLOCK;

  do
    r = fcntl(fd, F_SETFL, flags);
  while (r == -1 && errno == EINTR);

  if (r)
    return __ERR(errno);

  return 0;
}


int fcntl_cloexec(int fd, int set) {
  int flags;
  int r;

  do
    r = fcntl(fd, F_GETFD);
  while (r == -1 && errno == EINTR);

  if (r == -1)
    return __ERR(errno);

  /* Bail out now if already set/clear. */
  if (!!(r & FD_CLOEXEC) == !!set)
    return 0;

  if (set)
    flags = r | FD_CLOEXEC;
  else
    flags = r & ~FD_CLOEXEC;

  do
    r = fcntl(fd, F_SETFD, flags);
  while (r == -1 && errno == EINTR);

  if (r)
    return __ERR(errno);

  return 0;
}

static int create_server_socket(Enet_Stream_t *stream)
{
	if(!stream)
		return -1;

	int sock_opt = 1;

	stream->sockfd = socket(AF_INET,SOCK_STREAM,0);
	fcntl_nonblock(stream->sockfd,1);
	fcntl_cloexec(stream->sockfd,1);
	if ((setsockopt(stream->sockfd, SOL_SOCKET, SO_REUSEADDR, (void *) &sock_opt,
                    sizeof (sock_opt))) == -1)
        printf("setsockopt failed\n");

	return 0;
}

int easynet_stream_bind(Enet_Stream_t *stream,char *ip,int port)
{
	struct sockaddr_in server_sockaddr;
	create_server_socket(stream);
    memset(&server_sockaddr, 0, sizeof server_sockaddr);
	server_sockaddr.sin_family = AF_INET;
	if (ip != NULL)
        inet_aton(ip, &server_sockaddr.sin_addr);
    else
        server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_sockaddr.sin_port = htons(port);

	return bind(stream->sockfd,(struct sockaddr *)&server_sockaddr,sizeof(server_sockaddr));
}

int easynet_stream_listen(Enet_Stream_t *stream,int backlog,stream_connection_cb cb)
{
	stream->connection_cb = cb;
	return listen(stream->sockfd,backlog);
}

int easynet_client_is_block(Enet_Client_t *stream)
{
	return QUEUE_EMPTY(&stream->write_req_queue)?0:1;
}

static void free_write_queue(Enet_Client_t *client)
{
	QUEUE* q;
	if(!client)
		return;
	while(!QUEUE_EMPTY(&client->write_req_queue))
	{
		q = QUEUE_HEAD(&client->write_req_queue);
		QUEUE_REMOVE(q);
		QUEUE_INIT(q);
		Enet_write_t *writer = QUEUE_DATA(q,Enet_write_t,write_req_queue);
		if(writer->buf.base)
			free(writer->buf.base);
	}
}

static void destroy_all_alive_client(Enet_Stream_t *stream)
{
	int i = 0;
	for(i = 0;i < stream->nwatchers;i++)
	{
		Enet_Client_t *client = (Enet_Client_t *)stream->watchers[i];
		if(client)
		{
			if(client->sockfd > 0)
				close(client->sockfd);
			
			free_write_queue(client);
			free(client);
		}
	}
	if(stream->watchers)
	{
		free(stream->watchers);
		stream->nwatchers = 0;
		stream->nfds = 0;
	}
}

static void destroy_client(Enet_Stream_t *stream)
{
	QUEUE* q;
	while(!QUEUE_EMPTY(&stream->destroy_queue))
	{
		q = QUEUE_HEAD(&stream->destroy_queue);
		QUEUE_REMOVE(q);
		QUEUE_INIT(q);
		Enet_Client_t *client = QUEUE_DATA(q,Enet_Client_t,destroy_queue);
		if(client->sockfd > 0)
		{
			close(client->sockfd);
			client->sockfd = -1;
		}
		free_write_queue(client);
		free(client);
	}
}

void easynet_process_request(Enet_Stream_t *stream)
{
	destroy_client(stream);
}

static void easynet_stream_accept(Enet_Stream_t *stream)
{
	int err;
	struct sockaddr clientAddr;
	socklen_t addrlen = sizeof(clientAddr);

	stream->accept_fd = -1;
	err = accept(stream->sockfd,&clientAddr,&addrlen);
	if(err < 0)
	{
		if (err == EAGAIN || err == EWOULDBLOCK)
			return;  /* Not an error. */

		if (err == ECONNABORTED)
			return;  /* Ignore. Nothing we can do about that. */

		if(stream->connection_cb)
			stream->connection_cb(stream,err);
		return;
	}
	fcntl_nonblock(err,1);
	fcntl_cloexec(err,1);
	stream->accept_fd = err;
	if(stream->connection_cb)
		stream->connection_cb(stream,0);
}

static void easynet_stream_read(Enet_Client_t *client)
{
	Enet_buf_t buf = {0};
	int nread = 0;

	buf.len = sizeof(char) * 64 * 1024;
	buf.base = (char *)malloc(buf.len);
	if(!buf.base)
	{
		printf("easynet_stream_read-------malloc failed\n");
		client->read_cb(client, __ERR(ENOBUFS), &buf);
		return;
	}

	do {
		nread = read(client->sockfd,buf.base,buf.len);
	}
	while (nread < 0 && errno == EINTR);

	if(nread < 0)
	{
		if(errno == EAGAIN || errno == EWOULDBLOCK)
		{
			client->read_cb(client,0,&buf);
		}
		else
		{
			client->read_cb(client,__ERR(errno),&buf);
		}
		return;
	}
	else if(nread == 0)
	{
		printf("EOF------------------------\n");
		client->read_cb(client, EOF, &buf);
	}
	else
	{
		buf.len = nread;
		client->read_cb(client,buf.len,&buf);
	}
}

int easynet_stream_loop_run(Enet_Stream_t *stream)
{
	struct timeval req_timeout = {0,200000};
	struct timeval *select_time = NULL;
	int i = 0;
	int client_num = 0;
	BYTE block_flag = 0;
	Enet_Client_t *client = NULL;
	while(stream->loop)
	{
		//deal other event
		easynet_process_request(stream);
	
		FD_ZERO(&stream->read_fdset);
		FD_ZERO(&stream->write_fdset);
		FD_SET(stream->sockfd,&stream->read_fdset);
		client_num = 0;
		block_flag = 0;

		for(i = 0;i < stream->nwatchers;i++)
		{
			client = (Enet_Client_t *)stream->watchers[i];
			if(client && client->sockfd > 0)
			{
				FD_SET(client->sockfd,&stream->read_fdset);
				if(easynet_client_is_block(client))
				{
					FD_SET(client->sockfd,&stream->write_fdset);
					block_flag = 1;
				}
				client_num++;
			}
			if(client_num >= stream->nfds)
				break;
		}

		if(block_flag == 1)
			select_time = NULL;
		else
			select_time = &req_timeout;
			
		if(select(1024,&stream->read_fdset,&stream->write_fdset,NULL,select_time) > 0)
		{
			if(FD_ISSET(stream->sockfd,&stream->read_fdset))
			{
				easynet_stream_accept(stream);
			}
			
			for(i = 0;i < stream->nwatchers;i++)
			{
				client = (Enet_Client_t *)stream->watchers[i];
				if(!client || client->sockfd <= 0)
					continue;
				if(FD_ISSET(client->sockfd,&stream->read_fdset))
				{
					easynet_stream_read(client);
				}
				if(FD_ISSET(client->sockfd,&stream->write_fdset))
				{
					easynet_stream_write_block_data(client);
				}
			}			
		}
	}

	//quit
	destroy_client(stream);
	destroy_all_alive_client(stream);
	return 0;
}

void easynet_stream_loop_stop(Enet_Stream_t *stream)
{
	if(stream)
		stream->loop = 0;
}

static unsigned int next_power_of_two(unsigned int val) {
  val -= 1;
  val |= val >> 1;
  val |= val >> 2;
  val |= val >> 4;
  val |= val >> 8;
  val |= val >> 16;
  val += 1;
  return val;
}

static void maybe_resize(Enet_Stream_t* stream, unsigned int len) {
  void** watchers;
  void* fake_watcher_list;
  void* fake_watcher_count;
  unsigned int nwatchers;
  unsigned int i;

  if (len <= stream->nwatchers)
    return;

  /* Preserve fake watcher list and count at the end of the watchers */
  if (stream->watchers != NULL) {
    fake_watcher_list = stream->watchers[stream->nwatchers];
    fake_watcher_count = stream->watchers[stream->nwatchers + 1];
  } else {
    fake_watcher_list = NULL;
    fake_watcher_count = NULL;
  }

  nwatchers = next_power_of_two(len + 2) - 2;
  watchers = realloc(stream->watchers,
                         (nwatchers + 2) * sizeof(stream->watchers[0]));

  if (watchers == NULL)
    abort();
  for (i = stream->nwatchers; i < nwatchers; i++)
    watchers[i] = NULL;
  watchers[nwatchers] = fake_watcher_list;
  watchers[nwatchers + 1] = fake_watcher_count;

  stream->watchers = watchers;
  stream->nwatchers = nwatchers;
}

void easynet_client_init(Enet_Client_t *client,int sockfd)
{
	memset(client,0,sizeof(Enet_Client_t));
	client->sockfd = sockfd;
	QUEUE_INIT(&client->write_req_queue);
	QUEUE_INIT(&client->destroy_queue);
}

int easynet_client_start(Enet_Stream_t *server,Enet_Client_t *client,client_read_cb cb)
{
	client->read_cb = cb;
	client->server = server;
	maybe_resize(server,client->sockfd + 1);

	if (server->watchers[client->sockfd] == NULL)
	{
		server->watchers[client->sockfd] = client;
		server->nfds++;
	}
	
	return 0;
}

int easynet_client_stop(Enet_Stream_t *stream,Enet_Client_t *client)
{
	int i = 0;
	for(i = 0;i < stream->nwatchers;i++)
	{
		Enet_Client_t *watcher = (Enet_Client_t *)stream->watchers[i];
		if(!watcher)
			continue;
		if(client->sockfd == watcher->sockfd)
		{
			stream->watchers[i] = NULL;
			stream->nfds--;
			return 0;
		}
	}
	return -1;
}

void easynet_client_destroy(Enet_Client_t *client)
{
	QUEUE_INSERT_TAIL(&client->server->destroy_queue, &client->destroy_queue);
}

static int easynet_init_write(Enet_write_t *writer,unsigned int write_size)
{
	easynet_init_buf(&writer->buf,write_size);
	QUEUE_INIT(&writer->write_req_queue);
	return 0;
}

static void easynet_uninit_write(Enet_write_t *writer)
{
	easynet_uninit_buf(&writer->buf);
}

int easynet_stream_write(Enet_Client_t *client,Enet_buf_t *data)
{
	if(!client || !data)
		return -1;
	
	int n = 0;
	Enet_write_t *writer = (Enet_write_t *)malloc(sizeof(Enet_write_t));
	easynet_init_write(writer,data->len);
	Enet_buf_t *buf = &writer->buf;
	memcpy(buf->base,data->base,data->len);
	
	do {
		n = write(client->sockfd,buf->base,buf->len);
	}while(n == -1 && errno == EINTR);

	printf("easynet_stream_write--------------------n:%d\n",n);

	if(n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
	{
		//block
		/* Append the request to write_queue. */
		QUEUE_INSERT_TAIL(&client->write_req_queue, &writer->write_req_queue);
	}

	if(n != writer->buf.len)
	{
		//some data left
		writer->send_offset = n;
		buf->len -= n;
		QUEUE_INSERT_TAIL(&client->write_req_queue, &writer->write_req_queue);
	}
	else
	{
		easynet_uninit_write(writer);
		free(writer);
	}

	return 0;
}

int easynet_stream_write_block_data(Enet_Client_t *client)
{
	QUEUE* q;
	Enet_write_t *writer;
	int n = 0;
	
	while(!QUEUE_EMPTY(&client->write_req_queue))
	{
		q = QUEUE_HEAD(&client->write_req_queue);

		writer = QUEUE_DATA(q,Enet_write_t,write_req_queue);

		Enet_buf_t *buf = &writer->buf;
		printf("easynet_stream_write_block_data---fd:%d,offset:%d,len:%d\n",
			client->sockfd,writer->send_offset,buf->len);
		do {
			n = write(client->sockfd,buf->base + writer->send_offset,buf->len);
		}while(n == -1 && errno == EINTR);

		if(n == -1)
		{
			//still block
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			else
			{
				client->read_cb(client, __ERR(errno), buf);
				break;
			}
		}

		printf("easynet_stream_write_block_data------------n:%d\n",n);
		if(n == buf->len)
		{
			//send over			
			QUEUE_REMOVE(q);
			QUEUE_INIT(q);
			easynet_uninit_write(writer);
			free(writer);
			writer = NULL;
		}
		else
		{
			buf->len -= n;
			writer->send_offset += n;
			break;
		}	
	}
	return 0;
}

int easynet_init_buf(Enet_buf_t *buf,unsigned int write_size)
{
	if(!buf)
		return -1;

	buf->base = (char *)malloc(sizeof(char) * write_size);
	if(!buf->base)
	{
		buf->len = 0;
		return -1;
	}
	buf->len = write_size;
	memset(buf->base,0,buf->len);
	return 0;
}

void easynet_uninit_buf(Enet_buf_t *buf)
{
	if(!buf)
		return;
	if(buf->base)
	{
		free(buf->base);
		buf->base = NULL;
	}
	buf->len = 0;
}