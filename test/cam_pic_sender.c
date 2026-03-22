
#include <sys/types.h>          /* See NOTES */
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include "protocal.h"

#define SERVER_PORT 8888
#define BUFFER_SIZE 512*1024

struct sockaddr_in server_addr;
int iAddinlen = sizeof(struct sockaddr);

void server_bind(int serverfd, struct sockaddr* server_addr);
void set_sockaddr_in(struct sockaddr_in* server_addr);
int server_init (int serverfd);

int main (void) {


    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in caddr;
    memset(&caddr, 0, sizeof(struct sockaddr_in));
    caddr.sin_family = AF_INET;
    caddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    // caddr.sin_addr.s_addr = inet_addr("68.64.177.182");
    caddr.sin_port = htons(SERVER_PORT);
    
    if (connect(fd, (struct sockaddr*)&caddr, sizeof(caddr)) == -1) {
        perror("connect");
        close(fd);
        return 1;
    }
    printf("1\n");
    char* send_header = "SEND 7";
    send(fd,send_header, strlen(send_header), 0);

    printf("2\n");


    char picture[BUFFER_SIZE];
    char pictur2[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    char buffer2[BUFFER_SIZE];


    int file_fd = open("featured.jpg",O_RDONLY);
    struct stat st;
    fstat(file_fd, &st);
    off_t file_size = st.st_size;
    int len1 = read(file_fd, picture, file_size);
    printf("3\n");
    int file_fd2 = open("pic2.jpg",O_RDONLY);
    struct stat st2;
    fstat(file_fd2, &st2);
    off_t file_size2 = st2.st_size;
    int len2 = read(file_fd2, pictur2, file_size2);  



    // memset(buffer, BUFLEN, 0);
    struct frame_header header1,header2;
    header1.magic = PROTO_MAGIC;
    header1.version = PROTO_VERSION;
    header1.jpeg_len = len1;
    header1.checksum = calc_checksum(picture,len1+sizeof(struct frame_header));
    pack_frame(buffer,&header1,sizeof(struct frame_header));
    len1 = snprintf(buffer, BUFFER_SIZE,picture);
    


    while(1) {
        send(fd, buffer, len1, 0);
        printf("sended\n");
        usleep(50000);
        // send(new_fd,buffer2,len2,0);
        // usleep(50000);

    }


}







// int main(int argc, char* argv[]) {
//     // 1. 连接到服务器
        



//     int send_1;
//     char buffer[] = "hello!\n";
//     int len = strlen(buffer);
//     while(1) {
//         for (int i = 0; i < NUM_OF_FD; i++) {
//             send_1 = send(fds[i], buffer, len, 0);
//             // printf("send1=%d,fd=%d\n",send_1,fds[i]);
//         }
//         sleep(100);
//         // close(fds[i]);
//     }

//     return 0;
// }
