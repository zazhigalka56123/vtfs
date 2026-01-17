#include <linux/fs.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "storage.h"
#include "http.h"
#include "vtfs.h"

static ssize_t vtfs_read(struct file *filp, char __user *buffer,
                         size_t len, loff_t *offset)
{
    struct inode *inode = file_inode(filp);
    struct vtfs_entry *entry;
    char *kbuffer;
    ssize_t bytes_read;
    
    entry = vtfs_storage_get_by_ino(inode->i_ino);
    if (!entry)
        return -ENOENT;
    
    if (!S_ISREG(entry->mode))
        return -EISDIR;
    
    if (*offset >= entry->size)
        return 0;
    
    kbuffer = kmalloc(len, GFP_KERNEL);
    if (!kbuffer)
        return -ENOMEM;
    
    bytes_read = -1;
    if (use_remote_server()) {
        char full_path[256];
        
        vtfs_get_full_path(entry, full_path, sizeof(full_path));
        bytes_read = vtfs_http_read(full_path, kbuffer, len, *offset);
    }
    
    if (bytes_read < 0) {
        bytes_read = vtfs_storage_read(entry, kbuffer, len, *offset);
        if (bytes_read < 0) {
            kfree(kbuffer);
            return bytes_read;
        }
    }
    
    if (copy_to_user(buffer, kbuffer, bytes_read)) {
        kfree(kbuffer);
        return -EFAULT;
    }
    
    kfree(kbuffer);
    *offset += bytes_read;
    inode->i_size = entry->size;
    
    return bytes_read;
}

static ssize_t vtfs_write(struct file *filp, const char __user *buffer,
                          size_t len, loff_t *offset)
{
    struct inode *inode = file_inode(filp);
    struct vtfs_entry *entry;
    char *kbuffer;
    ssize_t bytes_written;
    
    entry = vtfs_storage_get_by_ino(inode->i_ino);
    if (!entry)
        return -ENOENT;
    
    if (!S_ISREG(entry->mode))
        return -EISDIR;
    
    if (*offset + len > VTFS_MAX_FILE_SIZE)
        return -EFBIG;
    
    if (filp->f_flags & O_APPEND)
        *offset = entry->size;
    
    kbuffer = kmalloc(len, GFP_KERNEL);
    if (!kbuffer)
        return -ENOMEM;
    
    if (copy_from_user(kbuffer, buffer, len)) {
        kfree(kbuffer);
        return -EFAULT;
    }
    
    bytes_written = vtfs_storage_write(entry, kbuffer, len, *offset);
    
    if (bytes_written < 0) {
        kfree(kbuffer);
        return bytes_written;
    }
    
    if (use_remote_server()) {
        char full_path[256];
        vtfs_get_full_path(entry, full_path, sizeof(full_path));
        vtfs_http_write(full_path, kbuffer, len, *offset);
    }
    
    kfree(kbuffer);
    
    *offset += bytes_written;
    inode->i_size = entry->size;
    inode->__i_mtime = inode->__i_ctime = current_time(inode);
    
    return bytes_written;
}

const struct file_operations vtfs_file_ops = {
    .owner   = THIS_MODULE,
    .read    = vtfs_read,
    .write   = vtfs_write,
    .llseek  = generic_file_llseek,
};
