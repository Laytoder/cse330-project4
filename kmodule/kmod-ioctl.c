#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/limits.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>
#include <linux/cdev.h>
#include <linux/nospec.h>

#include "../ioctl-defines.h"

/* USB storage disk-related data structures */
extern struct block_device*     bdevice;
extern struct bio*              bdevice_bio;

/* Device-related definitions */
static dev_t            dev = 0;
static struct class*    kmod_class;
static struct cdev      kmod_cdev;

/* Buffers for different operation requests */
struct block_rw_ops         rw_request;
struct block_rwoffset_ops   rwoffset_request;

char * kernel_buffer;
char * kernel_buffer_copy;
int page_number;

unsigned int curr_offset = 0;

unsigned int num_buffers;

static long kmod_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
    curr_offset = 0;
    printk("reached here\n");
    switch (cmd)
    {
        case BREAD:
        case BWRITE:
            printk("reached here copy 1\n");
            /* Get request from user */
            if(copy_from_user((void*) &rw_request, (void*)arg, sizeof(struct block_rw_ops))){
                printk("Error: User didn't send right message.\n");
                return -1;
            }

            kernel_buffer = (char*)(vmalloc(rw_request.size));
            kernel_buffer_copy = kernel_buffer;

            printk("reached here copy 2\n");
            if(copy_from_user(kernel_buffer, rw_request.data, rw_request.size)){
                printk("Error: User didn't send right message.\n");
                return -1;
            }

            printk("reached here copy 3\n");

            /* Allocate a kernel buffer to read/write user data */
            num_buffers = rw_request.size / 512;
            page_number = 0;
            if (cmd == BREAD) {
                printk("reached here 5\n");
                printk("%u\n", num_buffers);
                bdevice_bio = bio_alloc(bdevice, num_buffers, REQ_OP_READ, GFP_NOIO);
                printk("\nreached here 6\n");
                bio_set_dev(bdevice_bio, bdevice);
                printk("reached here 7\n");
                bdevice_bio->bi_iter.bi_sector = curr_offset;
                bdevice_bio->bi_opf = REQ_OP_READ;
                for(int i = 0; i < num_buffers; i++) {
                    bio_add_page(bdevice_bio, vmalloc_to_page(kernel_buffer), 512, curr_offset);
                    submit_bio_wait(bdevice_bio);
                    bio_reset(bdevice_bio, bdevice, FMODE_READ);
                    printk("bc\n");
                    printk("%u\n", curr_offset);
                    if (curr_offset > 4096) {
                        printk("reached here aadeesh\n");
                        page_number += 1;
                        kernel_buffer = kernel_buffer_copy + 4096 * page_number;
                        curr_offset = 0;
                        kernel_buffer_copy = kernel_buffer;
                    }
                    curr_offset = curr_offset + 512;
                    bdevice_bio->bi_iter.bi_sector = curr_offset;
                }
                printk("reached here 8\n");
            }
            else {
                bdevice_bio = bio_alloc(bdevice, num_buffers, REQ_OP_WRITE, GFP_NOIO);
                bio_set_dev(bdevice_bio, bdevice);
                bdevice_bio->bi_iter.bi_sector = curr_offset;
                bdevice_bio->bi_opf = REQ_OP_WRITE;
                for(int i = 0; i < num_buffers; i++) {
                    bio_add_page(bdevice_bio, vmalloc_to_page(kernel_buffer), 512, curr_offset);
                    submit_bio_wait(bdevice_bio);
                    bio_reset(bdevice_bio, bdevice, FMODE_WRITE);
                    curr_offset = curr_offset + 1;
                    bdevice_bio->bi_iter.bi_sector = curr_offset;
                }
            }

            /* Perform the block operation */

            if(copy_to_user(rw_request.data, kernel_buffer, rw_request.size)){
                printk("Error: Copying data to user.\n");
                return -1;
            }

            return 0;
        case BREADOFFSET:
        case BWRITEOFFSET:
            /* Get request from user */
            printk("reached here copy 4\n");
            if(copy_from_user((void*) &rwoffset_request, (void*)arg, sizeof(struct block_rwoffset_ops))){
                printk("Error: User didn't send right message.\n");
                return -1;
            }

            printk("reached here copy 5\n");

            kernel_buffer = (char*)(vmalloc(rw_request.size));

            if(copy_from_user(kernel_buffer, rwoffset_request.data, rwoffset_request.size)){
                printk("Error: User didn't send right message.\n");
                return -1;
            }

            printk("reached here copy 6\n");

            /* Allocate a kernel buffer to read/write user data */
            num_buffers = rwoffset_request.size / 512;
            if (cmd == BREADOFFSET) {
                printk("reached here 1\n");
                curr_offset = rwoffset_request.offset;
                bdevice_bio = bio_alloc(bdevice, num_buffers, REQ_OP_READ, GFP_NOIO);
                printk("reached here 2\n");
                bio_set_dev(bdevice_bio, bdevice);
                printk("reached here 3\n");
                bdevice_bio->bi_iter.bi_sector = curr_offset;
                bdevice_bio->bi_opf = REQ_OP_READ;
                for(int i = 0; i < num_buffers; i++) {
                    bio_add_page(bdevice_bio, vmalloc_to_page(kernel_buffer), 4096, curr_offset);
                    submit_bio_wait(bdevice_bio);
                    bio_reset(bdevice_bio, bdevice, FMODE_READ);
                    curr_offset = curr_offset + 1;
                    bdevice_bio->bi_iter.bi_sector = curr_offset;
                }
                printk("reached here 4\n");
            }
            else {
                curr_offset = rwoffset_request.offset;
                bdevice_bio = bio_alloc(bdevice, num_buffers, REQ_OP_WRITE, GFP_NOIO);
                bio_set_dev(bdevice_bio, bdevice);
                bdevice_bio->bi_iter.bi_sector = curr_offset;
                bdevice_bio->bi_opf = REQ_OP_WRITE;
                for(int i = 0; i < num_buffers; i++) {
                    bio_add_page(bdevice_bio, vmalloc_to_page(kernel_buffer), 4096, curr_offset);
                    submit_bio_wait(bdevice_bio);
                    bio_reset(bdevice_bio, bdevice, FMODE_WRITE);
                    curr_offset = curr_offset + 1;
                    bdevice_bio->bi_iter.bi_sector = curr_offset;
                }
            }

            /* Perform the block operation */

            if(copy_to_user(rw_request.data, kernel_buffer, rw_request.size)){
                printk("Error: Copying data to user.\n");
                return -1;
            }

            return 0;
        default: 
            printk("Error: incorrect operation requested, returning.\n");
            return -1;
    }
    return 0;
}

static int kmod_open(struct inode* inode, struct file* file) {
    printk("Opened kmod. \n");
    return 0;
}

static int kmod_release(struct inode* inode, struct file* file) {
    printk("Closed kmod. \n");
    return 0;
}

static struct file_operations fops = 
{
    .owner          = THIS_MODULE,
    .open           = kmod_open,
    .release        = kmod_release,
    .unlocked_ioctl = kmod_ioctl,
};

/* Initialize the module for IOCTL commands */
bool kmod_ioctl_init(void) {

    /* Allocate a character device. */
    if (alloc_chrdev_region(&dev, 0, 1, "usbaccess") < 0) {
        printk("error: couldn't allocate \'usbaccess\' character device.\n");
        return false;
    }

    /* Initialize the chardev with my fops. */
    cdev_init(&kmod_cdev, &fops);
    if (cdev_add(&kmod_cdev, dev, 1) < 0) {
        printk("error: couldn't add kmod_cdev.\n");
        goto cdevfailed;
    }

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6,2,16)
    if ((kmod_class = class_create(THIS_MODULE, "kmod_class")) == NULL) {
        printk("error: couldn't create kmod_class.\n");
        goto cdevfailed;
    }
#else
    if ((kmod_class = class_create("kmod_class")) == NULL) {
        printk("error: couldn't create kmod_class.\n");
        goto cdevfailed;
    }
#endif

    if ((device_create(kmod_class, NULL, dev, NULL, "kmod")) == NULL) {
        printk("error: couldn't create device.\n");
        goto classfailed;
    }

    printk("[*] IOCTL device initialization complete.\n");
    return true;

classfailed:
    class_destroy(kmod_class);
cdevfailed:
    unregister_chrdev_region(dev, 1);
    return false;
}

void kmod_ioctl_teardown(void) {
    /* Destroy the classes too (IOCTL-specific). */
    if (kmod_class) {
        device_destroy(kmod_class, dev);
        class_destroy(kmod_class);
    }
    cdev_del(&kmod_cdev);
    unregister_chrdev_region(dev,1);
    printk("[*] IOCTL device teardown complete.\n");
}
