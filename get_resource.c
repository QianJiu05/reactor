#include <stdio.h>
#include <fcntl.h>
#include <string.h>
       #include <sys/types.h>
       #include <sys/stat.h>

#include "get_resource.h"

extern char resource_buf[1024*1024];
// get_resource_callback接收相机客户端的数据，我该怎么把接受的数据对应到http对应的struct connect里，用指针吗？
int get_resource_callback(struct connect* conn) {
    printf("resource callback\n");

    strncpy(resource_buf, conn->rbuf, conn->rlen);
    
    int fd_file = open("test.jpg", O_RDWR | O_CREAT, 0666);

    if (fd_file < 0) {
        printf("can not create file\n");
    }
    // printf("capture to %s\n", filename);
    
    // 把文件(MJPG)从对应buf的对应索引写到fd里
    write(fd_file, resource_buf, 1024*1024);
    close(fd_file);
    
    return 0;
}
int handle_resource(struct connect* conn) {
    memset(conn->rbuf, 0, conn->rlen);
    conn->rlen = 0;
}