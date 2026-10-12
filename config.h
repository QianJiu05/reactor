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

/* ============================================================
 *  账号体系
 *
 *  一个账号 = 一路摄像头通道：最多 1 个采集端 + 多个查看端。
 *
 *  ⚠ 本版【不校验密码】，账号名即访问凭据 —— 这不是访问控制，
 *    只是通道标识。详见 docs/账号体系设计.md 的风险表 R1~R3。
 * ============================================================ */
#define ACCOUNT_FILE        "./accounts.conf"  /* 账号名单文件 */
#define MAX_ACCOUNTS        64                 /* 账号总数上限 */
#define MAX_VIEWERS_PER     32                 /* 单账号查看端上限 */
#define ACCOUNT_NAME_LEN    32                 /* 账号名最大长度 */

/* 首行最大长度（防慢速攻击占缓冲）。
 * 注意：recv.h 里也有一份同名定义，改动时两处要同步。 */
#define MAX_HANDSHAKE_LEN   128

#define CAM_STALE_MS        10000              /* 采集端无帧多久视为僵死 */
