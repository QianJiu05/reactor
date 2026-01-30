
#include <stdio.h>
#include <string.h>

#include "http_handler.h"


int generate_http_response(char* response_buf, const char* request_buf) {
    const char* http_response = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: 44\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body>Hello from Reactor!</body></html>";
    
    strcpy(response_buf, http_response);
    return strlen(http_response);
}