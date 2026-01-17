#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include "storage.h"
#include "vtfs.h"

extern struct vtfs_storage vtfs_store;

int vtfs_iterate(struct file *filp, struct dir_context *ctx)
{
    struct dentry *dentry = filp->f_path.dentry;
    struct inode *inode = d_inode(dentry);
    struct vtfs_entry *dir_entry, *child_entry;
    unsigned long offset = ctx->pos;
    ino_t parent_ino;
    int stored = 0;
    unsigned long flags;
    
    dir_entry = vtfs_storage_get_by_ino(inode->i_ino);
    if (!dir_entry)
        return -ENOENT;
    
    if (!S_ISDIR(dir_entry->mode))
        return -ENOTDIR;
    
    if (offset == 0) {
        if (!dir_emit(ctx, ".", 1, inode->i_ino, DT_DIR))
            return stored;
        ctx->pos++;
        stored++;
        offset++;
    }
    
    if (offset == 1) {
        if (dentry->d_parent && d_inode(dentry->d_parent))
            parent_ino = d_inode(dentry->d_parent)->i_ino;
        else
            parent_ino = inode->i_ino;
        
        if (!dir_emit(ctx, "..", 2, parent_ino, DT_DIR))
            return stored;
        ctx->pos++;
        stored++;
        offset++;
    }
    
    spin_lock_irqsave(&vtfs_store.lock, flags);
    
    list_for_each_entry(child_entry, &dir_entry->children, sibling) {
        unsigned char dtype;
        
        if (offset > 2) {
            offset--;
            continue;
        }
        
        if (S_ISDIR(child_entry->mode))
            dtype = DT_DIR;
        else if (S_ISREG(child_entry->mode))
            dtype = DT_REG;
        else
            dtype = DT_UNKNOWN;
        
        spin_unlock_irqrestore(&vtfs_store.lock, flags);
        
        if (!dir_emit(ctx, child_entry->name, strlen(child_entry->name),
                     child_entry->ino, dtype))
            return stored;
        
        spin_lock_irqsave(&vtfs_store.lock, flags);
        
        ctx->pos++;
        stored++;
    }
    
    spin_unlock_irqrestore(&vtfs_store.lock, flags);
    
    return stored;
}

const struct file_operations vtfs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = vtfs_iterate,
    .llseek = generic_file_llseek,
};
