
#include <sys/types.h>          /* See NOTES */
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#include "reactor.h"
#include "accept.h"
#include "connect_pool.h"
#include "epoll.h"
#include "server_init.h"

/* 由 example/main.c 在解析命令行后设置 */
extern int g_server_port;

static struct sockaddr_in server_addr;
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

    /* 允许地址复用：服务器重启时不必等 TIME_WAIT 结束 */
    int on = 1;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct connect* connector = get_connector(serverfd);
    if (connector != NULL) {
        connect_init(connector, serverfd);
        connector->recv_func.accept_cb = accept_callback;
    }
    return serverfd;
}

void set_sockaddr_in(struct sockaddr_in* addr) {
    struct sockaddr_in* target = (addr != NULL) ? addr : &server_addr;

    memset(target, 0, sizeof(struct sockaddr_in));
    target->sin_family = AF_INET;
    target->sin_addr.s_addr = INADDR_ANY;
    target->sin_port = htons((uint16_t)g_server_port); /* host to net */
}

/* 2.绑定socket与端口号 */
void server_bind(int serverfd) {
    if (bind(serverfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        perror("bind");
        _exit(1);
    }
}

struct sockaddr_in* get_sockaddr_in(void) {
    return &server_addr;
}
