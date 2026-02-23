
#ifndef __STRUCT_HTTP_H__
#define __STRUCT_HTTP_H__

struct http_context {
    int file_fd;
    off_t remain;       // 还需要从file_fd中读多少字节
    bool header_sent;   
    int status_code;    // 200, 404 ...
    int content_type;   // jpg//text//...
    bool keep_alive;    // Connection: close -- Connection: keep-alive
    char path[256];     //请求路径
    bool stream_mode;  
};

#endif