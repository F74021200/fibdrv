#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 92

static char test_buf[30];
static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);
static void adder(unsigned long long *,
                  unsigned long long *,
                  unsigned long long *,
                  unsigned long long *);
static bool fib_num_to_str(char *str, int size, unsigned long long *n);
static bool rev_str(char *str);

static bool rev_str(char *str)
{
    char *start, *end, tmp[1];
    int sz = 0;

    sz = strlen(str);
    if (sz == 0)
        return false;
    if (sz == 1)
        return true;

    start = str;
    end = start + sz - 1;
    sz = sz >> 1;

    for (int i = 0; i < sz; i++) {
        tmp[0] &= 0;
        tmp[0] |= start[0];
        start[0] &= 0;
        start[0] |= end[0];
        end[0] &= 0;
        end[0] |= tmp[0];
        ++start;
        --end;
    }
    return true;
}
static bool fib_num_to_str(char *str, int size, unsigned long long *n)
{
    unsigned long long n_tmp = 0, tmp = 0;
    int i = 0;

    if (size == 0)
        return false;

    for (int i = 0; i < size; i++) {
        str[i] &= 0;
    }

    i = 0;
    n_tmp = *n;
    while (1) {
        tmp = n_tmp / 10;
        n_tmp = n_tmp - (tmp << 3) - (tmp << 1);

        switch (n_tmp) {
        case 9:
            str[i++] |= '9';
            break;
        case 8:
            str[i++] |= '8';
            break;
        case 7:
            str[i++] |= '7';
            break;
        case 6:
            str[i++] |= '6';
            break;
        case 5:
            str[i++] |= '5';
            break;
        case 4:
            str[i++] |= '4';
            break;
        case 3:
            str[i++] |= '3';
            break;
        case 2:
            str[i++] |= '2';
            break;
        case 1:
            str[i++] |= '1';
            break;
        case 0:
            str[i++] |= '0';
            break;
        }
        if (tmp == 0)
            break;
        if (i >= size)
            return false;
        n_tmp = tmp;
    }
    if (!rev_str(str))
        return false;
    return true;
};
static void adder(unsigned long long *c,
                  unsigned long long *s,
                  unsigned long long *a,
                  unsigned long long *b)
{
    unsigned long long a1, b1;

    a1 = *a;
    b1 = *b;

    for (int i = 0; i < 64; i++) {
        *s = a1 ^ b1;
        *c = (a1 & b1) << 1;
        a1 = *s;
        b1 = *c;
    }
}

static unsigned long long fib_sequence(long long k)
{
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    unsigned long long f[k + 2], s = 0, c = 0;

    f[0] = 0;
    f[1] = 1;

    for (int i = 2; i <= k; i++) {
        s = 0;
        c = 0;
        adder(&c, &s, &f[i - 1], &f[i - 2]);
        f[i] = s;
    }

    return f[k];
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char __user *user_buf,
                        size_t size,
                        loff_t *offset)
{
    unsigned long long fib_n;

    fib_n = fib_sequence(*offset);
    if (!fib_num_to_str(test_buf, 30, &fib_n))
        strncpy(test_buf, "not enough buf size.\n", 30);
    copy_to_user(user_buf, test_buf, 30);
    return (ssize_t) fib_n;
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    cdev_init(fib_cdev, &fib_fops);
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
