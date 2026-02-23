#ifndef __SERVER_INIT_H__
#define __SERVER_INIT_H__

#include <sys/socket.h>
#include <netinet/in.h>

#include "reactor.h"
/* 网络连接 */
int server_init (int serverfd);
void set_sockaddr_in(struct sockaddr_in* server_addr);
void server_bind(int serverfd, struct sockaddr* server_addr);

struct sockaddr_in* get_sockaddr_in(void) ;

#endif