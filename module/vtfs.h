#ifndef _VTFS_H
#define _VTFS_H

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/time.h>
#include <linux/stat.h>

#define VTFS_MODULE_NAME "vtfs"
#define VTFS_MODULE_DESC "Virtual Trivial File System"
#define VTFS_MAGIC 0x56544653
#define VTFS_ROOT_INO 1000
#define VTFS_DEFAULT_MODE 0777
#define VTFS_MAX_NAME_LEN 255
#define VTFS_MAX_FILE_SIZE (1024 * 1024)

#define VTFS_LOG(fmt, ...) printk(KERN_INFO "[vtfs] " fmt, ##__VA_ARGS__)
#define VTFS_ERR(fmt, ...) printk(KERN_ERR "[vtfs] " fmt, ##__VA_ARGS__)
#define VTFS_DEBUG(fmt, ...) printk(KERN_DEBUG "[vtfs] " fmt, ##__VA_ARGS__)

struct vtfs_entry;

extern const struct inode_operations vtfs_inode_ops;
extern const struct inode_operations vtfs_file_inode_ops;
extern const struct file_operations vtfs_dir_ops;
extern const struct file_operations vtfs_file_ops;

bool use_remote_server(void);

struct inode *vtfs_get_inode(struct super_block *sb,
                              const struct inode *dir,
                              umode_t mode,
                              ino_t ino);

const char *vtfs_get_server_url(void);
const char *vtfs_get_token(void);

int vtfs_storage_init(void);
void vtfs_storage_cleanup(void);
struct vtfs_entry *vtfs_storage_get_root(void);
struct vtfs_entry *vtfs_storage_create_entry(struct vtfs_entry *parent,
                                              const char *name,
                                              umode_t mode,
                                              ino_t ino);
int vtfs_storage_delete_entry(struct vtfs_entry *entry);
struct vtfs_entry *vtfs_storage_lookup(struct vtfs_entry *parent,
                                        const char *name);
struct vtfs_entry *vtfs_storage_get_by_ino(ino_t ino);
int vtfs_storage_read(struct vtfs_entry *entry, char *buffer,
                      size_t len, loff_t offset);
int vtfs_storage_write(struct vtfs_entry *entry, const char *buffer,
                       size_t len, loff_t offset);
int vtfs_storage_add_link(struct vtfs_entry *entry,
                          struct vtfs_entry *parent,
                          const char *name);

#endif
