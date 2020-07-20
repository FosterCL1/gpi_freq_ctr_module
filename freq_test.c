/**
 * @file freq_test.c
 * @author Colin Foster
 * @date 17 July 2020
 * @brief A kernel module for counting events from a Beaglebone GPI device.
 * It is intended to allow detection of an IR sensor for the Schwinn bike
 * which should generate interrupts around 100Hz.  Started with the examples
 * from Derek Molloy.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Colin Foster");
MODULE_DESCRIPTION("A frequency detector for tbe BeagleBone");
MODULE_VERSION("0.1");

static unsigned int gpioInput = 15;
static unsigned int irqNumber;
static unsigned int numberPresses = 0;

// We shouldn't really exceed a valid input of 100 Hz, so 10ms should be good
#define FREQ_TEST_INPUT_DEBOUNCE_MS 10

static irq_handler_t ebbgpio_irq_handler(
        unsigned int irq, 
        void *dev_id, 
        struct pt_regs *regs);

/**
 * @brief The LKM initialization function
 * @return 0 upon success
 */
static int __init ebbgpio_init(void){
    int result = 0;
    printk(KERN_INFO "FREQ_TEST: Initializing the LKM\n");

    // Unsure whether this test will succeed
    if (!gpio_is_valid(gpioInput)){
        printk(KERN_INFO "FREQ_TEST: Invalid input GPIO\n");
        return -ENODEV;
    }
    gpio_request(gpioInput, "sysfs");
    gpio_direction_input(gpioInput);
    gpio_set_debounce(gpioInput, FREQ_TEST_INPUT_DEBOUNCE_MS);
    gpio_export(gpioInput, false);

    printk(KERN_INFO "FREQ_TEST: The input state is currently: %d\n", 
            gpio_get_value(gpioInput));

    irqNumber = gpio_to_irq(gpioInput);

    printk(KERN_INFO "FREQ_TEST: The input is mapped to IRQ: %d\n", 
            irqNumber);
    
    result = request_irq(irqNumber, 
            (irq_handler_t) ebbgpio_irq_handler,
            IRQF_TRIGGER_RISING,
            "ebb_gpio_handler",
            NULL);

    printk(KERN_INFO "FREQ_TEST: The interrupt request result is: %d\n", 
            result);
    return result;
}

static void __exit ebbgpio_exit(void){
    printk(KERN_INFO "FREQ_TEST: The button state is currently: %d\n", 
            gpio_get_value(gpioInput));
    printk(KERN_INFO "FREQ_TEST: The button was pressed %d times\n",
            numberPresses);
    free_irq(irqNumber, NULL);
    gpio_unexport(gpioInput);
    gpio_free(gpioInput);
    printk(KERN_INFO "FREQ_TEST: Goodbye from the LKM!\n");
}

static irq_handler_t ebbgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){
    printk(KERN_INFO "FREQ_TEST: Interrupt!\n");
    numberPresses++;
    return (irq_handler_t) IRQ_HANDLED;
}

module_init(ebbgpio_init);
module_exit(ebbgpio_exit);
