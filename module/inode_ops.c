#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/string.h>
#include <linux/mount.h>
#include "storage.h"
#include "http.h"
#include "vtfs.h"

static struct vtfs_entry *fetch_from_remote(struct vtfs_entry *parent, const char *name)
{
    struct vtfs_entry *entry;
    char full_path[512];
    umode_t mode;
    loff_t size;
    
    if (!use_remote_server())
        return NULL;
    
    vtfs_get_full_path(parent, full_path, sizeof(full_path));
    if (strcmp(full_path, "/") != 0)
        strlcat(full_path, "/", sizeof(full_path));
    strlcat(full_path, name, sizeof(full_path));
    
    if (vtfs_http_stat(full_path, &mode, &size) != 0)
        return NULL;
    
    entry = vtfs_storage_create_entry_no_sync(parent, name, mode, 0);
    if (!entry)
        return NULL;
    
    if (S_ISREG(mode) && size > 0) {
        char *buffer = kmalloc(size, GFP_KERNEL);
        if (buffer) {
            ssize_t bytes = vtfs_http_read(full_path, buffer, size, 0);
            if (bytes > 0)
                vtfs_storage_write(entry, buffer, bytes, 0);
            kfree(buffer);
        }
    }
    
    return entry;
}

static struct dentry *vtfs_lookup(struct inode *parent_inode,
                                  struct dentry *child_dentry,
                                  unsigned int flags)
{
    struct vtfs_entry *parent, *child;
    struct inode *inode = NULL;
    const char *name = child_dentry->d_name.name;
    
    parent = vtfs_storage_get_by_ino(parent_inode->i_ino);
    if (!parent)
        return ERR_PTR(-ENOENT);
    
    child = vtfs_storage_lookup(parent, name);
    if (!child)
        child = fetch_from_remote(parent, name);
    
    if (child) {
        inode = vtfs_get_inode(parent_inode->i_sb, parent_inode,
                               child->mode, child->ino);
        if (!inode)
            return ERR_PTR(-ENOMEM);
        
        set_nlink(inode, child->nlink);
        inode->i_size = child->size;
    }
    
    d_add(child_dentry, inode);
    return NULL;
}

static int vtfs_create(struct mnt_idmap *idmap,
                       struct inode *parent_inode,
                       struct dentry *child_dentry,
                       umode_t mode,
                       bool excl)
{
    struct vtfs_entry *parent, *entry;
    struct inode *inode;
    
    parent = vtfs_storage_get_by_ino(parent_inode->i_ino);
    if (!parent)
        return -ENOENT;
    
    entry = vtfs_storage_create_entry(parent, child_dentry->d_name.name,
                                      S_IFREG | (mode & 0777), 0);
    if (!entry)
        return -EEXIST;
    
    inode = vtfs_get_inode(parent_inode->i_sb, parent_inode,
                           entry->mode, entry->ino);
    if (!inode) {
        vtfs_storage_delete_entry(entry);
        return -ENOMEM;
    }
    
    d_instantiate(child_dentry, inode);
    parent_inode->__i_mtime = parent_inode->__i_ctime = current_time(parent_inode);
    return 0;
}

static int vtfs_unlink(struct inode *parent_inode, struct dentry *child_dentry)
{
    struct vtfs_entry *parent, *child;
    int ret;
    
    parent = vtfs_storage_get_by_ino(parent_inode->i_ino);
    if (!parent)
        return -ENOENT;
    
    child = vtfs_storage_lookup(parent, child_dentry->d_name.name);
    if (!child)
        return -ENOENT;
    
    ret = vtfs_storage_delete_entry(child);
    if (ret)
        return ret;
    
    drop_nlink(d_inode(child_dentry));
    parent_inode->__i_mtime = parent_inode->__i_ctime = current_time(parent_inode);
    return 0;
}

static int vtfs_mkdir(struct mnt_idmap *idmap,
                      struct inode *parent_inode,
                      struct dentry *child_dentry,
                      umode_t mode)
{
    struct vtfs_entry *parent, *entry;
    struct inode *inode;
    
    parent = vtfs_storage_get_by_ino(parent_inode->i_ino);
    if (!parent)
        return -ENOENT;
    
    entry = vtfs_storage_create_entry(parent, child_dentry->d_name.name,
                                      S_IFDIR | (mode & 0777), 0);
    if (!entry)
        return -EEXIST;
    
    entry->nlink = 2;
    
    inode = vtfs_get_inode(parent_inode->i_sb, parent_inode,
                           entry->mode, entry->ino);
    if (!inode) {
        vtfs_storage_delete_entry(entry);
        return -ENOMEM;
    }
    
    inc_nlink(parent_inode);
    d_instantiate(child_dentry, inode);
    parent_inode->__i_mtime = parent_inode->__i_ctime = current_time(parent_inode);
    return 0;
}

static int vtfs_rmdir(struct inode *parent_inode, struct dentry *child_dentry)
{
    struct vtfs_entry *parent, *child;
    int ret;
    
    parent = vtfs_storage_get_by_ino(parent_inode->i_ino);
    if (!parent)
        return -ENOENT;
    
    child = vtfs_storage_lookup(parent, child_dentry->d_name.name);
    if (!child)
        return -ENOENT;
    
    if (!S_ISDIR(child->mode))
        return -ENOTDIR;
    
    if (!list_empty(&child->children))
        return -ENOTEMPTY;
    
    ret = vtfs_storage_delete_entry(child);
    if (ret)
        return ret;
    
    clear_nlink(d_inode(child_dentry));
    drop_nlink(parent_inode);
    parent_inode->__i_mtime = parent_inode->__i_ctime = current_time(parent_inode);
    return 0;
}

static int vtfs_link(struct dentry *old_dentry,
                     struct inode *parent_dir,
                     struct dentry *new_dentry)
{
    struct vtfs_entry *target, *parent, *link;
    struct inode *inode = d_inode(old_dentry);
    int ret;
    
    target = vtfs_storage_get_by_ino(inode->i_ino);
    if (!target)
        return -ENOENT;
    
    if (S_ISDIR(target->mode))
        return -EPERM;
    
    parent = vtfs_storage_get_by_ino(parent_dir->i_ino);
    if (!parent)
        return -ENOENT;
    
    link = vtfs_storage_create_entry(parent, new_dentry->d_name.name,
                                     target->mode, target->ino);
    if (!link)
        return -EEXIST;
    
    link->data = target->data;
    link->size = target->size;
    link->capacity = target->capacity;
    
    ret = vtfs_storage_add_link(target, parent, new_dentry->d_name.name);
    if (ret) {
        vtfs_storage_delete_entry(link);
        return ret;
    }
    
    inc_nlink(inode);
    inode->__i_ctime = current_time(inode);
    ihold(inode);
    d_instantiate(new_dentry, inode);
    return 0;
}

const struct inode_operations vtfs_inode_ops = {
    .lookup = vtfs_lookup,
    .create = vtfs_create,
    .unlink = vtfs_unlink,
    .mkdir  = vtfs_mkdir,
    .rmdir  = vtfs_rmdir,
    .link   = vtfs_link,
};

const struct inode_operations vtfs_file_inode_ops = {
};
