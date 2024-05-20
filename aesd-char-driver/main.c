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
#include <linux/slab.h> // For kmalloc()
#include <linux/uaccess.h> // For copy_from_user()

#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("rohanventer2010"); 
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

	struct aesd_dev *dev; /* device information */

	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = dev; /* for other methods */

    return 0;
}


int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}


ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    PDEBUG("filp->f_pos %lld", filp->f_pos);

    /* make sure arguments are valid 
     * return EFAULT (Bad address)
     */
    if (!filp || !buf || !f_pos)
        return -EFAULT;

    struct aesd_dev *dev;
    dev = (struct aesd_dev*) filp->private_data;        

    if(mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS; /* return ERESTARTSYS (Interrupted system call should be restarted) */

    struct aesd_buffer_entry *entry = NULL;
    struct aesd_circular_buffer *buffer = &dev->buffer;
    size_t offset = 0;
    do {
        entry = aesd_circular_buffer_find_entry_offset_for_fpos(buffer, *f_pos, &offset);
        if (entry == NULL || retval >= count) 
            break;

        size_t bytes_to_copy = entry->size - offset;
        if (bytes_to_copy + retval > count)
            bytes_to_copy = count - retval;

        size_t copied_bytes = copy_to_user(buf + retval, entry->buffptr + offset, bytes_to_copy);
        if (copied_bytes != 0) 
        {
            mutex_unlock(&dev->lock);
            return -EFAULT;
        } else 
        {
            retval += bytes_to_copy;
            *f_pos += bytes_to_copy;
        }
    } while (retval < count);

    mutex_unlock(&dev->lock);
    return retval;
}


ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = count;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    /* make sure arguments are valid 
     * return EFAULT (Bad address)
     * we ignore f_pos
     */
    if (!filp || !buf)
        return -EFAULT;

    /* return 0 when attempting to write zero */
    if (count == 0)
        return 0;

    struct aesd_dev *dev;
    dev = (struct aesd_dev*) filp->private_data;        

    if(mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS; /* return ERESTARTSYS (Interrupted system call should be restarted) */

    // first look at the buf from user
    char *temp_buffer;
    temp_buffer = kmalloc(count * sizeof(char), GFP_KERNEL);
    if (!temp_buffer)
    {
        mutex_unlock(&dev->lock);
        return -ENOMEM;
    }   

    /* unsigned long copy_from_user (void * to, const void __user * from, unsigned long n);
     * Returns number of bytes that could not be copied. On success, this will be zero. 
     * If some data could not be copied, this function will pad the copied data to the requested size using zero bytes. */
    size_t copied_bytes = -1;
    copied_bytes = copy_from_user((void *)temp_buffer,(const void *)buf, count);
    if (copied_bytes != 0)
    {
        /* unable to copy from user space */
        kfree(temp_buffer); /* cleaup */
        mutex_unlock(&dev->lock);
        return -EFAULT;
    }
    
    copied_bytes = count;
    /* find last non-zero byte, this will be the true length */
    size_t ii = 0;
    for (ii = copied_bytes-1; ii >= 0; ii--)
    {
        if (temp_buffer[ii] != '\0')
        {
            copied_bytes = (ii + 1);
            break;
        }
    }
    
    /* is the last char a new line */
    bool last_line_complete = false;
    if (temp_buffer[copied_bytes-1] == '\n')
        last_line_complete = true;
    
    /* lets count the number of lines */
    size_t line_count = 0;
    for (ii = 0; ii < copied_bytes; ii++)
    {
        if (temp_buffer[ii] == '\n')
            line_count++;
    }

    /* adjust the line count if the last line does not end with a newline */
    if (!last_line_complete)
        line_count++;

    PDEBUG("Number of lines: %d", line_count);
    PDEBUG("Last line complete: %s", last_line_complete ? "true" : "false");

    /* extract each line from the temp_buffer and create a new entry */
    const char *ptr = temp_buffer;
    const char *start_ptr = ptr;
    for (ii = 0; ii < line_count; ii++)
    {
        // get line start ptr and length
        const char *start_ptr = ptr;
        size_t line_length = 0; 
        if (ii != (line_count-1)) /* last line? */
        {
            while (*ptr != '\n')
            {
                ptr++;
                line_length++;
            }
            ptr++; /* skip the '\n' */
            line_length++; /* we have to include the '\n' */
        }
        else
        {
            if (last_line_complete)
            {
                while (*ptr != '\n')
                {
                    ptr++;
                    line_length++;
                }
                ptr++; /* skip the '\n' */ 
                line_length++; /* we have to include the '\n' */                  
            }
            else
                line_length = (&temp_buffer[copied_bytes-1] - start_ptr + 1); /* calculate line length */
        }

        PDEBUG("Line %d: line length: %d", ii, line_length);

        struct aesd_buffer_entry *entry = &dev->entry;
        if(!entry->buffptr)
        {
            /* this is a new entry */
            entry->buffptr = kmalloc(line_length, GFP_KERNEL);
            if (!entry->buffptr)
            {
                kfree(temp_buffer);
                mutex_unlock(&dev->lock);
                return -ENOMEM;
            }

            memcpy((void *)entry->buffptr, (void *)start_ptr, line_length);
            entry->size = line_length;

            /* add entry to circular buffer */
            struct aesd_circular_buffer *buffer = &dev->buffer;
            if (ii == (line_count-1)) /* last line? */
            {
                if (last_line_complete)
                {
                    struct aesd_buffer_entry *old_entry = aesd_circular_buffer_add_entry(buffer, entry);
                    if (!old_entry)
                        kfree(old_entry->buffptr);
                    entry->buffptr = NULL;
                    entry->size = 0;
                }
            }
        }
        else
        {
            /* complete a previous entry */
            char *tmp = kmalloc(line_length + entry->size, GFP_KERNEL);
            if (!tmp)
            {
                kfree(temp_buffer);
                mutex_unlock(&dev->lock);
                return -ENOMEM;
            }

            memcpy((void *)tmp, (void *)entry->buffptr, entry->size);
            memcpy((void *)tmp + entry->size, (void *)start_ptr, line_length);

            kfree(entry->buffptr); /* free the old entry buffer */
            entry->buffptr = tmp;
            entry->size += line_length;

            /* add entry to circular buffer */
            struct aesd_circular_buffer *buffer = &dev->buffer;
            if (ii == (line_count-1)) /* last line? */
            {
                if (last_line_complete)
                {
                    struct aesd_buffer_entry *old_entry = aesd_circular_buffer_add_entry(buffer, entry);
                    if (!old_entry)
                        kfree(old_entry->buffptr);
                    entry->buffptr = NULL;
                    entry->size = 0;
                }
            }   
        }
    }

    uint8_t index;
    struct aesd_circular_buffer *buffer_ = &dev->buffer;
    struct aesd_buffer_entry *entry_;
    PDEBUG("======");
    AESD_CIRCULAR_BUFFER_FOREACH(entry_, buffer_, index) 
    {
        char new_string[entry_->size+1];
        memcpy(new_string, entry_->buffptr, entry_->size);
        new_string[entry_->size] = '\0'; //properly null terminate
        PDEBUG("%d : %s : %d", index, new_string, entry_->size);
    }
    PDEBUG("======");

    kfree(temp_buffer);
    mutex_unlock(&dev->lock);
    return retval; /* return number of bytes written */
}


loff_t aesd_llseek(struct file *filp, loff_t offset, int whence) 
{
    struct aesd_dev *dev;

    PDEBUG("whence: %d offset: %lld", whence, offset);

    /* make sure arguments are valid 
     * return EFAULT (Bad address)
     */
    if (!filp)
        return -EFAULT;    

    dev = (struct aesd_dev*)filp->private_data;

    /* since we support wrapping around in reading from the FIFO we can define the maxsize as 
     * the maximum file size the FS supports
     * If we want to be more restrictive, set maxsize to the size of the FIFO (circular buffer) */
    loff_t maxsize = MAX_LFS_FILESIZE;

    if(mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS; /* return ERESTARTSYS (Interrupted system call should be restarted) */

    /* get current size of the FIFO */
    loff_t eof = 0;
    uint8_t index;
    struct aesd_circular_buffer *buffer_ = &dev->buffer;
    struct aesd_buffer_entry *entry_;
    AESD_CIRCULAR_BUFFER_FOREACH(entry_, buffer_, index) 
    {
        eof += entry_->size;
    }
    /* only lock the relevant data */
    mutex_unlock(&dev->lock);

    switch(whence) 
    {
        case SEEK_SET: /* fall through */
        case SEEK_CUR: /* fall through */
        case SEEK_END:
            /* loff_t generic_file_llseek_size(struct file *file, loff_t offset, int whence, loff_t maxsize, loff_t eof) */
            return generic_file_llseek_size(filp, offset, whence, maxsize, eof);
        default:
            return -EINVAL;
            break;
    }
}


static long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) 
{
    struct aesd_dev *dev;

    PDEBUG("ioctl cmd %ud, arg: %lu", cmd, arg);

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC)
        return -ENOTTY;  /* Inappropriate ioctl for device */

    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)
        return -ENOTTY;  /* Inappropriate ioctl for device */

    
    if (cmd != AESDCHAR_IOCSEEKTO)
        return -EINVAL;  /* Inappropriate ioctl for device */

    /* continue as cmd == AESDCHAR_IOCSEEKTO */

    dev = (struct aesd_dev*)filp->private_data;


    struct aesd_seekto seekto;
    size_t copied_bytes = copy_from_user (&seekto, (struct aesd_seekto *)arg, sizeof(struct aesd_seekto));
    /* number of bytes that could not be copied */
    if (copied_bytes != 0)
        return -EINVAL;

    if(mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS; /* return ERESTARTSYS (Interrupted system call should be restarted) */

    /* get current size of the FIFO, and calculate new offset */
    loff_t new_fpos = 0;
    loff_t eof = 0;
    uint8_t index;
    struct aesd_circular_buffer *buffer_ = &dev->buffer;
    struct aesd_buffer_entry *entry_;
    AESD_CIRCULAR_BUFFER_FOREACH(entry_, buffer_, index) 
    {
        eof += entry_->size;
        if (index < seekto.write_cmd)
            new_fpos += entry_->size;
    }
    
    mutex_unlock(&dev->lock);
    
    // Calculate the new file position
//    uint8_t ii = 0;
//    for (ii = 0; i < seekto.write_cmd; ii++)
//        new_fpos += buffer_.entry[ii % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED].size; // Should we wrap around?

    //TODO: should we check if this offset is less than the size of the command?

    /* Validate the new file position against total length */
    if (new_fpos >= eof)
        return -EINVAL;

    new_fpos += seekto.write_cmd_offset;
    filp->f_pos = new_fpos;

    /* unlock the mutex*/
    

    return 0;
}


struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
};


static int aesd_setup_cdev(struct aesd_dev *dev)
{
    PDEBUG("aesd_setup_cdev");
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err)
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    return err;
}


int aesd_init_module(void)
{
    PDEBUG("aesd_init_module");
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) 
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.buffer);
    mutex_init(&aesd_device.lock); 
    result = aesd_setup_cdev(&aesd_device);

    if (result)
        unregister_chrdev_region(dev, 1);
    return result;
}


void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * cleanup AESD specific poritions here as necessary
     */
    size_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) 
    {
        kfree(entry->buffptr);
    }
    mutex_destroy(&aesd_device.lock);
    unregister_chrdev_region(devno, 1);
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);