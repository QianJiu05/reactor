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

#define SERVER_PORT 8888
#define BACKLOG     10

void handle_error (char* string);

int recv_callback(int fd, void* buf, size_t n, int flags);

struct connect{
    int fd;

    // void* rbuf;
    char rbuf[128];
    int rlen;
    // void* wbuf;
    char wbuf[128];
    int wlen;


    int (*recv)(int fd, void* buf, size_t n, int flags);
    int (*close)(int fd);
};

/* 1.创建socket   AF_INET -> 允许远程通信
                 SOCK_STREAM -> TCP协议  */
int server_init (int serverfd) {
    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == serverfd)
	{
		printf("socket error!\n");
		return -1;
	}
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
    if (bind(serverfd, server_addr,sizeof(struct sockaddr)) == -1)
        handle_error("bind");
}
int main (void) {

    int serverfd;
    serverfd = server_init(serverfd);

    struct sockaddr_in server_addr;
    set_sockaddr_in(&server_addr);

    server_bind(serverfd, (struct sockaddr *) &server_addr);
    
    /* 3.开始监听 */
    if (listen(serverfd, BACKLOG) == -1)
        handle_error("listen");
    
    struct connect test;
    
    
	while (1)
	{
		int iAddrLen = sizeof(struct sockaddr);
		test.fd = accept(serverfd, (struct sockaddr *)&server_addr, &iAddrLen);
		if (-1 != test.fd)
		{
			if (!fork())
			{
				while (1)
				{
                    int iRecvLen;
                    test.rlen = recv(test.fd,test.rbuf,999,0);
					if (test.rlen <= 0)
					{
						close(test.fd);
						return -1;
					}
					else
					{
                        strncpy(test.wbuf,test.rbuf,test.rlen);
						printf("Get Msg: %s\n",  test.wbuf);

                        int send_cnt = send(test.fd,test.wbuf,test.wlen,0);
                        test.wlen -= send_cnt;
                        printf("send back\n");

                        if (test.wlen == 0) {
                            memset(test.wbuf, 0, sizeof(test.wbuf));
                        }
					}
				}				
			}
		}
	}

    return 0;
}


void handle_error (char* string) {
    printf("err: %s\n",string);
}