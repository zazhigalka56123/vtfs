#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/inet.h>
#include <net/sock.h>
#include <linux/stdarg.h>

#include "vtfs.h"
#include "http.h"

static char server_host[256] = "127.0.0.1";
static int server_port = 8080;
static bool http_initialized = false;

static int parse_url(const char *url)
{
    const char *host_start;
    const char *port_start;
    int host_len;

    if (!url)
        return -EINVAL;

    if (strncmp(url, "http://", 7) == 0)
        host_start = url + 7;
    else
        host_start = url;

    port_start = strchr(host_start, ':');
    if (port_start) {
        host_len = port_start - host_start;
        if (kstrtoint(port_start + 1, 10, &server_port) != 0)
            server_port = 8080;
    } else {
        host_len = strlen(host_start);
        server_port = 8080;
    }

    {
        const char *slash = strchr(host_start, '/');
        if (slash && slash - host_start < host_len)
            host_len = slash - host_start;
    }

    if (host_len >= sizeof(server_host))
        host_len = sizeof(server_host) - 1;

    strncpy(server_host, host_start, host_len);
    server_host[host_len] = '\0';

    return 0;
}

int vtfs_http_init(const char *server_url)
{
    int ret;

    if (server_url) {
        ret = parse_url(server_url);
        if (ret)
            return ret;
    }

    http_initialized = true;
    return 0;
}

void vtfs_http_cleanup(void)
{
    http_initialized = false;
}

void vtfs_http_set_server(const char *url)
{
    if (url)
        parse_url(url);
}

static struct socket *create_connection(void)
{
    struct socket *sock = NULL;
    struct sockaddr_in server_addr;
    int ret;

    ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
    if (ret < 0)
        return NULL;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    ret = in4_pton(server_host, -1, (u8 *)&server_addr.sin_addr.s_addr, -1, NULL);
    if (ret != 1) {
        sock_release(sock);
        return NULL;
    }

    ret = kernel_connect(sock, (struct sockaddr *)&server_addr,
                         sizeof(server_addr), 0);
    if (ret < 0) {
        sock_release(sock);
        return NULL;
    }

    return sock;
}

static int socket_send(struct socket *sock, const char *buf, size_t len)
{
    struct kvec iov;
    struct msghdr msg;

    memset(&msg, 0, sizeof(msg));
    iov.iov_base = (void *)buf;
    iov.iov_len = len;

    return kernel_sendmsg(sock, &msg, &iov, 1, len);
}

static int socket_recv(struct socket *sock, char *buf, size_t len)
{
    struct kvec iov;
    struct msghdr msg;

    memset(&msg, 0, sizeof(msg));
    iov.iov_base = buf;
    iov.iov_len = len;

    return kernel_recvmsg(sock, &msg, &iov, 1, len, 0);
}

static int url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i, j;

    for (i = 0, j = 0; src[i] && j < dst_size - 3; i++) {
        char c = src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = c;
        } else {
            dst[j++] = '%';
            dst[j++] = hex[(c >> 4) & 0x0F];
            dst[j++] = hex[c & 0x0F];
        }
    }
    dst[j] = '\0';

    return j;
}

static int parse_http_response(const char *response, char *body,
                               size_t body_size, int *status_code)
{
    const char *body_start;
    const char *status_start;
    size_t body_len;

    status_start = strstr(response, "HTTP/1.");
    if (!status_start) {
        *status_code = 500;
        return 0;
    }
    
    status_start = strchr(status_start, ' ');
    if (!status_start) {
        *status_code = 500;
        return 0;
    }
    
    status_start++;
    unsigned long status_ul = simple_strtoul(status_start, NULL, 10);
    if (status_ul >= 100 && status_ul <= 599) {
        *status_code = (int)status_ul;
    } else {
        *status_code = 500;
    }

    body_start = strstr(response, "\r\n\r\n");
    if (!body_start)
        body_start = strstr(response, "\n\n");
    
    if (!body_start) {
        body[0] = '\0';
        return 0;
    }

    body_start += (response[body_start - response] == '\r') ? 4 : 2;

    if (strstr(response, "Transfer-Encoding: chunked")) {
        const char *chunk_ptr = body_start;
        char *output_ptr = body;
        size_t total_decoded = 0;
        
        while (total_decoded < body_size - 1) {
            unsigned long chunk_size;
            char *endptr;
            
            chunk_size = simple_strtoul(chunk_ptr, &endptr, 16);
            if (chunk_size == 0) break;
            
            chunk_ptr = endptr;
            while (*chunk_ptr == '\r' || *chunk_ptr == '\n') chunk_ptr++;
            
            if (total_decoded + chunk_size >= body_size - 1)
                chunk_size = body_size - 1 - total_decoded;
            
            memcpy(output_ptr, chunk_ptr, chunk_size);
            output_ptr += chunk_size;
            total_decoded += chunk_size;
            
            chunk_ptr += chunk_size;
            while (*chunk_ptr == '\r' || *chunk_ptr == '\n') chunk_ptr++;
        }
        
        *output_ptr = '\0';
        return total_decoded;
    }

    body_len = strlen(body_start);
    if (body_len >= body_size)
        body_len = body_size - 1;

    memcpy(body, body_start, body_len);
    body[body_len] = '\0';

    return body_len;
}

int64_t vtfs_http_call(const char *token,
                       const char *method,
                       char *response_buffer,
                       size_t buffer_size,
                       size_t arg_size,
                       ...)
{
    struct socket *sock = NULL;
    char *request = NULL;
    char *response = NULL;
    char *query_params = NULL;
    va_list args;
    int ret = 0;
    int http_status = 0;
    size_t request_len;
    size_t i;

    if (!http_initialized)
        return -EINVAL;

    request = kmalloc(VTFS_HTTP_BUFFER_SIZE, GFP_KERNEL);
    response = kmalloc(VTFS_HTTP_BUFFER_SIZE, GFP_KERNEL);
    query_params = kmalloc(VTFS_HTTP_BUFFER_SIZE, GFP_KERNEL);

    if (!request || !response || !query_params) {
        ret = -ENOMEM;
        goto out;
    }

    query_params[0] = '\0';
    va_start(args, arg_size);
    for (i = 0; i < arg_size; i++) {
        const char *key = va_arg(args, const char *);
        const char *value = va_arg(args, const char *);
        char encoded_value[512];

        if (i > 0)
            strlcat(query_params, "&", VTFS_HTTP_BUFFER_SIZE);

        strlcat(query_params, key, VTFS_HTTP_BUFFER_SIZE);
        strlcat(query_params, "=", VTFS_HTTP_BUFFER_SIZE);
        
        url_encode(value, encoded_value, sizeof(encoded_value));
        strlcat(query_params, encoded_value, VTFS_HTTP_BUFFER_SIZE);
    }
    va_end(args);

    if (strlen(query_params) > 0) {
        request_len = snprintf(request, VTFS_HTTP_BUFFER_SIZE,
            "GET /%s?token=%s&%s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, token ? token : "", query_params,
            server_host, server_port);
    } else {
        request_len = snprintf(request, VTFS_HTTP_BUFFER_SIZE,
            "GET /%s?token=%s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, token ? token : "",
            server_host, server_port);
    }

    sock = create_connection();
    if (!sock) {
        ret = -ECONNREFUSED;
        goto out;
    }

    ret = socket_send(sock, request, request_len);
    if (ret < 0)
        goto out;

    memset(response, 0, VTFS_HTTP_BUFFER_SIZE);
    ret = socket_recv(sock, response, VTFS_HTTP_BUFFER_SIZE - 1);
    if (ret < 0)
        goto out;

    response[ret] = '\0';
    
    parse_http_response(response, response_buffer, buffer_size, &http_status);

    if (http_status >= 400)
        ret = http_status;
    else
        ret = 0;

out:
    if (sock)
        sock_release(sock);
    kfree(request);
    kfree(response);
    kfree(query_params);

    return ret;
}

static int base64_decode(const char *input, unsigned char *output, size_t *output_len)
{
    static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t in_len = strlen(input);
    size_t i, j;
    unsigned char a, b, c, d;
    const char *pos;

    if (in_len % 4 != 0)
        return -EINVAL;

    *output_len = (in_len / 4) * 3;
    if (in_len > 0 && input[in_len - 1] == '=') (*output_len)--;
    if (in_len > 1 && input[in_len - 2] == '=') (*output_len)--;

    for (i = 0, j = 0; i < in_len;) {
        if (input[i] == '=') {
            a = 0;
        } else {
            pos = strchr(base64_chars, input[i]);
            a = pos ? (pos - base64_chars) : 0;
        }
        i++;

        if (input[i] == '=') {
            b = 0;
        } else {
            pos = strchr(base64_chars, input[i]);
            b = pos ? (pos - base64_chars) : 0;
        }
        i++;

        if (input[i] == '=') {
            c = 0;
        } else {
            pos = strchr(base64_chars, input[i]);
            c = pos ? (pos - base64_chars) : 0;
        }
        i++;

        if (input[i] == '=') {
            d = 0;
        } else {
            pos = strchr(base64_chars, input[i]);
            d = pos ? (pos - base64_chars) : 0;
        }
        i++;

        if (j < *output_len) output[j++] = (a << 2) | (b >> 4);
        if (j < *output_len) output[j++] = (b << 4) | (c >> 2);
        if (j < *output_len) output[j++] = (c << 6) | d;
    }

    return 0;
}

static int bytes_to_base64(const void *data, size_t len, char *base64, size_t base64_size)
{
    static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char *bytes = data;
    size_t i, j;
    
    if (base64_size < ((len + 2) / 3) * 4 + 1)
        return -EINVAL;
    
    for (i = 0, j = 0; i < len; i += 3) {
        unsigned char a = bytes[i];
        unsigned char b = (i + 1 < len) ? bytes[i + 1] : 0;
        unsigned char c = (i + 2 < len) ? bytes[i + 2] : 0;
        
        base64[j++] = base64_chars[(a >> 2) & 0x3F];
        base64[j++] = base64_chars[((a << 4) | (b >> 4)) & 0x3F];
        base64[j++] = (i + 1 < len) ? base64_chars[((b << 2) | (c >> 6)) & 0x3F] : '=';
        base64[j++] = (i + 2 < len) ? base64_chars[c & 0x3F] : '=';
    }
    
    base64[j] = '\0';
    return 0;
}

static int extract_json_field(const char *json, const char *field, char *value, size_t value_size)
{
    char pattern[128];
    const char *field_start;
    const char *value_start;
    const char *value_end;
    size_t value_len;

    snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    field_start = strstr(json, pattern);
    if (!field_start)
        return -ENOENT;

    value_start = strchr(field_start + strlen(pattern), '"');
    if (!value_start)
        return -EINVAL;
    value_start++;

    value_end = strchr(value_start, '"');
    if (!value_end)
        return -EINVAL;

    value_len = value_end - value_start;
    if (value_len >= value_size)
        value_len = value_size - 1;

    memcpy(value, value_start, value_len);
    value[value_len] = '\0';

    return 0;
}

static int extract_json_number(const char *json, const char *field, char *value, size_t value_size)
{
    char pattern[128];
    const char *field_start;
    const char *value_start;
    const char *value_end;
    size_t value_len;

    snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    field_start = strstr(json, pattern);
    if (!field_start)
        return -ENOENT;

    value_start = strchr(field_start + strlen(pattern), ':');
    if (!value_start)
        return -EINVAL;
    value_start++;

    while (*value_start == ' ' || *value_start == '\t')
        value_start++;

    value_end = value_start;
    while (*value_end && *value_end != ',' && *value_end != '}' && 
           *value_end != ']' && *value_end != ' ' && *value_end != '\t' &&
           *value_end != '\n' && *value_end != '\r') {
        value_end++;
    }

    value_len = value_end - value_start;
    if (value_len == 0)
        return -EINVAL;
    
    if (value_len >= value_size)
        value_len = value_size - 1;

    memcpy(value, value_start, value_len);
    value[value_len] = '\0';

    return 0;
}

int vtfs_http_create(const char *path, const char *type, int mode)
{
    char response[VTFS_HTTP_BUFFER_SIZE];
    char mode_str[16];
    int ret;

    if (!http_initialized)
        return 0; // Not an error, just skip remote sync

    snprintf(mode_str, sizeof(mode_str), "%o", mode);

    ret = vtfs_http_call(vtfs_get_token(), "create", response, sizeof(response), 3,
                         "path", path,
                         "type", type,
                         "mode", mode_str);

    if (ret < 0) {
        return ret;
    }

    if (strstr(response, "\"error\"")) {
        return -EIO;
    }

    return 0;
}

int vtfs_http_write(const char *path, const void *data, size_t size, loff_t offset)
{
    char response[VTFS_HTTP_BUFFER_SIZE];
    char *base64_data;
    char offset_str[32];
    int ret;

    if (!http_initialized)
        return 0;

    base64_data = kmalloc(((size + 2) / 3) * 4 + 1, GFP_KERNEL);
    if (!base64_data)
        return -ENOMEM;

    ret = bytes_to_base64(data, size, base64_data, ((size + 2) / 3) * 4 + 1);
    if (ret) {
        kfree(base64_data);
        return ret;
    }

    snprintf(offset_str, sizeof(offset_str), "%lld", (long long)offset);

    ret = vtfs_http_call(vtfs_get_token(), "write", response, sizeof(response), 3,
                         "path", path,
                         "offset", offset_str,
                         "data", base64_data);

    kfree(base64_data);

    if (ret < 0) {
        return ret;
    }

    if (strstr(response, "\"error\"")) {
        return -EIO;
    }

    return size;
}

int vtfs_http_read(const char *path, void *buffer, size_t size, loff_t offset)
{
    char response[VTFS_HTTP_BUFFER_SIZE];
    char data_str[VTFS_HTTP_BUFFER_SIZE];
    char offset_str[32];
    char size_str[32];
    unsigned char *decoded;
    size_t decoded_len = size;
    const char *result_ptr;
    int ret;

    if (!http_initialized) {
        return -ENOENT;
    }

    snprintf(offset_str, sizeof(offset_str), "%lld", (long long)offset);
    snprintf(size_str, sizeof(size_str), "%zu", size);

    ret = vtfs_http_call(vtfs_get_token(), "read", response, sizeof(response), 3,
                         "path", path,
                         "offset", offset_str,
                         "size", size_str);

    if (ret != 0) {
        return -ENOENT;
    }

    if (strstr(response, "\"error\"")) {
        return -ENOENT;
    }

    result_ptr = strstr(response, "\"result\"");
    if (!result_ptr) {
        return -ENOENT;
    }

    result_ptr = strstr(result_ptr, "\"data\"");
    if (!result_ptr) {
        return -ENOENT;
    }

    ret = extract_json_field(result_ptr, "data", data_str, sizeof(data_str));
    if (ret) {
        return ret;
    }

    decoded = kmalloc(size, GFP_KERNEL);
    if (!decoded) {
        return -ENOMEM;
    }

    decoded_len = size;
    ret = base64_decode(data_str, decoded, &decoded_len);
    if (ret) {
        kfree(decoded);
        return ret;
    }

    if (decoded_len > size) {
        decoded_len = size;
    }
    memcpy(buffer, decoded, decoded_len);
    kfree(decoded);

    return decoded_len;
}

int vtfs_http_delete(const char *path)
{
    char response[VTFS_HTTP_BUFFER_SIZE];
    int ret;

    if (!http_initialized)
        return 0; // Not an error, just skip remote sync

    ret = vtfs_http_call(vtfs_get_token(), "delete", response, sizeof(response), 1,
                         "path", path);

    if (ret < 0) {
        return ret;
    }

    if (strstr(response, "\"error\"")) {
        return -EIO;
    }

    return 0;
}

int vtfs_http_stat(const char *path, umode_t *mode, loff_t *size)
{
    char response[VTFS_HTTP_BUFFER_SIZE];
    char type_str[16];
    char size_str[32];
    const char *result_start, *result_end;
    char result_json[VTFS_HTTP_BUFFER_SIZE];
    int ret;
    long long size_val;

    if (!http_initialized)
        return -ENOENT;

    ret = vtfs_http_call(vtfs_get_token(), "stat", response, sizeof(response), 1,
                         "path", path);

    if (ret < 0) {
        return ret;
    }

    if (strstr(response, "\"error\"")) {
        return -ENOENT;
    }

    result_start = strstr(response, "\"result\"");
    if (!result_start) {
        return -EIO;
    }
    
    result_start = strchr(result_start, '{');
    if (!result_start) {
        return -EIO;
    }
    
    result_end = result_start + 1;
    int brace_count = 1;
    while (*result_end && brace_count > 0) {
        if (*result_end == '{')
            brace_count++;
        else if (*result_end == '}')
            brace_count--;
        result_end++;
    }
    
    if (brace_count != 0) {
        return -EIO;
    }
    
    size_t result_len = result_end - result_start;
    if (result_len >= sizeof(result_json))
        result_len = sizeof(result_json) - 1;
    memcpy(result_json, result_start, result_len);
    result_json[result_len] = '\0';

    if (extract_json_field(result_json, "type", type_str, sizeof(type_str)) != 0) {
        return -EIO;
    }

    if (extract_json_number(result_json, "size", size_str, sizeof(size_str)) != 0) {
        return -EIO;
    }

    if (kstrtoll(size_str, 10, &size_val) != 0) {
        return -EIO;
    }

    if (mode) {
        if (strcmp(type_str, "file") == 0)
            *mode = S_IFREG | 0777;
        else if (strcmp(type_str, "dir") == 0)
            *mode = S_IFDIR | 0777;
        else {
            return -EIO;
        }
    }

    if (size)
        *size = size_val;

    return 0;
}
