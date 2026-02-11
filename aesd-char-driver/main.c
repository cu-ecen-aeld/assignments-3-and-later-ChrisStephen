/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Chris Stephen");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

    // AESD device pointer
    struct aesd_dev *device;
    // Find aesd device associated with character device
    device = container_of(inode->i_cdev, struct aesd_dev, cdev);
    // Save aesd device for other other methods
    filp->private_data = device;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    // Initialize return check as zero
    int rc = 0;
    // Grab aesd device
    struct aesd_dev *device = filp->private_data;
    // Lock mutex
    rc = mutex_lock_interruptible(&device->mtx);
    // Check error conditions
    if (rc != 0)
    {
	    // Error condition
	    return rc;
    }
    // Initialize offset as 0
    size_t offset = 0;
    // Initialize entry as NULL
    struct aesd_buffer_entry *entry = NULL;
    // Find buffer entry/offset associated with current position
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&device->cb, *f_pos, &offset);
    // Check that data exists
    if (entry != NULL)
    {
	    // Compute characters remaining in buffer
	    size_t characters = entry->size - offset;
	    // Check whether characters exceeds count
	    if (characters > count)
	    {
		    // Limit characters as count
		    characters = count;
	    }
	    // Move data from kernel space to user space
	    rc = copy_to_user(buf, entry->buffptr + offset, characters);
	    // Check error conditions
	    if (rc != 0)
	    {
		    // Release mutex
		    mutex_unlock(&device->mtx);
		    // Error condition
		    return rc;
	    }
	    // Bump current position by characters operated over
	    *f_pos += characters;
	    // Assign return value as characters operated over
	    retval = characters;

    }
    // Unlock mutex
    mutex_unlock(&device->mtx);

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    // Initialize return check as zero
    int rc = 0;
    // Grab aesd device
    struct aesd_dev *device = filp->private_data;
    // Lock mutex
    rc = mutex_lock_interruptible(&device->mtx);
    // Check error conditions
    if (rc != 0)
    {
	    return rc;
    }
    // Compute characters in updated buffer
    size_t characters = device->be.size + count;
    // Allocate updated buffer
    char *buffer = kmalloc(characters, GFP_KERNEL);
    // Check error conditions
    if (buffer == NULL)
    {
	    // Release mutex
	    mutex_unlock(&device->mtx);
	    // Error condition
	    return rc;
    }
    // Check for working buffer
    if (device->be.buffptr != NULL)
    {
	    // Initialize updated buffer with working buffer
	    memcpy(buffer, device->be.buffptr, device->be.size);
	    // Free working buffer
	    kfree(device->be.buffptr);
    }
    // Assign working buffer as updated buffer
    device->be.buffptr = buffer;
    // Move data from user space to kernel space
    rc = copy_from_user(device->be.buffptr + device->be.size, buf, count);
    // Check error conditions
    if (rc != 0)
    {
	    // Release mutex
	    mutex_unlock(&device->mtx);
	    // Error condition
	    return rc;
    }
    // Update working buffer size
    device->be.size = characters;
    // Check whether working buffer newline terminated
    if (buffer[characters - 1] == '\n')
    {
	    // Check whether circular buffer full
	    if (device->cb.full)
	    {
		    // Free buffer that will be overriden
		    kfree(device->cb.entry[device->cb.in_offs].buffptr);
	    }
	    // Create new entry of circular buffer
	    aesd_circular_buffer_add_entry(&device->cb, &device->be);
	    // Clear working buffer
	    memset(&device->be, 0, sizeof(struct aesd_buffer_entry));
    }
    // Assign return value as characters operated over
    retval = count;
    // Unlock mutex
    mutex_unlock(&device->mtx);

    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
	// Grab aesd device
	struct aesd_dev *device = filp->private_data;
	// Initialize position as zero
	loff_t fpos = 0;
	// Lock mutex
	int rc = mutex_lock_interruptible(&device->mtx);
	// Check error conditions
	if (rc != 0)
	{
		// Error condition
		return -EINVAL;
	}
	// Match whence positional type
	switch (whence)
	{
		case SEEK_SET:
			// Assign position as specified offset
			fpos = offset;
			// Case complete
			break;
		case SEEK_CUR:
			// Bump position by specified offset
			fpos = filp->f_pos + offset;
			// Case complete
			break;
		case SEEK_END:
			// Initialize size as zero
			size_t size = 0;
			// Compute number of entries for circular buffer
			uint8_t indexes = device->cb.full ? AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : (device->cb.in_offs - device->cb.out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
			// Loop through entries of circular buffer
			for (uint8_t index = 0; index < indexes; index += 1)
			{
				// Update size with circular buffer entry size
				size += device->cb.entry[(device->cb.out_offs + index) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED].size;
			}
			// Update position as offset beyond size
			fpos = size + offset;
			// Case complete
			break;
		default:
			// Default case error condition
			fpos = -EINVAL;
			// Case complete
			break;
	}
	// Check that position is positive not negative
	if (fpos >= 0)
	{
		// Update file position
		filp->f_pos = fpos;
	}
	// Unlock mutex
	mutex_unlock(&device->mtx);
	// Exit with updated position
	return fpos;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	// Grab aesd device
	struct aesd_dev *device = filp->private_data;
	// Initialize position as zero
	long fpos = 0;
	// Match ioctl command
	switch (cmd)
	{
		case AESDCHAR_IOCSEEKTO:
			// Initialize result check as zero
			int rc = 0;
			// AESD seek metadata
			struct aesd_seekto st;
			// Move data from user space to kernel space
			rc = copy_from_user(&st, (const void __user *)arg, sizeof(struct aesd_seekto));
			// Check error conditions
			if (rc != 0)
			{
				// Error condition
				return -EINVAL;
			}
			// Lock mutex
			rc = mutex_lock_interruptible(&device->mtx);
			// Check error conditions
			if (rc != 0)
			{
				// Error condition
				return -EINVAL;
			}
			// Compute number of entries for circular buffer
			uint32_t indexes = device->cb.full ? AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : (device->cb.in_offs - device->cb.out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
			// Check that specified index does not exceed indexes
			if (st.write_cmd >= indexes)
			{
				// Unlock mutex
				mutex_unlock(&device->mtx);
				// Error condition
				return -EINVAL;
			}
			// Loop through indexes
			for (uint32_t index = 0; index < st.write_cmd; index += 1)
			{
				// Bump position by size associated with index
				fpos += device->cb.entry[(device->cb.out_offs + index) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED].size;
			}
			// Check that specified offset does not exceed size
			if (st.write_cmd_offset >= device->cb.entry[(device->cb.out_offs + st.write_cmd) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED].size)
			{
				// Unlock mutex
				mutex_unlock(&device->mtx);
				// Error condition
				return -EINVAL;
			}
			// Bump position by offset
			fpos += st.write_cmd_offset;
			// Update file position
			filp->f_pos = fpos;
			// Unlock mutex
			mutex_unlock(&device->mtx);
			// Case complete
			break;
		default:
			// Default case error condition
			fpos = -EINVAL;
			// Case complete
			break;
	}
	// Exit with updated position
	return fpos;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek = aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    // Initialize mutex
    mutex_init(&aesd_device.mtx);
    // Initialize circular buffer
    aesd_circular_buffer_init(&aesd_device.cb);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    // Initialize index as zero
    int index = 0;
    // Initialize entry as NULL
    struct aesd_buffer_entry *entry = NULL;
    // Loop over entries of circular buffer
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.cb, index)
    {
	    // Check whether entry is valid (non-NULL)
	    if (entry->buffptr != NULL)
	    {
		    // Free memory associated with entry
		    kfree(entry->buffptr);
	    }
    }
    // Destroy mutex
    mutex_destroy(&aesd_device.mtx);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
