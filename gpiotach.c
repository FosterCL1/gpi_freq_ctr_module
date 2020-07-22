/**
 * @file freq_test.c
 * @author Colin Foster
 * @date 17 July 2020
 * @brief A kernel module for counting events from a Beaglebone GPI device.
 * It is intended to allow detection of an IR sensor for the Schwinn bike
 * which should generate interrupts around 100Hz.  Started with the examples
 * from Derek Molloy.
 */

#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/circ_buf.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Colin Foster");
MODULE_DESCRIPTION("A frequency detector for the BeagleBone");
MODULE_VERSION("0.1");

#define MODULE_NAME "GPIO_TACH"
#define MODULE_MAX_MINORS 1

#define MODULE_MAX_EVENT_COUNTS 200

static unsigned int gpioInput = 15;
static unsigned int irqNumber;
static unsigned int numberPresses = 0;
static int Major;
static dev_t deviceNumber;
static struct class *cl;

static DEFINE_SPINLOCK(cache_lock);

struct tachometer_data {
    struct cdev cdev;
    struct semaphore sem;
};

struct tachometer_data devs[MODULE_MAX_MINORS];

typedef struct
{
    ktime_t buffer[MODULE_MAX_EVENT_COUNTS];
    unsigned long head;
    unsigned long tail;
    int bufferSize;
    //DEFINE_SPINLOCK(cache_lock);
    //struct semaphore producer_lock;
    //struct semaphore consumer_lock;
}timeBuffer_t;

static timeBuffer_t timeBuffer;

#define TIME_SIZE sizeof(ktime_t)

// We shouldn't really exceed a valid input of 100 Hz, so 10ms should be good
#define FREQ_TEST_INPUT_DEBOUNCE_MS 10

static irq_handler_t gpiotach_irq_handler(
        unsigned int irq, 
        void *dev_id, 
        struct pt_regs *regs);

static void __exit gpiotach_exit(void){
    int i;
    printk(KERN_INFO MODULE_NAME ": The button state is currently: %d\n", 
            gpio_get_value(gpioInput));
    printk(KERN_INFO MODULE_NAME ": The button was pressed %d times\n",
            numberPresses);
    free_irq(irqNumber, NULL);
    gpio_unexport(gpioInput);
    gpio_free(gpioInput);

    for (i = 0; i < MODULE_MAX_MINORS; i++) {
        cdev_del(&devs[i].cdev);
    }
    device_destroy(cl, deviceNumber);
    class_destroy(cl);
    unregister_chrdev_region(deviceNumber, MODULE_MAX_MINORS);
    printk(KERN_INFO MODULE_NAME ": Goodbye from the LKM!\n");
}
module_exit(gpiotach_exit);

/**
 * Assume the mutex is taken
 */
static void clearTailBuffer(void *dev_id, ktime_t *pktime)
{
    unsigned long head, tail;
    ktime_t *item;

    head = timeBuffer.head;
    tail = timeBuffer.tail;

    while (CIRC_CNT(head, tail, timeBuffer.bufferSize) >= 1) {
        item = &(timeBuffer.buffer[tail]);
        if (ktime_compare(*pktime, *item) > 0) {
            //smp_store_release(timeBuffer.tail, (tail + 1) & (timeBuffer.bufferSize - 1));
            timeBuffer.tail ++;
            timeBuffer.tail %= (timeBuffer.bufferSize - 1);
        } else {
            break;
        }
    }
}

static irq_handler_t gpiotach_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){
    ktime_t ktime;
    unsigned long head, tail, flags;
    ktime_t *item;
    numberPresses++;
    ktime = ktime_get();

    //spin_lock(timeBuffer.producer_lock);
    spin_lock_irqsave(&cache_lock, flags); 

    clearTailBuffer(dev_id, &ktime);

    ktime_add_ns(ktime, NSEC_PER_SEC);

    head = timeBuffer.head;
    tail = timeBuffer.tail;

    if (CIRC_SPACE(head, tail, timeBuffer.bufferSize) >= 1) {
        item = &timeBuffer.buffer[head];
        *item = ktime;
        timeBuffer.head++;
        timeBuffer.head %= (timeBuffer.bufferSize - 1);
        //smp_store_release(timeBuffer.head, (head + 1) & (buffer->size - 1));
    }

    spin_unlock_irqrestore(&cache_lock, flags);

    return (irq_handler_t) IRQ_HANDLED;
}

static int gpiotach_open(struct inode *inode, struct file *file)
{
    struct tachometer_data *my_data;
    printk(KERN_INFO MODULE_NAME "Inside open\n");
    my_data = (struct tachometer_data *) file->private_data;

    if (down_interruptible(&my_data->sem)) {
        printk(KERN_INFO MODULE_NAME ": Could not hold semaphore\n");
        return -1;
    }

    return 0;
}

static int gpiotach_release(struct inode *inode, struct file *file) 
{
    struct tachometer_data *my_data;
    printk(KERN_INFO MODULE_NAME ": Inside close\n");
    my_data = (struct tachometer_data *) file->private_data;
    up(&my_data->sem);
    return 0;
}

static int gpiotach_read(struct file *file, char __user *user_buffer, size_t size, loff_t *offset)
{
    struct tachometer_data *my_data;
    ssize_t len;
    unsigned long flags;
    int bufferCount;
    unsigned int head, tail;
    ktime_t ktime;
    my_data = (struct tachometer_data *) file->private_data;

    len = min(sizeof(numberPresses), size);

    ktime = ktime_get();

    spin_lock_irqsave(&cache_lock, flags); 
    clearTailBuffer(file, &ktime);
    head = timeBuffer.head;
    tail = timeBuffer.tail;
    bufferCount = CIRC_CNT(head, tail, timeBuffer.bufferSize);
    spin_unlock_irqrestore(&cache_lock, flags);

    if (copy_to_user(user_buffer, &bufferCount, len))
        return -EFAULT;

    *offset += len;
    return len;
}

static int gpiotach_write(struct file *file, const char __user *user_buffer, size_t size, loff_t * offset)
{
    struct tachometer_data *my_data;
    my_data = (struct tachometer_data *) file->private_data;

    if (size > 0) {
        numberPresses = 0;
    }

    *offset += size;
    return size;
}

const struct file_operations gpiotach_fops = {
    .owner = THIS_MODULE,
    .open = gpiotach_open,
    .read = gpiotach_read,
    .write = gpiotach_write,
    .release = gpiotach_release,
    //.unlocked_ioctl = gpiotach_ioctl,
};

/**
 * @brief The LKM initialization function
 * @return 0 upon success
 */
static int __init gpiotach_init(void){
    int result = 0;
    int i;
    struct device *dev_ret;

    printk(KERN_INFO MODULE_NAME ": Initializing the LKM\n");

    // Unsure whether this test will succeed
    if (!gpio_is_valid(gpioInput)){
        printk(KERN_INFO MODULE_NAME ": Invalid input GPIO\n");
        return -ENODEV;
    }
    gpio_request(gpioInput, "sysfs");
    gpio_direction_input(gpioInput);
    gpio_set_debounce(gpioInput, FREQ_TEST_INPUT_DEBOUNCE_MS);
    gpio_export(gpioInput, false);

    printk(KERN_INFO MODULE_NAME ": The input state is currently: %d\n", 
            gpio_get_value(gpioInput));

    irqNumber = gpio_to_irq(gpioInput);

    printk(KERN_INFO MODULE_NAME ": The input is mapped to IRQ: %d\n", 
            irqNumber);
    
    result = request_irq(irqNumber, 
            (irq_handler_t) gpiotach_irq_handler,
            IRQF_TRIGGER_RISING,
            "gpio_tach_handler",
            NULL);

    printk(KERN_INFO MODULE_NAME ": The interrupt request result is: %d\n", 
            result);

    result = alloc_chrdev_region(&deviceNumber, 0, MODULE_MAX_MINORS, "gpiotach");
    if (result < 0) {
        printk(KERN_INFO MODULE_NAME ": Error registering chardev region %d\n", result);
        return result;
    }

    Major = MAJOR(deviceNumber);
    printk(KERN_INFO MODULE_NAME ": The major number is %d\n", Major);

    if (IS_ERR(cl = class_create(THIS_MODULE, "gpiotach")))
    {
        unregister_chrdev_region(deviceNumber, MODULE_MAX_MINORS);
        return PTR_ERR(cl);
    }
    if (IS_ERR(dev_ret = device_create(cl, NULL, deviceNumber, NULL, "gpiotach1.0")))
    {
        class_destroy(cl);
        unregister_chrdev_region(deviceNumber, MODULE_MAX_MINORS);
        return PTR_ERR(dev_ret);
    }

    for (i = 0; i < MODULE_MAX_MINORS; i++) {
        cdev_init(&devs[i].cdev, &gpiotach_fops);
        // Check return value here
        cdev_add(&devs[i].cdev, MKDEV(Major, i), 1);
    }

    return result;
}
module_init(gpiotach_init);

