#ifndef __HTTP_HANDLER_H__
#define __HTTP_HANDLER_H__

#include "connect_pool.h"
#include "struct_http.h"
#include "protocol.h"

#define TYPE_NO_NEED            0
#define TYPE_TEXT_HTML          1
#define TYPE_IMG_JPEG           2

int http_get_status_code(const char* request_buf);
int generate_http_response(struct connect* conn);

int http_callback(struct connect* conn);
#endif