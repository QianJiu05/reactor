#ifndef __HTTP_HELPER_H__
#define __HTTP_HELPER_H__

/*
 * 响应报文里用到的固定字段。
 * 全部用 static const：放在头文件里被多个 .c 包含时不会产生重复符号。
 *
 * 注意连接头的写法：数组里已经带了 "\r\n"，拼接时不要再补，
 * 否则会多出一个空行把响应头提前截断。
 */

static const char* status_200 = "OK";
static const char* status_400 = "Bad Request";
static const char* status_404 = "Not Found";
static const char* status_405 = "Method Not Allowed";
static const char* status_503 = "Service Unavailable";

static const char* connect_close      = "Connection: close\r\n";
static const char* connect_keep_alive = "Connection: keep-alive\r\n";

static const char* type_no_need   = "Content-Type: text/plain; charset=utf-8\r\n";
static const char* type_image_jpg = "Content-Type: image/jpeg\r\n";
static const char* type_text_html = "Content-Type: text/html; charset=utf-8\r\n";
static const char* type_multipart =
    "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n";

/* ===== 各状态码的说明页 ===== */

static const char* body_400 =
    "<html><head><meta charset=\"utf-8\"><title>400</title></head><body>"
    "<h3>400 Bad Request</h3><p>请求报文格式不正确。</p>"
    "</body></html>";

static const char* body_404 =
    "<html><head><meta charset=\"utf-8\"><title>404</title></head><body>"
    "<h3>404 Not Found</h3><p>账号不存在，或路径格式不正确。</p>"
    "<p>访问格式： <code>/&lt;账号&gt;</code></p>"
    "</body></html>";

static const char* body_405 =
    "<html><head><meta charset=\"utf-8\"><title>405</title></head><body>"
    "<h3>405 Method Not Allowed</h3><p>本服务只接受 GET 请求。</p>"
    "</body></html>";

/*
 * GET / 的提示页：只给了根路径、没带账号。
 *
 * 刻意【不列出】可用账号 —— 列出来等于任何人打开首页就知道所有通道名，
 * 与本设计"账号名即访问凭据"的定位冲突（设计文档 §5.5.4）。
 * 若演示时需要少打字，把 config.h 的 ALLOW_INDEX_PAGE 设为 1。
 */
static const char* body_503_no_account =
    "<html><head><meta charset=\"utf-8\"><title>请指定账号</title></head><body>"
    "<h3>请指定账号</h3>"
    "<p>访问格式： <code>/&lt;账号&gt;</code>，例如 <code>/alice</code></p>"
    "</body></html>";

#define BODY_NO_ACCOUNT  body_503_no_account

#endif /* __HTTP_HELPER_H__ */
