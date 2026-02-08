#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#include "config.h"
#include "connect_pool.h"
#include "reactor.h"
#include "http_handler.h"
#include "recv_resource.h"
#include "server_init.h"



/* epoll */
void connect_init(struct connect* conn, int fd) ;
void set_epoll(int EVENT, int OPERATION, int fd) ;

/* callback */
int accept_callback(struct connect*);
int recv_callback(struct connect*);
void close_callback(struct connect*);

int echo_callback(struct connect* conn);


int epfd;
struct sockaddr_in server_addr;
int iAddidx_in = sizeof(struct sockaddr);



int main (void) {
    connect_pool_init();

    int serverfd;
    serverfd = server_init(serverfd);
    set_sockaddr_in(&server_addr);
    server_bind(serverfd, (struct sockaddr *) &server_addr);
    
    /* 3.开始监听 */
    if (listen(serverfd, BACKLOG) == -1)
        printf("listen");

    struct epoll_event events[MAX_EVENTS]; 


    epfd = epoll_create(1);
    set_epoll(EPOLLIN | EPOLLOUT, EPOLL_CTL_ADD, serverfd);

    while (1)
    {
        int nready;//就绪了多少个事件
        nready = epoll_wait(epfd,events,MAX_EVENTS,-1);

        for (int i = 0; i < nready; i++) {
            if (serverfd == events[i].data.fd) {
                /* 建立新连接 */
                struct connect* this = get_connector(serverfd);
                this->recv_func.accept_cb(this);

            } else {
                /* events[i].data.fd为已有的fd */
                struct connect* this = get_connector(events[i].data.fd);

                /* 有新msg进入 */
                if (events[i].events & EPOLLIN) {
                    this->recv_func.recv_cb(this);

                /* outbuf已经发送完，发送新的outbuf */
                } else if (events[i].events & EPOLLOUT) {
                    this->send_cb(this);
                }
            }
        }
        
    }	

    return 0;
}

/****************************** callback *******************************/
int parse_serve_type(struct connect* conn) {
    int type = SERVE_ECHO;

    if (strncmp(conn->inbuf, "GET ", 4) == 0) {
        type = SERVE_HTTP;
    } else if (strncmp(conn->inbuf, "SEND ", 5) == 0) {
        type = SERVE_GET_RESOURCE;
    } 

    return type;
}

int accept_callback(struct connect* conn) {
    int new_fd = accept(conn->fd, (struct sockaddr *)&server_addr, &iAddidx_in);
    if (new_fd == -1) {
        printf("get bad new_fd\n");
        return -1;
    }
    printf("get new fd:%d\n",new_fd);
    struct connect* connector = get_connector(new_fd);
    connect_init(connector, new_fd);
    set_epoll(EPOLLIN, EPOLL_CTL_ADD, new_fd);
    return new_fd;
}

int recv_callback(struct connect* conn) {
    /* 把数据接收到inbuf */
    int to_copy = CONNECT_BUF_LEN - conn->idx_in;
    conn->idx_in += recv(conn->fd, conn->inbuf + conn->idx_in , to_copy, 0);

    /* idx_in == 0 表示对端正常关闭，应直接 close，不要看 errno。
        只有 idx_in < 0 才检查 errno == EAGAIN/EWOULDBLOCK。 */
    if (conn->idx_in == 0){
        conn->close(conn);
        return -1;
    } else if (conn->idx_in < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // 继续等待
        } else {
            conn->close(conn);
            return -1;
        }
    }

    // conn->inbuf[conn->idx_in] = '\0';

    if (conn->serve_type == SERVE_NOT_INIT || conn->serve_type == SERVE_HTTP) {
        // 解析idx_in，GET-->HTTP/ SEND-->recv from cam/ echo
        // 生成 HTTP 响应(初始化 file_fd 和 remaining)
        int serve_type = parse_serve_type(conn);
        conn->serve_type = serve_type;
        switch(conn->serve_type) {
            case SERVE_HTTP:
                generate_http_response(conn);
                conn->send_cb = http_callback;
                break;
    
            case SERVE_GET_RESOURCE:
                handle_resource(conn);
                conn->send_cb = get_resource_callback;
                break;

            case SERVE_ECHO:
                conn->send_cb = echo_callback;
                break;

            default:
                printf("err serve type:%d\n",serve_type);
        }
    }
    
    conn->send_cb(conn);
    return 0;
    // printf("fd:%d msg:%s\n",conn->fd,conn->inbuf);
}

int echo_callback(struct connect* conn) {
    strncpy(conn->outbuf, conn->inbuf, conn->idx_in);
    conn->idx_out = conn->idx_in;
    conn->idx_in = 0;
    printf("%d Get Msg: %s\n", conn->fd, conn->outbuf);
    int send_cnt = send(conn->fd, conn->outbuf, conn->idx_out, 0);
    conn->idx_out -= send_cnt;


    if (conn->idx_out != 0) {
        memmove(conn->outbuf, conn->outbuf + send_cnt, conn->idx_out);
    } else {
        memset(conn->outbuf, 0, sizeof(conn->outbuf));
    }
    return conn->idx_out;
}

void close_callback(struct connect* conn) {
    printf("close fd:%d\n",conn->fd);
    set_epoll(0, EPOLL_CTL_DEL,conn->fd);
    close(conn->fd);
    memset(conn, 0, sizeof(struct connect));
}




/******************************* epoll  *******************************/
void connect_init(struct connect* conn, int fd) {
    conn->fd = fd;
    conn->idx_in = 0;
    conn->idx_out = 0;
    conn->recv_func.recv_cb = recv_callback;
    conn->close = close_callback;
    conn->serve_type = SERVE_NOT_INIT;

    // 设置为非阻塞模式，没消息直接返回
    // 如果是阻塞，send/recv会在没有消息时一直阻塞等待，直到有消息
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* 
    EPOLLIN, EPOLL_CTL_ADD, fd --> 新增事件：监听 fd 的 EPOLLIN
*/
void set_epoll(int EVENT, int OPERATION, int fd) {
    struct epoll_event ev;

    if (EVENT != 0) { ev.events = EVENT;}
    ev.data.fd = fd;

    epoll_ctl(epfd, OPERATION, fd, &ev);
}
