const char* body_200 = "<html><body>Hello from Reactor!</body></html>";
const char* body_400 = "<html><body>400 Bad Request</body></html>";
const char* body_404 = "<html><body>404 Not found</body></html>";
const char* body_405 = "<html><body>405 Method Not Allowed</body></html>";

const char* status_200 = "OK";
const char* status_400 = "BAD";
const char* status_404 = "NOTFOUND";
const char* status_405 = "NOTALLOWED";

const char* connect_close      = "Connection: close";
const char* connect_keep_alive = "Connection: keep-alive";

const char* type_no_need        = " ";
const char* type_image_jpg      = "Content-Type: image/jpeg";
const char* type_text_html      = "Content-Type: text/html; charset=utf-8";
const char* type_multipart      = "Content-Type: multipart/x-mixed-replace; boundary=frame";




    // /* 普通模式：读取file_fd然后发送 */
    // else if (http->file_fd >= 0 && http->remain > 0) 
    // {
    //     int to_read = (http->remain < CONNECT_BUF_LEN) ? http->remain : CONNECT_BUF_LEN;
    //     int bytes_read = read(http->file_fd, conn->wbuf, to_read);
        
    //     if (bytes_read > 0) {
    //         int send_cnt = send(conn->fd, conn->wbuf, bytes_read, 0);
    //         if (send_cnt < 0) {
    //             if (errno == EAGAIN || errno == EWOULDBLOCK) {
    //                 set_epoll(EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
    //                 return 0;
    //             }
    //             conn->close(conn);
    //             return -1;
    //         }
    //         http->remain -= send_cnt;
            
    //         // 还有数据,继续监听 EPOLLOUT
    //         if (http->remain > 0) {
    //             set_epoll(EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
    //             return 0;
    //         }
    //     }
    //     // printf("send pic OK\n");
    //     // 发送完毕
    //     close(http->file_fd);
    //     http->file_fd = -1;
    // }