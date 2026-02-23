#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

#include "server_init.h"
#include "callback.h"
#include "recv.h"
#include "epoll.h"


int accept_callback(int fd) {
    struct sockaddr_in* server_addr = get_sockaddr_in();
    static int iAddinlen = sizeof(server_addr);
    int new_fd = accept(fd, (struct sockaddr *)server_addr, &iAddinlen);
    if (new_fd == -1) {
        printf("get bad new_fd\n");
        return -1;
    }
    printf("get new fd:%d\n",new_fd);

    return new_fd;
}


