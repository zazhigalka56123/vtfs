#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include "storage.h"
#include "http.h"
#include "vtfs.h"

struct vtfs_storage vtfs_store;

bool use_remote_server(void)
{
    const char *url = vtfs_get_server_url();
    return url && strlen(url) > 0;
}

static void build_path(struct vtfs_entry *entry, char *path, size_t size)
{
    if (!entry || entry == vtfs_store.root) {
        strncpy(path, "/", size);
        return;
    }
    
    if (entry->parent && entry->parent != vtfs_store.root) {
        build_path(entry->parent, path, size);
        strlcat(path, "/", size);
        strlcat(path, entry->name, size);
    } else {
        snprintf(path, size, "/%s", entry->name);
    }
}

void vtfs_get_full_path(struct vtfs_entry *entry, char *buf, size_t size)
{
    build_path(entry, buf, size);
}

static void sync_create_to_server(const char *path, const char *type)
{
    char response[256];
    
    if (!use_remote_server())
        return;
    
    vtfs_http_call(vtfs_get_token(), "create", response, sizeof(response),
                   2, "path", path, "type", type);
}

static void sync_delete_to_server(const char *path)
{
    char response[256];
    
    if (!use_remote_server())
        return;
    
    vtfs_http_call(vtfs_get_token(), "delete", response, sizeof(response),
                   1, "path", path);
}

static void sync_write_to_server(const char *path, const char *data, size_t len)
{
    if (!use_remote_server())
        return;
    
    vtfs_http_write(path, data, len, 0);
}

static struct vtfs_entry *alloc_entry(const char *name, umode_t mode, ino_t ino)
{
    struct vtfs_entry *entry;

    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry)
        return NULL;

    strncpy(entry->name, name, VTFS_MAX_NAME_LEN);
    entry->name[VTFS_MAX_NAME_LEN] = '\0';
    
    entry->mode = mode;
    entry->ino = ino;
    entry->nlink = 1;
    
    entry->data = NULL;
    entry->size = 0;
    entry->capacity = 0;
    entry->parent = NULL;

    ktime_get_real_ts64(&entry->atime);
    entry->mtime = entry->atime;
    entry->ctime = entry->atime;

    INIT_LIST_HEAD(&entry->children);
    INIT_LIST_HEAD(&entry->sibling);
    INIT_LIST_HEAD(&entry->global_list);

    return entry;
}

static void free_entry(struct vtfs_entry *entry)
{
    if (!entry)
        return;

    if (entry->data)
        kfree(entry->data);

    kfree(entry);
}

int vtfs_storage_init(void)
{
    spin_lock_init(&vtfs_store.lock);
    
    INIT_LIST_HEAD(&vtfs_store.all_entries);
    
    vtfs_store.next_ino = VTFS_ROOT_INO + 1;

    vtfs_store.root = alloc_entry("/", S_IFDIR | 0777, VTFS_ROOT_INO);
    if (!vtfs_store.root)
        return -ENOMEM;

    vtfs_store.root->parent = vtfs_store.root;
    vtfs_store.root->nlink = 2;

    list_add(&vtfs_store.root->global_list, &vtfs_store.all_entries);

    return 0;
}

static void free_entries_recursive(struct vtfs_entry *dir)
{
    struct vtfs_entry *child, *tmp;

    if (!dir)
        return;

    list_for_each_entry_safe(child, tmp, &dir->children, sibling) {
        if (S_ISDIR(child->mode))
            free_entries_recursive(child);
        
        list_del(&child->sibling);
        list_del(&child->global_list);
        free_entry(child);
    }
}

void vtfs_storage_cleanup(void)
{
    unsigned long flags;

    spin_lock_irqsave(&vtfs_store.lock, flags);

    if (vtfs_store.root) {
        free_entries_recursive(vtfs_store.root);
        
        list_del(&vtfs_store.root->global_list);
        
        free_entry(vtfs_store.root);
        vtfs_store.root = NULL;
    }

    spin_unlock_irqrestore(&vtfs_store.lock, flags);
}

struct vtfs_entry *vtfs_storage_get_root(void)
{
    return vtfs_store.root;
}

static struct vtfs_entry *create_entry_internal(struct vtfs_entry *parent,
                                                const char *name,
                                                umode_t mode,
                                                ino_t ino,
                                                bool skip_sync)
{
    struct vtfs_entry *entry;
    unsigned long flags;

    if (!parent || !S_ISDIR(parent->mode))
        return NULL;

    if (vtfs_storage_lookup(parent, name))
        return NULL;

    spin_lock_irqsave(&vtfs_store.lock, flags);

    if (ino == 0)
        ino = vtfs_store.next_ino++;

    entry = alloc_entry(name, mode, ino);
    if (!entry) {
        spin_unlock_irqrestore(&vtfs_store.lock, flags);
        return NULL;
    }

    entry->parent = parent;
    
    list_add(&entry->sibling, &parent->children);
    
    list_add(&entry->global_list, &vtfs_store.all_entries);

    if (S_ISDIR(mode))
        parent->nlink++;

    spin_unlock_irqrestore(&vtfs_store.lock, flags);

    if (!skip_sync && use_remote_server()) {
        char path[512];
        build_path(entry, path, sizeof(path));
        sync_create_to_server(path, S_ISDIR(mode) ? "dir" : "file");
    }

    return entry;
}

struct vtfs_entry *vtfs_storage_create_entry(struct vtfs_entry *parent,
                                             const char *name,
                                             umode_t mode,
                                             ino_t ino)
{
    return create_entry_internal(parent, name, mode, ino, false);
}

struct vtfs_entry *vtfs_storage_create_entry_no_sync(struct vtfs_entry *parent,
                                                     const char *name,
                                                     umode_t mode,
                                                     ino_t ino)
{
    return create_entry_internal(parent, name, mode, ino, true);
}

int vtfs_storage_delete_entry(struct vtfs_entry *entry)
{
    unsigned long flags;

    if (!entry)
        return -EINVAL;

    if (entry == vtfs_store.root)
        return -EBUSY;

    if (S_ISDIR(entry->mode) && !list_empty(&entry->children))
        return -ENOTEMPTY;

    spin_lock_irqsave(&vtfs_store.lock, flags);

    entry->nlink--;

    if (entry->nlink == 0) {
        char path[512];
        if (use_remote_server()) {
            build_path(entry, path, sizeof(path));
        }
        
        if (S_ISDIR(entry->mode) && entry->parent)
            entry->parent->nlink--;

        list_del(&entry->sibling);
        list_del(&entry->global_list);

        spin_unlock_irqrestore(&vtfs_store.lock, flags);

        free_entry(entry);
        
        if (use_remote_server()) {
            sync_delete_to_server(path);
        }
    } else {
        spin_unlock_irqrestore(&vtfs_store.lock, flags);
    }

    return 0;
}

struct vtfs_entry *vtfs_storage_lookup(struct vtfs_entry *parent,
                                       const char *name)
{
    struct vtfs_entry *child;
    unsigned long flags;

    if (!parent || !S_ISDIR(parent->mode))
        return NULL;

    spin_lock_irqsave(&vtfs_store.lock, flags);

    list_for_each_entry(child, &parent->children, sibling) {
        if (strcmp(child->name, name) == 0) {
            spin_unlock_irqrestore(&vtfs_store.lock, flags);
            return child;
        }
    }

    spin_unlock_irqrestore(&vtfs_store.lock, flags);
    return NULL;
}

struct vtfs_entry *vtfs_storage_get_by_ino(ino_t ino)
{
    struct vtfs_entry *entry;
    unsigned long flags;

    spin_lock_irqsave(&vtfs_store.lock, flags);

    list_for_each_entry(entry, &vtfs_store.all_entries, global_list) {
        if (entry->ino == ino) {
            spin_unlock_irqrestore(&vtfs_store.lock, flags);
            return entry;
        }
    }

    spin_unlock_irqrestore(&vtfs_store.lock, flags);
    return NULL;
}

int vtfs_storage_read(struct vtfs_entry *entry, char *buffer,
                      size_t len, loff_t offset)
{
    size_t bytes_to_read;
    unsigned long flags;

    if (!entry || !S_ISREG(entry->mode))
        return -EINVAL;

    spin_lock_irqsave(&vtfs_store.lock, flags);

    if (offset >= entry->size) {
        spin_unlock_irqrestore(&vtfs_store.lock, flags);
        return 0;
    }

    bytes_to_read = min(len, entry->size - (size_t)offset);

    if (entry->data && bytes_to_read > 0)
        memcpy(buffer, entry->data + offset, bytes_to_read);

    ktime_get_real_ts64(&entry->atime);

    spin_unlock_irqrestore(&vtfs_store.lock, flags);

    return bytes_to_read;
}

int vtfs_storage_write(struct vtfs_entry *entry, const char *buffer,
                       size_t len, loff_t offset)
{
    size_t new_size;
    char *new_data;
    unsigned long flags;

    if (!entry || !S_ISREG(entry->mode))
        return -EINVAL;

    new_size = offset + len;

    if (new_size > VTFS_MAX_FILE_SIZE)
        return -EFBIG;

    spin_lock_irqsave(&vtfs_store.lock, flags);

    if (new_size > entry->capacity) {
        size_t new_capacity = max(new_size, entry->capacity * 2);
        if (new_capacity < 64)
            new_capacity = 64;

        new_data = krealloc(entry->data, new_capacity, GFP_ATOMIC);
        if (!new_data) {
            spin_unlock_irqrestore(&vtfs_store.lock, flags);
            return -ENOMEM;
        }

        if (offset > entry->size)
            memset(new_data + entry->size, 0, offset - entry->size);

        entry->data = new_data;
        entry->capacity = new_capacity;
    }

    memcpy(entry->data + offset, buffer, len);

    if (new_size > entry->size)
        entry->size = new_size;

    ktime_get_real_ts64(&entry->mtime);
    entry->ctime = entry->mtime;

    spin_unlock_irqrestore(&vtfs_store.lock, flags);

    if (use_remote_server()) {
        char path[512];
        build_path(entry, path, sizeof(path));
        sync_write_to_server(path, buffer, len);
    }

    return len;
}

int vtfs_storage_add_link(struct vtfs_entry *entry,
                          struct vtfs_entry *parent,
                          const char *name)
{
    unsigned long flags;

    if (!entry || !parent)
        return -EINVAL;

    if (S_ISDIR(entry->mode))
        return -EPERM;

    spin_lock_irqsave(&vtfs_store.lock, flags);
    entry->nlink++;
    spin_unlock_irqrestore(&vtfs_store.lock, flags);

    return 0;
}
