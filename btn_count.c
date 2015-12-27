#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <asm-generic/uaccess.h>
#include <linux/printk.h>
#include <linux/miscdevice.h>

#define DEV_NAME "btn_count"

typedef struct btn_data {
    int gpio_pin;
    int irq;
    struct mutex rdwr_mutx;
    bool pressed;
    //bool release;
    uint64_t cnt;
    int status;
} btn_data_t;

enum cnt_status {
    STOP = -1,
    START = 0, // start or reset counting
};

static btn_data_t btn;
static int gpio_pin = 0;

static ssize_t btn_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t btn_write(struct file *, const char __user *, size_t, loff_t *);
static int btn_open(struct inode *, struct file *);
static int btn_release(struct inode *, struct file *);

static struct file_operations btn_fops = {
    .read = btn_read,
    .write = btn_write,
    .open = btn_open,
    .release = btn_release
};

static struct miscdevice btn_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEV_NAME,
    .fops = &btn_fops
};

static int btn_open(struct inode *node, struct file * filp) {
    int ret = try_module_get(THIS_MODULE);
    pr_info("%s dev opened\n", DEV_NAME);
    pr_info("%s read: %p\n", DEV_NAME, btn_fops.read);
    pr_info("%s write: %p\n", DEV_NAME, btn_fops.write);
    return ret;
}

static int btn_release(struct inode *node, struct file *filp) {
    module_put(THIS_MODULE);
    return 0;
}

static ssize_t btn_write(struct file *filp, const char __user *buf, \
    size_t len, loff_t *offp) {
    pr_info("%s btn_write: start writing\n", DEV_NAME);
    int ret, rslt;
    ret = mutex_trylock(&btn.rdwr_mutx);
    if (ret)
        return ret;

    //write status
    /*char tmp[len] = {0};
    if (copy_from_user(tmp, buffer, len))
        return -EFAULT;
    tmp[len-1] = (tmp[len-1] != '\0')? '\0': ;

    int digit;
    ret = kstrtoint(tmp, 10, &rslt);
    if (ret)
        return ret;*/
    ret = kstrtoint_from_user(buf, len, 10, &rslt);
    if (ret)
        return -EFAULT;

    if (STOP == rslt) {
        btn.status = STOP;
        pr_info("%s stop counting\n", DEV_NAME);
    } else if (START == rslt) {
        btn.status = START;
        btn.cnt = 0;
        pr_info("%s start/reset counting\n", DEV_NAME);
    } else {
        return EINVAL;
    }

    mutex_unlock(&btn.rdwr_mutx);
    return ret;
}

static ssize_t btn_read(struct file *filp, char __user *buf, \
    size_t len, loff_t *offp) {
    pr_info("%s btn_read; start reading\n", DEV_NAME);

    return 0;
}

static irqreturn_t btn_irq_handler(int irq, void *data) {
    if (btn.status != START)
        goto finish;

    int val;
    // val = 1 for released button; val = 0 for pressing button
    val = gpio_get_value(btn.gpio_pin);
    if (val && btn.pressed) {
        btn.cnt++;
        pr_info("%s count plus one; cnt=%lu\n", DEV_NAME, btn.cnt);
        btn.pressed = false;
    } else if (!val && !btn.pressed) {
        pr_info("%s button is pressing\n", DEV_NAME);
        btn.pressed = true;
    } else {
        pr_warning("%s we should not be here...\n", DEV_NAME);
    }

finish:
    free_irq(btn.irq, NULL);
    return IRQ_HANDLED;
}

static int __init btn_count_init(void) {
    if (!gpio_is_valid(gpio_pin)) {
        pr_err("%s gpio_pin: %d is invalid\n", DEV_NAME, gpio_pin);
        return -EINVAL;
    }

    //no need to request in bbb runtime
    /*ret = gpio_request_one(gpio_pin, GPIOF_IN, DEV_NAME);
     if (ret) {
        printk("%s gpio: %d request error\n", DEV_NAME, gpio_pin);
        return ret;
    }*/

    int irq;
    irq = gpio_to_irq(gpio_pin);
    if (irq < 0) {
        pr_err("%s GPIO %d has no interrupt\n", DEV_NAME, gpio_pin);
        //gpio_free(gpio_pin);
        return -EINVAL;
    }

    int ret;
    ret = request_irq(irq, btn_irq_handler, IRQF_TRIGGER_RISING | \
        IRQF_TRIGGER_FALLING, DEV_NAME, NULL);
    if (ret) {
        pr_err("%s request_irq fails\n", DEV_NAME);
        return ret;
    }

    btn.gpio_pin = gpio_pin;
    btn.irq = irq;
    btn.cnt = 0;
    btn.status = STOP;
    btn.pressed = false;
    //btn.release = false;
    mutex_init(&btn.rdwr_mutx);

    ret = misc_register(&btn_dev);
    if (ret)
        pr_err("%s misc_register fails\n", DEV_NAME);
    else
        pr_info("%s misc_register success\n", DEV_NAME);

    return ret;
}

static void __exit btn_count_exit(void) {
    free_irq(btn.irq, NULL);
    misc_deregister(&btn_dev);
}

module_init(btn_count_init);
module_exit(btn_count_exit);

module_param(gpio_pin, int, S_IRUGO);
MODULE_PARM_DESC(gpio_pin, "GPIO pin to use");

MODULE_DESCRIPTION("Button Counter");
MODULE_AUTHOR("Alvin(Maoyu Chien)");
MODULE_LICENSE("GPL");
