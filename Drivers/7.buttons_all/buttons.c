/* 按键驱动：引入 定时器，互斥锁  */

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

static struct class *button_cls;
static struct class_device *button_cls_dev;

static DECLARE_WAIT_QUEUE_HEAD(button_waitq);
static struct pin_desc *irq_pd;						//加入了定时器后，IRQ相当于一个“中转”，为其分配一个结构体

struct pin_desc{
	unsigned int pin;
	unsigned int key_val;
};

struct pin_desc pins_desc[3] = {
	{S3C2410_GPF0, 0x01},
	{S3C2410_GPF2, 0x02},
	{S3C2410_GPG3, 0x03},
};

static struct timer_list button_timer;
static volatile int ev_timer = 0;				//在 timer_function 置1， 在 read 清0.

static DECLARE_MUTEX(button_lock);

unsigned char key_val;

/* **************************************** */
static irqreturn_t buttons_irq(int irq, void *dev_id);
static void buttons_timer_function(unsigned long data);

static int buttons_open(struct inode *inode, struct file *file)
{
	/* 获取信号量：非阻塞打开方式下 ，若没有 信号量 ，则立即返回值；阻塞方式则是一直等到 有信号量 */
	if(file -> f_flags & O_NONBLOCK)
		{
			if(down_trylock(&button_lock))
				return -EBUSY;
		}
	else
		{
			down(&button_lock);
		}
	
	/* 注册中断 */
	request_irq(IRQ_EINT0, buttons_irq, IRQT_BOTHEDGE, "S1", &pins_desc[0]);
	request_irq(IRQ_EINT2, buttons_irq, IRQT_BOTHEDGE, "S2", &pins_desc[1]);
	request_irq(IRQ_EINT11, buttons_irq, IRQT_BOTHEDGE, "S3", &pins_desc[2]);
	return 0;
}

ssize_t buttons_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	if(size != 1)
		return -EINVAL;
		
	/* 休眠进程：非阻塞打开方式下 ，若没有 按键事件 发生，则立即返回值；阻塞方式则是一直等待  按键事件 ； 按键事件：中断-定时器延时 */
	if(file -> f_flags & O_NONBLOCK)
		{
			if(! ev_timer)
				{
					return -EAGAIN;
				}
		}
	else
		{
			wait_event_interruptible(button_waitq, ev_timer);
		}
	/*向用户空间返回 key_val */
	copy_to_user(buf, &key_val, 1);
	ev_timer = 0;
	return 1;
}

static int buttons_release(struct inode *inode, struct file *file)
{
	/* 释放中断，信号量 */
	free_irq(IRQ_EINT0, pins_desc[0]);
	free_irq(IRQ_EINT2, pins_desc[1]);
	free_irq(IRQ_EINT11, pins_desc[2]);
	up(&button_lock);
	return 0;
}

static struct file_operations button_fops = {
	.owner		=	THIS_MODULE,
	.open		=	buttons_open,
	.read		=	buttons_read,
	.release	=	buttons_release,
};

static irqreturn_t buttons_irq(int irq, void *dev_id)
{
	irq_pd = (struct pin_desc *)dev_id;
	mod_timer(&button_timer, jiffies + HZ/100);
	return IRQ_RETVAL(IRQ_HANDLED);
}

static void buttons_timer_function(unsigned long data)
{
	/* 获取键值 */
	struct pin_desc *pindesc = irq_pd;
	int pinval;
	pinval = s3c2410_gpio_getpin(pindesc -> pin);
	if(pinval)
		{
			key_val = 0x80 | pindesc -> key_val;
		}
	else
		{
			key_val = pindesc -> key_val;
		}
	/* 唤醒进程 */
	ev_timer = 1;
	wake_up_interruptible(&button_waitq);
}

int major;
static int buttons_init(void)
{
	/* 初始化、添加定时器 */
	init_timer(&button_timer);
	button_timer.function = buttons_timer_function;
	add_timer(&button_timer);
	/* 注册字符类设备，创建设备节点 */
	major = register_chrdev(0, "buttons_all", &button_fops);
	button_cls = class_create(THIS_MODULE, "buttons_all");
	button_cls_dev = class_device_create(button_cls, NULL, MKDEV(major, 0), NULL, "buttons");
	
	return 0;
}

static void buttons_exit(void)
{
	unregister_chrdev(major, "buttons_all");
	class_device_destroy(button_cls, MKDEV(major, 0));
	class_destroy(button_cls);
	del_timer(&button_timer);
}

module_init(buttons_init);
module_exit(buttons_exit);
MODULE_LICENSE("GPL");
