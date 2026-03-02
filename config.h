
/* 网络连接 */
#define SERVER_PORT 8888
// #define BACKLOG     2048
#define BACKLOG     8192

/* epoll */
#define MAX_EVENTS 8192
#define NUM_OF_CONNECTOR 10000
// #define CONNECT_BUF_LEN 512*1024
// #define CONNECT_BUF_LEN 4096
#define CONNECT_BUF_LEN 128

#define NUM_OF_THREAD   2
#define NUM_OF_REACTOR  NUM_OF_THREAD