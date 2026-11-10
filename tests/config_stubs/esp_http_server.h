#pragma once
#include <string>
#include <cstring>
#include <algorithm>
typedef int esp_err_t;
typedef void *httpd_handle_t;
#define ESP_OK 0
#define HTTPD_RESP_USE_STRLEN -1
enum { HTTP_GET, HTTP_POST };
struct httpd_req_t {
    size_t content_len = 0, offset = 0;
    std::string body, auth, response, status = "200 OK";
};
struct httpd_config_t {
    int server_port = 80, stack_size = 0, max_open_sockets = 0;
    bool lru_purge_enable = false;
    int recv_wait_timeout = 0, send_wait_timeout = 0;
    size_t max_req_hdr_len = 1024, max_uri_len = 512;
};
#define HTTPD_DEFAULT_CONFIG()                                                                     \
    httpd_config_t {}
struct httpd_uri_t {
    const char *uri;
    int method;
    esp_err_t (*handler)(httpd_req_t *);
    void *context;
};
inline int httpd_start(httpd_handle_t *h, const httpd_config_t *) {
    *h = (void *)1;
    return 0;
}
inline int httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *) {
    return 0;
}
inline int httpd_resp_set_status(httpd_req_t *r, const char *s) {
    r->status = s;
    return 0;
}
inline int httpd_resp_set_type(httpd_req_t *, const char *) {
    return 0;
}
inline int httpd_resp_set_hdr(httpd_req_t *, const char *, const char *) {
    return 0;
}
inline int httpd_resp_send(httpd_req_t *r, const char *p, int n) {
    r->response = n < 0 ? p : std::string(p, n);
    return 0;
}
inline int httpd_req_get_hdr_value_str(httpd_req_t *r, const char *, char *out, size_t n) {
    if (r->auth.empty() || r->auth.size() + 1 > n)
        return -1;
    strcpy(out, r->auth.c_str());
    return 0;
}
inline int httpd_req_recv(httpd_req_t *r, char *out, size_t n) {
    n = (std::min)(n, r->body.size() - r->offset);
    memcpy(out, r->body.data() + r->offset, n);
    r->offset += n;
    return int(n);
}
