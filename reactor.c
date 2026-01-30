//gcc -o reactor reactor.c

#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <fcntl.h>
#include "reactor.h"
#include <errno.h>

#define MAX_EVENTS 1024
#define NUM_OF_CONNECTOR 128
#define SERVER_PORT 8888
#define BACKLOG     512

#define CONNECT_BUF_LEN 128

struct connect{
    int fd;

    char rbuf[CONNECT_BUF_LEN];
    int rlen;
    char wbuf[CONNECT_BUF_LEN];
    int wlen;

    union recv_func
    {
        int (*accept_cb)(struct connect*);
        int (*recv_cb)(struct connect*);
    }recv_func;
    

    int (*send_cb)(struct connect*);
    void (*close)(struct connect*);
};
struct connect_node{
    struct connect pool[NUM_OF_CONNECTOR];
    struct connect_node* next;
};
struct connect_pool{
    int num_of_pool;
    struct connect_node* start;
    struct connect_node* last;
};

/* structure */
void connect_pool_init(void);
struct connect* get_connector(int fd);
struct connect_node* alloc_new_pool(void);


int server_init (int serverfd);
void set_sockaddr_in(struct sockaddr_in* server_addr);
void server_bind(int serverfd, struct sockaddr* server_addr);

/* epoll */
void connect_init(struct connect* conn, int fd) ;
void set_epoll(int EVENT, int OPERATION, int fd) ;

/* callback */
int accept_callback(struct connect*);
int recv_callback(struct connect*);
int send_callback(struct connect*);
void close_callback(struct connect*);

int epfd;
struct sockaddr_in server_addr;
int iAddrLen = sizeof(struct sockaddr);

struct connect_pool pool;

int main (void) {
    // printf("pid=%d\n",getpid());
    connect_pool_init();

    int serverfd;
    serverfd = server_init(serverfd);
    set_sockaddr_in(&server_addr);
    server_bind(serverfd, (struct sockaddr *) &server_addr);
    
    /* 3.开始监听 */
    if (listen(serverfd, BACKLOG) == -1)
        printf("listen");

    struct connect connector[NUM_OF_CONNECTOR];
    struct epoll_event events[MAX_EVENTS]; 


    epfd = epoll_create(1);
    set_epoll(EPOLLIN, EPOLL_CTL_ADD, serverfd);

    while (1)
    {
        int nready;//就绪了多少个事件
        nready = epoll_wait(epfd,events,MAX_EVENTS,-1);

        for (int i = 0; i < nready; i++) {
            if (serverfd == events[i].data.fd) {
                /* 建立新连接 */
                struct connect* this = get_connector(serverfd);
                // struct connect* this = &connector[serverfd];
                this->recv_func.accept_cb(this);
            } else {
                /* events[i].data.fd为已有的fd */
                struct connect* this = get_connector(events[i].data.fd);
                // struct connect* this = &connector[events[i].data.fd];
                this->recv_func.recv_cb(this);
            }
        }
        
    }	

    return 0;
}

/*************** structure ***************/
struct connect_node* alloc_new_pool(void) {
    printf("alloc new pool\n");
    struct connect_node* new = malloc(sizeof(struct connect_node));
    if (new == NULL) {
        printf("alloc new pool err\n");
        return NULL;
    }
    memset(new, 0, sizeof(struct connect_node));
    if (pool.last != NULL) {
        pool.last->next = new;
        pool.last = new;
    }
    pool.num_of_pool++;
    return new;
}

void connect_pool_init(void) {
    memset(&pool, 0, sizeof pool);

    pool.start = alloc_new_pool();
    pool.last = pool.start;
}

struct connect_node* get_pool (int num) {
    //num_of_pool是从1开始的,numpool=3:0,1,2
    // num是从0开始的，1/128 =0
    //比如num=3，num_of_pool=3,
    printf("num =%d, numofpool=%d\n",num,pool.num_of_pool);
    while (num >= pool.num_of_pool) {
        // printf("pool not exist,creating..\n");
        alloc_new_pool();
    }

    struct connect_node* ret = pool.start;
    for(int i = 0; i < num; i++) {
        ret = ret->next;
    }
    return ret;
}

struct connect* get_connector(int fd) {
    //在第几个pool
    struct connect_node* node = get_pool(fd / NUM_OF_CONNECTOR);
    //在这个pool的第几个
    int cnt = fd % NUM_OF_CONNECTOR;
    return &node->pool[cnt];
}

/*************** callback ****************/
int accept_callback(struct connect* conn) {
    int new_fd = accept(conn->fd, (struct sockaddr *)&server_addr, &iAddrLen);
    if (new_fd == -1) {
        printf("get bad new_fd\n");
        return -1;
    }
    printf("get new fd:%d\n",new_fd);
    struct connect* connector = get_connector(new_fd);
    connect_init(connector,new_fd);
    set_epoll(EPOLLIN, EPOLL_CTL_ADD, new_fd);
    return new_fd;
}
int recv_callback(struct connect* conn) {
    conn->rlen = recv(conn->fd,conn->rbuf,CONNECT_BUF_LEN - 1, 0);

    if (conn->rlen == 0){
        conn->close(conn);
        return -1;
    } else if (conn->rlen < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // 继续等待
        }
    }
    conn->rbuf[conn->rlen] = '\0';
    conn->send_cb(conn);
    printf("fd:%d msg:%s\n",conn->fd,conn->rbuf);
}

int send_callback(struct connect* conn) {
    strncpy(conn->wbuf,conn->rbuf,conn->rlen);
    conn->wlen = conn->rlen;
    // printf("Get Msg: %s\n",  conn->wbuf);

    int send_cnt = send(conn->fd,conn->wbuf,conn->wlen,0);
    conn->wlen -= send_cnt;

    if (conn->wlen == 0) {
        memset(conn->wbuf, 0, sizeof(conn->wbuf));
    }
}

void close_callback(struct connect* conn) {
    printf("close fd:%d\n",conn->fd);
    set_epoll(0, EPOLL_CTL_DEL,conn->fd);
    close(conn->fd);
}

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

void set_sockaddr_in(struct sockaddr_in* server_addr) {
    memset(server_addr, 0, sizeof(struct sockaddr_in));
    server_addr->sin_family = AF_INET;
    server_addr->sin_addr.s_addr = INADDR_ANY;
    server_addr->sin_port = htons(SERVER_PORT); /* host to net, short 大小端序的转换*/
}

/* 2.绑定socket与端口号 */
void server_bind(int serverfd, struct sockaddr* server_addr) {
    if (bind(serverfd, server_addr,sizeof(struct sockaddr)) == -1) {
        printf("bind");
    }
}


/**************** epoll  ****************/
void connect_init(struct connect* conn, int fd) {
    conn->fd = fd;
    conn->rlen = 0;
    conn->wlen = 0;
    conn->recv_func.recv_cb = recv_callback;
    conn->send_cb = send_callback;
    conn->close = close_callback;

    // 添加：设置为非阻塞模式
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void set_epoll(int EVENT, int OPERATION, int fd) {
    struct epoll_event ev;
    if (EVENT != 0) {
        ev.events = EVENT;
    }
    ev.data.fd = fd;
    epoll_ctl(epfd,OPERATION,fd,&ev);
}
