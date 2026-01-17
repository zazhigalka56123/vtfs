#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/uidgid.h>
#include <linux/mnt_idmapping.h>
#include <linux/pagemap.h>
#include "storage.h"
#include "http.h"
#include "vtfs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ivan Pasechnik");
MODULE_DESCRIPTION("Virtual Trivial File System");

static char *server_url = "http://127.0.0.1:8080";
static char *token = "";

module_param(token, charp, 0644);
MODULE_PARM_DESC(token, "Authentication token for remote server");
const char *vtfs_get_server_url(void)
{
    return server_url;
}

const char *vtfs_get_token(void)
{
    return token;
}

struct inode *vtfs_get_inode(struct super_block *sb,
                              const struct inode *dir,
                              umode_t mode,
                              ino_t ino)
{
    struct inode *inode;

    inode = new_inode(sb);
    if (!inode)
        return NULL;

    inode->i_ino = ino;
    
    inode->i_mode = mode;
    if (dir) {
        inode->i_uid = dir->i_uid;
        inode->i_gid = dir->i_gid;
    } else {
        inode->i_uid = GLOBAL_ROOT_UID;
        inode->i_gid = GLOBAL_ROOT_GID;
    }
    
    inode->__i_atime = inode->__i_mtime = inode->__i_ctime = current_time(inode);

    if (S_ISDIR(mode)) {
        inode->i_op = &vtfs_inode_ops;
        inode->i_fop = &vtfs_dir_ops;
        set_nlink(inode, 2);
    } else if (S_ISREG(mode)) {
        inode->i_op = &vtfs_file_inode_ops;
        inode->i_fop = &vtfs_file_ops;
        set_nlink(inode, 1);
    }

    return inode;
}

static int vtfs_drop_inode(struct inode *inode)
{
    return 1;
}

static const struct super_operations vtfs_super_ops = {
    .statfs     = simple_statfs,
    .drop_inode = vtfs_drop_inode,
};

static int vtfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct inode *inode;
    
    sb->s_magic = 0x56544653;
    
    sb->s_blocksize = PAGE_SIZE;
    
    sb->s_blocksize_bits = PAGE_SHIFT;
    
    sb->s_op = &vtfs_super_ops;
    
    sb->s_maxbytes = VTFS_MAX_FILE_SIZE;
    
    struct vtfs_entry *root_entry = vtfs_storage_get_root();
    if (!root_entry)
        return -ENOMEM;
    
    inode = vtfs_get_inode(sb, NULL, S_IFDIR | 0777, VTFS_ROOT_INO);
    if (!inode) {
        return -ENOMEM;
    }
    
    sb->s_root = d_make_root(inode);
    if (!sb->s_root) {
        return -ENOMEM;
    }
    
    return 0;
}

static struct dentry *vtfs_mount(struct file_system_type *fs_type,
                                  int flags,
                                  const char *dev_name,
                                  void *data)
{
    struct dentry *ret;
    
    ret = mount_nodev(fs_type, flags, data, vtfs_fill_super);
    
    return ret;
}

static void vtfs_kill_sb(struct super_block *sb)
{
    if (sb->s_root && sb->s_root->d_inode) {
        truncate_inode_pages_final(&sb->s_root->d_inode->i_data);
    }
    
    kill_anon_super(sb);
}

static struct file_system_type vtfs_fs_type = {
    .name = "vtfs",
    .mount = vtfs_mount,
    .kill_sb = vtfs_kill_sb,
    .owner = THIS_MODULE,
};

static int __init vtfs_init(void)
{
    int ret;
    
    if (server_url && strlen(server_url) > 0) {
        ret = vtfs_http_init(server_url);
        if (ret) {
            printk(KERN_ERR "[vtfs] Failed to initialize HTTP client\n");
            return ret;
        }
    }
    
    ret = vtfs_storage_init();
    if (ret) {
        printk(KERN_ERR "[vtfs] Failed to initialize storage\n");
        if (server_url && strlen(server_url) > 0)
            vtfs_http_cleanup();
        return ret;
    }
    
    ret = register_filesystem(&vtfs_fs_type);
    
    if (ret) {
        vtfs_storage_cleanup();
        if (server_url && strlen(server_url) > 0)
            vtfs_http_cleanup();
        printk(KERN_ERR "[vtfs] Failed to register filesystem\n");
        return ret;
    }
    
    return 0;
}

static void __exit vtfs_exit(void)
{
    unregister_filesystem(&vtfs_fs_type);
    
    vtfs_storage_cleanup();
    
    if (server_url && strlen(server_url) > 0)
        vtfs_http_cleanup();
}

module_init(vtfs_init);
module_exit(vtfs_exit);
