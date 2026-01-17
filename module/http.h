#ifndef _VTFS_HTTP_H
#define _VTFS_HTTP_H

#include <linux/types.h>

#define VTFS_HTTP_BUFFER_SIZE 4096
#define VTFS_HTTP_MAX_ARGS 10

int64_t vtfs_http_call(const char *token,
                       const char *method,
                       char *response_buffer,
                       size_t buffer_size,
                       size_t arg_size,
                       ...);

int vtfs_http_init(const char *server_url);
void vtfs_http_cleanup(void);
void vtfs_http_set_server(const char *url);

int vtfs_http_create(const char *path, const char *type, int mode);
int vtfs_http_write(const char *path, const void *data, size_t size, loff_t offset);
int vtfs_http_read(const char *path, void *buffer, size_t size, loff_t offset);
int vtfs_http_delete(const char *path);
int vtfs_http_stat(const char *path, umode_t *mode, loff_t *size);

#endif
