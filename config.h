/* ============================================================
 *  服务器端配置
 * ============================================================ */

/* 打开后打印运行期日志（连接、转发、落盘统计）。
 * 用 #ifndef 保护，方便用 make CFLAGS+=-DDEBUG 在命令行覆盖。 */
#ifndef DEBUG
#define DEBUG
#endif

/* ===== 网络 ===== */
#define SERVER_PORT 8888
#define BACKLOG     8192

/* ===== epoll ===== */
#define MAX_EVENTS       8192
#define NUM_OF_CONNECTOR 100      /* 一个连接池节点容纳的连接数 */
#define CONNECT_BUF_LEN  (512 * 1024)   /* 每个连接收发缓冲区大小 */

#define NUM_OF_THREAD   2
#define NUM_OF_REACTOR  NUM_OF_THREAD

/* ===== 本地落盘（课题要求 4）===== */
#define RECORD_DIR          "./records"
#define RECORD_FILE_FRAMES  500   /* 每个文件最多存多少帧 */
