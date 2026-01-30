
#define NUM_OF_CONNECTOR 128
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
void connect_pool_init(struct connect_pool*);
struct connect* get_connector(int,struct connect_pool*);
struct connect_node* get_pool (int num, struct connect_pool* pool);
// struct connect_node* alloc_new_pool(void);/