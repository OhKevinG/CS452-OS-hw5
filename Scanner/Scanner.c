#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BSU CS 452 HW5");
MODULE_AUTHOR("<kgutierrez@u.boisestate.edu>");

typedef struct {
  dev_t devno;
  struct cdev cdev;
  // Default seperators
  char *def_sep; // stores the default seperator set
  size_t def_sep_len;
} Device;			/* per-init() data */

typedef struct {
  // fd's seperator set (starts as copy of def_sep)
  char *sep;
  size_t sep_len;

  // Data to scan (buffer) 
  char *buf;
  size_t buf_len;

  // Tells scanner that the next write is separator(s) not a token
  int next_write_sep;

  size_t pos;     // where we are in buf
  size_t t0;      // start of current token in buf
  size_t tlen;    // length of current token
  size_t tgot;    // bytes of current token already given to read()
  int end;        // next read() should return 0 (end of token)

} File;				/* per-open() data */

static Device device;

// determines if a character is a separator
// returns 1 if the character is a separator, 0 otherwise
static int is_sep(File *f, char c) {
  int i;

  for (i=0;i<f->sep_len;i++) {
    if (f->sep[i]==c)
      return 1;
  }
  return 0;
}

// resets the scan to the beginning of the buffer
static void scan_reset(File *f) {
  f->pos = 0;
  f->t0 = 0;
  f->tlen = 0;
  f->tgot = 0;
  f->end = 0;
}

// One open() means one scanner instance
// returns an fd to the user
static int open(struct inode *inode, struct file *filp) {
  File *file=(File *)kmalloc(sizeof(*file),GFP_KERNEL);
  if (!file) {
    printk(KERN_ERR "%s: kmalloc() failed\n",DEVNAME);
    return -ENOMEM;
  }
  file->sep_len=device.def_sep_len;
  file->sep=(char *)kmalloc(file->sep_len, GFP_KERNEL);
  if (!file->sep) {
    printk(KERN_ERR "%s: kmalloc() failed\n",DEVNAME);
    kfree(file);
    return -ENOMEM;
  }
  memcpy(file->sep, device.def_sep, file->sep_len);

  file->buf = NULL;
  file->buf_len = 0;
  file->next_write_sep = 0;
  scan_reset(file);

  filp->private_data=file;
  return 0;
}

// frees the file
static int release(struct inode *inode, struct file *filp) {
  File *file=filp->private_data;
  kfree(file->sep);
  kfree(file->buf);
  kfree(file);
  return 0;
}

static ssize_t read(struct file *filp,
    char *buf,
    size_t count,
    loff_t *f_pos) {
  File *file=filp->private_data;
  size_t n;

  // Nothing to scan
  if (count == 0)
      return 0;
  if(!file->buf || file->buf_len == 0)
      return -ENODATA;

  // "End of this token" marker from last read of a non-empty token
  if (file->end) {
    file->end = 0;
    return 0;
  }

  // Finishing a long token
  if (file->tgot < file->tlen) {
    n = file->tlen - file->tgot;
    if (n > count)
      n = count;
    if (copy_to_user(buf, file->buf + file->t0 + file->tgot, n)) { // SEEMS ADVANCED //
      printk(KERN_ERR "%s: copy_to_user() failed\n", DEVNAME);
      return -EFAULT;
    }
    file->tgot += n;
    if (file->tgot < file->tlen)
      return n;
    // Last chunk of this token
    file->end = 1;
    return n;
  }

  // At end of token, find next token
  if (file->pos == file->buf_len)
    return -ENODATA;

  // Empty token: current byte is a sperator
  if (is_sep(file, file->buf[file->pos])) {
    file->pos++;
    return 0;
  }

  file->t0 = file->pos;
  while (file->pos < file->buf_len 
          && !is_sep(file, file->buf[file->pos]))
      file->pos++;

  // file->pos is on a sperator or at buf_len
  file->tlen = file->pos - file->t0;
  file->tgot = 0;

  n = file->tlen;
  if (n > count)
    n = count;
  if (n > 0) {
    if (copy_to_user(buf, file->buf + file->t0, n)) {
      printk(KERN_ERR "%s: copy_to_user() failed\n", DEVNAME);
      return -EFAULT;
    }
    file->tgot = n;
  }
  if (file->tgot < file->tlen) {
    // Token longer than this read; more later
    return n;
  }

  // Whole token fits this read
  file->end = 1;
  return n;
}

static ssize_t write(struct file *filp,
    const char *buf, 
    size_t count,
    loff_t *f_pos) 
{
  File *file = filp->private_data;
  char *n;

  if (file->next_write_sep) {
    // New seperator bytes: ioctl(0) then this write
    n = NULL;
    if (count) {
      n = (char *)kmalloc(count, GFP_KERNEL);
      if (!n)
        return -ENOMEM;
      if (copy_from_user(n, buf, count)) {
        kfree(n);
        printk(KERN_ERR "%s: copy_from_user() failed\n", DEVNAME);
        return -EFAULT;
      }
    }
    kfree(file->sep);
    file->sep = n;
    file->sep_len = count;
    file->next_write_sep = 0;
  } else {
    // New sequence to scan
    n = NULL;
    if (count) {
      n = (char *)kmalloc(count, GFP_KERNEL);
      if(!n)
        return -ENOMEM;
      if (copy_from_user(n, buf, count)) {
        kfree(n);
        printk(KERN_ERR "%s: copy_from_user() failed\n", DEVNAME);
        return -EFAULT;
      }
    }
    kfree(file->buf);
    file->buf = n;
    file->buf_len = count;
  }
  scan_reset(file);
  return count;
}

static long ioctl(struct file *filp,
    unsigned int cmd,
    unsigned long arg) {
  File *file = filp->private_data;

  if (cmd != 0)
      return -EINVAL;

  file->next_write_sep = 1;
  return 0;
}

static struct file_operations ops={
  .open=open,
  .release=release,
  .read=read,
  .write=write,
  .unlocked_ioctl=ioctl,
  .owner=THIS_MODULE
};

static int __init my_init(void) {
  // Default seperators: space, tab, newline, colon

  const char def[] = {' ', '\t', '\n', ':'};
  int err;
  device.def_sep_len = sizeof(def);
  device.def_sep=(char *)kmalloc(device.def_sep_len,GFP_KERNEL);
  if (!device.def_sep) {
    printk(KERN_ERR "%s: kmalloc() failed\n",DEVNAME);
    return -ENOMEM;
  }
  memcpy(device.def_sep, def, device.def_sep_len); // Don't have to use kmalloc and memcpy?
  err=alloc_chrdev_region(&device.devno,0,1,DEVNAME);
  if (err<0) {
    printk(KERN_ERR "%s: alloc_chrdev_region() failed\n",DEVNAME);
    kfree(device.def_sep);
    return err;
  }
  cdev_init(&device.cdev,&ops);
  device.cdev.owner=THIS_MODULE;
  err=cdev_add(&device.cdev,device.devno,1);
  if (err) {
    printk(KERN_ERR "%s: cdev_add() failed\n",DEVNAME);
    unregister_chrdev_region(device.devno, 1);
    kfree(device.def_sep);
    return err;
  }
  printk(KERN_INFO "%s: init\n",DEVNAME);
  return 0;
}

static void __exit my_exit(void) {
  cdev_del(&device.cdev);
  unregister_chrdev_region(device.devno,1);
  kfree(device.def_sep);
  printk(KERN_INFO "%s: exit\n",DEVNAME);
}

module_init(my_init);
module_exit(my_exit);
