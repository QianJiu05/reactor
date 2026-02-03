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