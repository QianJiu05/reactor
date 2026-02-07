
#include <sys/types.h>          /* See NOTES */
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
       #include <sys/stat.h>
       #include <fcntl.h>
       #include <sys/socket.h>
#include <errno.h>


#define SERVER_PORT 8888
#define BACKLOG     2048

struct sockaddr_in server_addr;
int iAddidx_in = sizeof(struct sockaddr);

void server_bind(int serverfd, struct sockaddr* server_addr);
void set_sockaddr_in(struct sockaddr_in* server_addr);
int server_init (int serverfd);

int main (void) {

    int serverfd;
    serverfd = server_init(serverfd);
    set_sockaddr_in(&server_addr);
    server_bind(serverfd, (struct sockaddr *) &server_addr);
        
        /* 3.开始监听 */
        if (listen(serverfd, BACKLOG) == -1)
            printf("listen");

    int new_fd = accept(serverfd, (struct sockaddr *)&server_addr, &iAddidx_in);
    if (new_fd == -1) {
        printf("get bad new_fd\n");
        return -1;
    }

#define BUFLEN 1024*512
    char picture1[BUFLEN];
    char picture2[BUFLEN];
    char buffer[BUFLEN];


    int file_fd = open("featured.jpg",O_RDONLY);
    struct stat st;
    fstat(file_fd, &st);
    off_t file_size = st.st_size;
    read(file_fd, picture1, file_size);

    int response_len = snprintf(buffer,BUFLEN,    
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
            "Connection: keep-alive\r\n"
            "Cache-Control: no-cache\r\n\r\n");

    send(new_fd, buffer, response_len, 0);


    memset(buffer, BUFLEN, 0);
    int len = snprintf(buffer, BUFLEN, 
                        "--frame\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: %d\r\n\r\n", 
                        (int)file_size);

    if (len > 0 && len + file_size + 2 <= BUFLEN) {
        memcpy(buffer + len, picture1, file_size);
        len += file_size;
        memcpy(buffer + len, "\r\n", 2);
        len += 2;
    }


    while(1) {
        send(new_fd, buffer, len, 0);

        usleep(50000);
    }


}




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
    if (bind(serverfd, server_addr,sizeof(struct sockaddr)) == -1) {
        printf("bind");
    }
}
