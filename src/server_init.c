
#include <sys/types.h>          /* See NOTES */
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#include "reactor.h"
#include "connect_pool.h"
#include "accept.h"

struct sockaddr_in server_addr;
int iAddinlen = sizeof(struct sockaddr);
/******************************* 初始化  *******************************/

/* 1.创建socket   AF_INET -> 允许远程通信
                 SOCK_STREAM -> TCP协议  */
int server_init (int serverfd) {
    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == serverfd)
	{
		printf("socket error!\n");
		return -1;
	}
    struct connect* connector = get_connector(serverfd);
    connect_init(connector,serverfd);
    connector->recv_func.accept_cb = accept_callback;
    return serverfd;
}

void set_sockaddr_in(void) {
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT); /* host to net, short 大小端序的转换*/
}

/* 2.绑定socket与端口号 */
void server_bind(int serverfd) {
    if (bind(serverfd, &server_addr,sizeof(struct sockaddr)) == -1) {
        printf("bind");
    }
}

struct sockaddr_in* get_sockaddr_in(void) {
    return &server_addr;
}