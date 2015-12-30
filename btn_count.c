#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#define DEV_NAME "btn_count"
#define OUT_MSG_LEN 256

typedef struct btn_data {
    int gpio_pin;
    int irq;
    struct mutex rdwr_mutx;
    bool pressed;
    //bool release;
    unsigned long cnt;
    int status;
} btn_data_t;

enum cnt_status {
    STOP = 0,
    START = 1, // start or reset counting
};

static btn_data_t btn;
static int gpio_pin = 0;

static ssize_t btn_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t btn_write(struct file *, const char __user *, size_t, loff_t *);
static int btn_open(struct inode *, struct file *);
static int btn_release(struct inode *, struct file *);
static int btn_setup_irq(void);
static void btn_clean_irq(void);

static const struct file_operations btn_fops = {
    .owner = THIS_MODULE,
    .read = btn_read,
    .write = btn_write,
    .open = btn_open,
    .release = btn_release,
    /*.llseek = no_llseek,*/
};

static struct miscdevice btn_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEV_NAME,
    .fops = &btn_fops
};

static int btn_open(struct inode *node, struct file * filp) {
    try_module_get(THIS_MODULE);
    pr_info("%s dev opened\n", DEV_NAME);
    return 0;
}

static int btn_release(struct inode *node, struct file *filp) {
    module_put(THIS_MODULE);
    return 0;
}

static ssize_t btn_write(struct file *filp, const char __user *buf, \
    size_t len, loff_t *offp) {
    int ret;
    unsigned int rslt;
    ret = mutex_trylock(&btn.rdwr_mutx);
    if (!ret) {
        pr_warning("%s is busy\n", DEV_NAME);
        return -EBUSY;
    }

    pr_info("%s btn_write: start writing; len: %d\n", DEV_NAME, len);
    //write status
    char tmp[len];
    memset(tmp, 0, len);
    copy_from_user(tmp, buf, len);
    /*if (copy_from_user(tmp, buf, len)) {
        pr_err("%s copy_from_user fails\n", DEV_NAME);
        return -EFAULT;
    }*/
    tmp[len-1] = (tmp[len-1] != '\0')? '\0': tmp[len-1];
    pr_info("%s buffer content: %s", DEV_NAME, tmp);

    ret = kstrtoint(tmp, 10, &rslt);
    if (ret)
        goto finish;
    /*ret = kstrtoint_from_user(buf, len, 10, &rslt);
    if (ret) {
        pr_err("%s kstrtoint_from_user fails\n", DEV_NAME);
        return ret;
    }*/

    if (STOP == rslt) {
        btn.status = STOP;
        pr_info("%s stop counting\n", DEV_NAME);
        btn_clean_irq();
    } else if (START == rslt) {
        gpio_direction_input(gpio_pin);
        btn.status = START;
        btn.cnt = 0;
        pr_info("%s start/reset counting\n", DEV_NAME);
        if (btn.irq < 0) {
            if (btn_setup_irq())
                pr_err("%s btn_setup_irq fails\n", DEV_NAME);
        }
    } else {
        pr_err("%s no such command: %d", DEV_NAME, rslt);
    }

finish:
    mutex_unlock(&btn.rdwr_mutx);
    return len;
}

static ssize_t btn_read(struct file *filp, char __user *buf, \
    size_t len, loff_t *offp) {
    pr_info("%s btn_read; btn.cnt: %lu", DEV_NAME, btn.cnt);
    char out[OUT_MSG_LEN];
    memset(out, 0, OUT_MSG_LEN);
    snprintf(out, OUT_MSG_LEN, "%lu", btn.cnt);
    long ret = copy_to_user(buf, out, OUT_MSG_LEN);
    return OUT_MSG_LEN;
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
    return IRQ_HANDLED;
}

static int btn_setup_irq(void) {
    int irq, ret;
    irq = gpio_to_irq(gpio_pin);
    if (irq < 0) {
        pr_err("%s GPIO %d has no interrupt\n", DEV_NAME, gpio_pin);
        //gpio_free(gpio_pin);
        return -EINVAL;
    }
    pr_info("%s gpio irq no: %d\n", DEV_NAME, irq);
    ret = request_irq(irq, btn_irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
        DEV_NAME, (void*)&btn_dev);
    if (ret) {
        pr_err("%s request_irq fails\n", DEV_NAME);
        return ret;
    }
    btn.irq = irq;
    return 0;
}

static void btn_clean_irq(void) {
    if (btn.irq > -1)
        free_irq(btn.irq, (void*)&btn_dev);
    btn.irq = -1;
}

static int __init btn_count_init(void) {
    int ret;
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

    btn.gpio_pin = gpio_pin;
    btn.irq = -1;
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

    return 0;
}

static void __exit btn_count_exit(void) {
    btn_clean_irq();
    misc_deregister(&btn_dev);
}

module_init(btn_count_init);
module_exit(btn_count_exit);

module_param(gpio_pin, int, S_IRUGO);
MODULE_PARM_DESC(gpio_pin, "GPIO pin to use");

MODULE_DESCRIPTION("Button Counter");
MODULE_AUTHOR("Alvin(Maoyu Chien)");
MODULE_LICENSE("GPL");
