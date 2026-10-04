#ifndef __HTTP_HELPER_H__
#define __HTTP_HELPER_H__

/*
 * 响应报文里用到的固定字段。
 * 全部用 static const：放在头文件里被多个 .c 包含时不会产生重复符号。
 */

static const char* body_200 = "<html><body>Hello from Reactor!</body></html>";
static const char* body_400 = "<html><body>400 Bad Request</body></html>";
static const char* body_404 = "<html><body>404 Not found</body></html>";
static const char* body_405 = "<html><body>405 Method Not Allowed</body></html>";

static const char* status_200 = "OK";
static const char* status_400 = "BAD";
static const char* status_404 = "NOTFOUND";
static const char* status_405 = "NOTALLOWED";

static const char* connect_close      = "Connection: close";
static const char* connect_keep_alive = "Connection: keep-alive";

static const char* type_no_need   = " ";
static const char* type_image_jpg = "Content-Type: image/jpeg";
static const char* type_text_html = "Content-Type: text/html; charset=utf-8";
static const char* type_multipart =
    "Content-Type: multipart/x-mixed-replace; boundary=frame";

#endif /* __HTTP_HELPER_H__ */
