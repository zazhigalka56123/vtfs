#ifndef _VTFS_STORAGE_H
#define _VTFS_STORAGE_H

#include <linux/list.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include "vtfs.h"

struct vtfs_entry {
    char name[VTFS_MAX_NAME_LEN + 1];
    ino_t ino;
    umode_t mode;
    
    char *data;
    size_t size;
    size_t capacity;
    
    unsigned int nlink;
    
    struct timespec64 atime;
    struct timespec64 mtime;
    struct timespec64 ctime;
    
    struct vtfs_entry *parent;
    struct list_head children;
    struct list_head sibling;
    struct list_head global_list;
};

struct vtfs_storage {
    struct vtfs_entry *root;
    struct list_head all_entries;
    spinlock_t lock;
    ino_t next_ino;
};

extern struct vtfs_storage vtfs_store;
int vtfs_storage_init(void);
void vtfs_storage_cleanup(void);
struct vtfs_entry *vtfs_storage_get_root(void);
struct vtfs_entry *vtfs_storage_create_entry(struct vtfs_entry *parent,
                                             const char *name,
                                             umode_t mode,
                                             ino_t ino);
struct vtfs_entry *vtfs_storage_create_entry_no_sync(struct vtfs_entry *parent,
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
void vtfs_get_full_path(struct vtfs_entry *entry, char *buf, size_t size);

#endif
