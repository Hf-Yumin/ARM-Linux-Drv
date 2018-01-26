#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>
#include <linux/poll.h>

static struct class *forthdrv_class;
static struct class_device *forthdrv_class_dev;

static DECLARE_WAIT_QUEUE_HEAD(button_waitq);
static volatile int ev_press = 0;

struct pin_desc{
	unsigned int pin;
	unsigned int key_val;
};

struct pin_desc pins_desc[3] = {
	{S3C2410_GPF0, 0x01},
	{S3C2410_GPF2, 0x02},
	{S3C2410_GPG3, 0x03},
};

static unsigned char key_val;

static irqreturn_t buttons_irq(int irq, void *dev_id)
{
	struct pin_desc *pindesc = (struct pin_desc *)dev_id;
	unsigned int pinval;
	
	pinval = s3c2410_gpio_getpin(pindesc -> pin);
	if(pinval)
		{
			key_val = 0x80 | pindesc -> key_val;
		}
	else
		{
			key_val = pindesc -> key_val;
		}
	
	ev_press = 1;
	wake_up_interruptible(&button_waitq);
	
	return IRQ_RETVAL(IRQ_HANDLED);
}

static int forth_drv_open(struct inode *inode, struct file *file)
{
	request_irq(IRQ_EINT0, buttons_irq, IRQT_BOTHEDGE, "S1", &pins_desc[0]);
	request_irq(IRQ_EINT2, buttons_irq, IRQT_BOTHEDGE, "S2", &pins_desc[1]);
	request_irq(IRQ_EINT11, buttons_irq, IRQT_BOTHEDGE, "S3", &pins_desc[2]);
	return 0;
}

ssize_t forth_drv_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	if(size != 1)
		return -EINVAL;
	
	wait_event_interruptible(button_waitq, ev_press);
	
	copy_to_user(buf, &key_val, 1);
	ev_press = 0;
	return 1;
}

int forth_drv_close(struct inode *inode, struct file *file)
{
	free_irq(IRQ_EINT0, &pins_desc[0]);
	free_irq(IRQ_EINT2, &pins_desc[1]);
	free_irq(IRQ_EINT11, &pins_desc[2]);
	return 0;
}

static unsigned int forth_drv_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;
	poll_wait(file, &button_waitq, wait);
	
	if(ev_press)
		mask |= POLLIN | POLLRDNORM;
		
	return mask;
}

static struct file_operations forth_drv_fops = {
	.owner		=	THIS_MODULE,
	.open		=	forth_drv_open,
	.read		=	forth_drv_read,
	.release	=	forth_drv_close,
	.poll			=	forth_drv_poll,
};

int major;
static int forth_drv_init(void)
{
	major = register_chrdev(0, "forth_drv", &forth_drv_fops);
	
	forthdrv_class = class_create(THIS_MODULE, "forth_drv");
	forthdrv_class_dev = class_device_create(forthdrv_class, NULL, MKDEV(major, 0), NULL, "buttons");
	
	return 0;
}

static int forth_drv_exit(void)
{
	unregister_chrdev(major, "forth_drv");
	class_device_unregister(forthdrv_class_dev);
	class_destroy(forthdrv_class);
	
	return 0;
}

module_init(forth_drv_init);
module_exit(forth_drv_exit);

MODULE_LICENSE("GPL");
