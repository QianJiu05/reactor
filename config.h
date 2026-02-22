
/* 网络连接 */
#define SERVER_PORT 8888
// #define BACKLOG     2048
//sudo sysctl -w net.ipv4.tcp_max_syn_backlog=8192
#define BACKLOG     8192

/* epoll */
#define MAX_EVENTS 2046
#define NUM_OF_CONNECTOR 1024
// #define CONNECT_BUF_LEN 512*1024
#define CONNECT_BUF_LEN 32
