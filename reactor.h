#ifndef __REACTOR_H__
#define __REACTOR_H__

#define CONNECT_BUF_LEN 128

struct connect{
    int fd;

    char rbuf[CONNECT_BUF_LEN];
    int rlen;
    char wbuf[CONNECT_BUF_LEN];
    int wlen;


    int (*recv_cb)(struct connect*);
    int (*send_cb)(struct connect*);
    int (*close)(struct connect);
};

#endif
