#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h> // platform_device, platform_driver
#include <linux/of.h>             // Device Tree функции (of_*)
#include <linux/of_gpio.h>        // of_get_named_gpio - парсинг GPIO из DT
#include <linux/gpio.h>  
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Universal LED driver");

struct led_data {
    int gpio;
    struct cdev cdev;
    dev_t devnum;
    struct class *class; // класс устройства для udev
    struct device *device; // объект устройства
};

// #define DEVICE_NAME "led"
#define DEVICE_FIRST 0
#define DEVICE_COUNT 1
#define DGROUP_NAME "gpio_leds_custom"
// #define BUF_SIZE 1024
// #define EOK 0


static int major = 0;
// static struct cdev my_dev;
static int device_open = 0;
// static char kernel_buffer[BUF_SIZE];
// static int buffer_len = 0;

static int led_release(struct inode *n, struct file *f);
static int led_open(struct inode *n, struct file *f);
static ssize_t led_read(struct file *fp, char __user *buffer, size_t count, loff_t *ppos);
static ssize_t led_write(struct file *fp, const char __user *buffer, size_t count, loff_t *ppos);
static int __init led_probe(struct platform_device *pdev);
static int __exit led_remove(struct platform_device *pdev);

static const struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .release = led_release,
    .write = led_write,
};

static int led_open(struct inode *n, struct file *f) {
    if (device_open) return -EBUSY;
    device_open++;
    return 0;
}

static int led_release(struct inode *n, struct file *f) {
    device_open--;
    return 0;
}

static ssize_t led_read(struct file *fp, char __user *buffer, size_t count, loff_t *ppos) {
    struct led_data *led = container_of(fp->f_inode->i_cdev, 
                                        struct led_data, cdev);

    char val = gpio_get_value(led->gpio) ? '1' : '0';

    if (copy_to_user(buffer, &val, count)) return -EFAULT;

    return count;
}

static ssize_t led_write(struct file *fp, const char __user *buffer, size_t count, loff_t *ppos) {
    // получаем указатель на структуру через объект (file), который является полем этой структуры
    struct led_data *led = container_of(fp->f_inode->i_cdev, 
                                        struct led_data, cdev);
    
                                    
    char val;
    if (get_user(val, buffer)) return -EFAULT;
    gpio_set_value(led->gpio, (val == '1') ? 1 : 0);
    return count;
}

static int __init led_probe(struct platform_device *pdev) {

    printk(KERN_INFO "LED custom driver setting up...");

    struct led_data *led;
    struct device *dev = &pdev->dev;

    int result = 0;

    led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
    if (!led) return -ENOMEM;
    led->gpio = of_get_named_gpio(dev->of_node, "gpios", 0);
    // обработка
    if (!gpio_is_valid(led->gpio)) {
        dev_err(dev, "Invalid GPIO\n");
        return -EINVAL;
    }

    // бронь на пин
    result = devm_gpio_request_one(dev, led->gpio, GPIOF_OUT_INIT_LOW, "led");


    printk(KERN_INFO "Trying to register char device region\n");

    result = alloc_chrdev_region(&led->devnum, DEVICE_FIRST, DEVICE_COUNT, DGROUP_NAME); // выделение региона под устройство, минусовой код = ошибка
    major = MAJOR(led->devnum);
    // printk(KERN_INFO "major number: %d\n", major);

    if (result < 0) {
        unregister_chrdev_region(MKDEV(major, DEVICE_FIRST), DEVICE_COUNT);
        printk(KERN_INFO "Can not refister char device region\n");
        goto err;
    }

    cdev_init(&led->cdev, &led_fops);
    led->cdev.owner = THIS_MODULE;

    result = cdev_add(&led->cdev, led->devnum, DEVICE_COUNT);
    if (result < 0) {
        unregister_chrdev_region(MKDEV(major, DEVICE_FIRST), DEVICE_COUNT);
        printk(KERN_INFO "Can not add char device\n");
        goto err;
    }

    // создание udev класса + обработка ошибок
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
        led->class = class_create("led_class");
    #else
        led->class = class_create(THIS_MODULE, "led_class");
    #endif
    if (IS_ERR(led->class)) {
        cdev_del(&led->cdev);
        unregister_chrdev_region(MKDEV(major, DEVICE_FIRST), DEVICE_COUNT);
        printk(KERN_INFO "Can not create udev class\n");
        return PTR_ERR(led->class);
    }
    // создание устройства + обработка ошибок
    led->device = device_create(led->class, NULL, led->devnum, 
                               NULL, "myled");
    if (IS_ERR(led->device)) {
        class_destroy(led->class);
        cdev_del(&led->cdev);
        unregister_chrdev_region(led->devnum, 1);
        return PTR_ERR(led->device);
    }

err: 
    return result;
}


static int __exit led_remove(struct platform_device *pdev) {
    struct led_data *led = platform_get_drvdata(pdev);

    device_destroy(led->class, led->devnum);
    class_destroy(led->class);
    unregister_chrdev_region(MKDEV(major, DEVICE_FIRST), DEVICE_COUNT);
    cdev_del(&led->cdev);
    printk(KERN_INFO "LED driver disabled");
    return 0;
}   

static const struct of_device_id led_of_match[] = {
    { .compatible = "custom,gpio-led"}, 
    { }
};

MODULE_DEVICE_TABLE(of, led_of_match);

static struct platform_driver led_driver = {
    .probe = led_probe,    // Вызывается когда найдено устройство
    .remove = led_remove,  // Вызывается при удалении устройства
    .driver = {
        .name = "gpio-led",           // Имя драйвера
        .of_match_table = led_of_match, // Таблица соответствия DT
    },
};

module_platform_driver(led_driver);
