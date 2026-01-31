#ifndef __CONNECT_POOL_H__
#define __CONNECT_POOL_H__



#define NUM_OF_CONNECTOR 128
#define CONNECT_BUF_LEN 4096

struct connect{
    int fd;

    char rbuf[CONNECT_BUF_LEN];
    int rlen;
    char wbuf[CONNECT_BUF_LEN];
    int wlen;

    int connect_type;     // echo, send, serve
    int file_fd;          // 文件描述符
    off_t remaining;      // 剩余未发送字节数
    int header_sent;      // 是否发送响应头


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
void connect_pool_init(struct connect_pool*);
struct connect* get_connector(int,struct connect_pool*);
struct connect_node* get_pool (int num, struct connect_pool* pool);

#endif
