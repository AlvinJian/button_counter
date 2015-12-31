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
#include <linux/string.h>

#define DEV_NAME "btn_count"
#define OUT_MSG_LEN 8

static const char START[] = "start";
static const char STOP[] = "stop";

typedef struct btn_data {
    int gpio_pin;
    int irq;
    struct mutex rdwr_mutx;
    bool pressed;
    //bool release;
    unsigned long cnt;
    int status;
} btn_data_t;

enum status_t {
    status_stop = 0,
    status_start,
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
    printk("%s dev opened\n", DEV_NAME);
    return 0;
}

static int btn_release(struct inode *node, struct file *filp) {
    module_put(THIS_MODULE);
    printk("%s close\n",DEV_NAME);
    return 0;
}

static ssize_t btn_write(struct file *filp, const char __user *buf, \
    size_t len, loff_t *offp) {
    int ret;
    ret = mutex_trylock(&btn.rdwr_mutx);
    if (!ret) {
        pr_warning("%s is busy\n", DEV_NAME);
        return -EBUSY;
    }

    printk("%s btn_write: start writing; len: %d\n", DEV_NAME, len);
    //write status
    char tmp[len];
    memset(tmp, 0, len);
    ret = copy_from_user(tmp, buf, len);
    /*if (copy_from_user(tmp, buf, len)) {
        pr_err("%s copy_from_user fails\n", DEV_NAME);
        return -EFAULT;
    }*/
    tmp[len-1] = (tmp[len-1] != '\0')? '\0': tmp[len-1];
    printk("%s buffer content: %s; cp ret: %d\n", DEV_NAME, tmp, ret);

    int cmp = strcmp(STOP,tmp);
    if (cmp == 0) {
        printk("%s stop counting\n", DEV_NAME);
        btn.status = status_stop;
        goto finish;
    }
    cmp = strcmp(START,tmp);
    if (cmp == 0) {
        printk("%s start/reset counting\n", DEV_NAME);
        btn.status = status_start;
        btn.cnt = 0;
        goto finish;
    }

    pr_err("%s no such command: %s\n", DEV_NAME, tmp);

#if 0
    if (!strcmp(STOP,tmp)) {
        btn.status = status_stop;
        printk("%s stop counting\n", DEV_NAME);
        btn_clean_irq();
    } else if (!strcmp(START,tmp)) {
        gpio_direction_input(gpio_pin);
        btn.status = status_start;
        btn.cnt = 0;
        printk("%s start/reset counting\n", DEV_NAME);
        if (btn.irq < 0) {
            if (btn_setup_irq())
                pr_err("%s btn_setup_irq fails\n", DEV_NAME);
        }
    } else {
        pr_err("%s no such command: %s\n", DEV_NAME, tmp);
    }
#endif

finish:
    mutex_unlock(&btn.rdwr_mutx);
    return len;
}

static ssize_t btn_read(struct file *filp, char __user *buf, \
    size_t len, loff_t *offp) {
    printk("%s btn_read; btn.cnt: %lu\n", DEV_NAME, btn.cnt);
    ssize_t msg_len;
    if (btn.status == status_start) {
        char out[OUT_MSG_LEN];
        memset(out, 0, OUT_MSG_LEN);
        snprintf(out, OUT_MSG_LEN, "%lu\n", btn.cnt);
        msg_len  = 0;
        while (1) {
            if (out[msg_len] == '\0'){
                msg_len++;
                break;
            }
            msg_len++;
        }
        if (*offp >= msg_len)
            return 0;
        copy_to_user(buf, out, msg_len);
        (*offp) += msg_len;
    } else {
        char msg[] = "-1\n";
        msg_len = sizeof(msg);
        if (*offp >= msg_len)
            return 0;
        copy_to_user(buf, msg, msg_len);
        (*offp) += msg_len;
    }
    return msg_len;
}

static irqreturn_t btn_irq_handler(int irq, void *data) {
    if (btn.status != status_start)
        goto finish;

    int val;
    // val = 1 for released button; val = 0 for pressing button
    val = gpio_get_value(btn.gpio_pin);
    if (val && btn.pressed) {
        btn.cnt++;
        printk("%s count plus one; cnt=%lu\n", DEV_NAME, btn.cnt);
        btn.pressed = false;
    } else if (!val && !btn.pressed) {
        printk("%s button is pressing\n", DEV_NAME);
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
    printk("%s gpio irq no: %d\n", DEV_NAME, irq);
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
        printk("%s misc_register success\n", DEV_NAME);

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
