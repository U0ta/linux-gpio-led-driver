#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>  
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Universal LED driver");
MODULE_AUTHOR("Your Name");

struct led_data {
    struct gpio_desc *gpio_desc;  
    struct cdev cdev;
    dev_t devnum;
    struct class *class;
    struct device *device;
};

#define DEVICE_FIRST 0
#define DEVICE_COUNT 1
#define DGROUP_NAME "gpio_leds_custom"

static int led_open(struct inode *n, struct file *f) {
    printk(KERN_INFO "LED device opened\n");
    return 0;
}

static int led_release(struct inode *n, struct file *f) {
    printk(KERN_INFO "LED device closed\n");
    return 0;
}

static ssize_t led_read(struct file *fp, char __user *buffer, size_t count, loff_t *ppos) {
    struct led_data *led = container_of(fp->f_inode->i_cdev, 
                                        struct led_data, cdev);
    char val;
    
    if (*ppos > 0)
        return 0;
    
    val = gpiod_get_value(led->gpio_desc) ? '1' : '0';
    
    if (copy_to_user(buffer, &val, 1))
        return -EFAULT;
    
    *ppos = 1;
    return 1;
}

static ssize_t led_write(struct file *fp, const char __user *buffer, size_t count, loff_t *ppos) {
    struct led_data *led = container_of(fp->f_inode->i_cdev, 
                                        struct led_data, cdev);
    char val;
    
    if (count < 1)
        return -EINVAL;
    
    if (get_user(val, buffer))
        return -EFAULT;
    
    if (val == '1') {
        gpiod_set_value(led->gpio_desc, 1); 
        printk(KERN_INFO "LED ON\n");
    } else if (val == '0') {
        gpiod_set_value(led->gpio_desc, 0); 
        printk(KERN_INFO "LED OFF\n");
    } else {
        return -EINVAL;
    }
    
    return count;
}

static const struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .release = led_release,
    .write = led_write,
};

static int led_probe(struct platform_device *pdev) {
    struct led_data *led;
    struct device *dev = &pdev->dev;
    int result;

    printk(KERN_INFO "LED: Probing device\n");

    led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
    if (!led)
        return -ENOMEM;
    
    // Получаем GPIO descriptor из Device Tree
    // devm_gpiod_get() автоматически:
    // 1. Применяет pinctrl настройки
    // 2. Резервирует GPIO
    // 3. Настраивает направление
    led->gpio_desc = devm_gpiod_get(dev, "gpios", GPIOD_OUT_LOW);
    if (IS_ERR(led->gpio_desc)) {
        result = PTR_ERR(led->gpio_desc);
        dev_err(dev, "Failed to get GPIO descriptor: %d\n", result);
        return result;
    }

    result = alloc_chrdev_region(&led->devnum, DEVICE_FIRST, DEVICE_COUNT, DGROUP_NAME);
    if (result < 0) {
        dev_err(dev, "Cannot register char device region\n");
        return result;
    }

    cdev_init(&led->cdev, &led_fops);
    led->cdev.owner = THIS_MODULE;

    result = cdev_add(&led->cdev, led->devnum, DEVICE_COUNT);
    if (result < 0) {
        unregister_chrdev_region(led->devnum, DEVICE_COUNT);
        dev_err(dev, "Cannot add char device\n");
        return result;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    led->class = class_create("myled_class");
#else
    led->class = class_create(THIS_MODULE, "myled_class");
#endif
    
    if (IS_ERR(led->class)) {
        result = PTR_ERR(led->class);
        cdev_del(&led->cdev);
        unregister_chrdev_region(led->devnum, DEVICE_COUNT);
        dev_err(dev, "Cannot create class\n");
        return result;
    }

    led->device = device_create(led->class, NULL, led->devnum, NULL, "myled");
    if (IS_ERR(led->device)) {
        result = PTR_ERR(led->device);
        class_destroy(led->class);
        cdev_del(&led->cdev);
        unregister_chrdev_region(led->devnum, DEVICE_COUNT);
        dev_err(dev, "Cannot create device\n");
        return result;
    }

    platform_set_drvdata(pdev, led);

    dev_info(dev, "LED driver ready: /dev/myled\n");
    
    return 0;
}

static int led_remove(struct platform_device *pdev) {
    struct led_data *led = platform_get_drvdata(pdev);

    printk(KERN_INFO "LED: Removing device\n");

    // Выключаем LED
    gpiod_set_value(led->gpio_desc, 0); 

    // GPIO автоматически освободится через devm_*

    device_destroy(led->class, led->devnum);
    class_destroy(led->class);
    cdev_del(&led->cdev);
    unregister_chrdev_region(led->devnum, DEVICE_COUNT);

    printk(KERN_INFO "LED driver removed\n");
    
    return 0;
}

static const struct of_device_id led_of_match[] = {
    { .compatible = "custom,gpio-led"}, 
    { }
};
MODULE_DEVICE_TABLE(of, led_of_match);

static struct platform_driver led_driver = {
    .probe = led_probe,
    .remove = led_remove,
    .driver = {
        .name = "gpio-led",
        .of_match_table = led_of_match,
    },
};

module_platform_driver(led_driver);