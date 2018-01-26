/* �������������� ��ʱ����������  */

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
static struct pin_desc *irq_pd;						//�����˶�ʱ����IRQ�൱��һ������ת����Ϊ�����һ���ṹ��

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
static volatile int ev_timer = 0;				//�� timer_function ��1�� �� read ��0.

static DECLARE_MUTEX(button_lock);

unsigned char key_val;

/* **************************************** */
static irqreturn_t buttons_irq(int irq, void *dev_id);
static void buttons_timer_function(unsigned long data);

static int buttons_open(struct inode *inode, struct file *file)
{
	/* ��ȡ�ź������������򿪷�ʽ�� ����û�� �ź��� ������������ֵ��������ʽ����һֱ�ȵ� ���ź��� */
	if(file -> f_flags & O_NONBLOCK)
		{
			if(down_trylock(&button_lock))
				return -EBUSY;
		}
	else
		{
			down(&button_lock);
		}
	
	/* ע���ж� */
	request_irq(IRQ_EINT0, buttons_irq, IRQT_BOTHEDGE, "S1", &pins_desc[0]);
	request_irq(IRQ_EINT2, buttons_irq, IRQT_BOTHEDGE, "S2", &pins_desc[1]);
	request_irq(IRQ_EINT11, buttons_irq, IRQT_BOTHEDGE, "S3", &pins_desc[2]);
	return 0;
}

ssize_t buttons_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	if(size != 1)
		return -EINVAL;
		
	/* ���߽��̣��������򿪷�ʽ�� ����û�� �����¼� ����������������ֵ��������ʽ����һֱ�ȴ�  �����¼� �� �����¼����ж�-��ʱ����ʱ */
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
	/*���û��ռ䷵�� key_val */
	copy_to_user(buf, &key_val, 1);
	ev_timer = 0;
	return 1;
}

static int buttons_release(struct inode *inode, struct file *file)
{
	/* �ͷ��жϣ��ź��� */
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
	/* ��ȡ��ֵ */
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
	/* ���ѽ��� */
	ev_timer = 1;
	wake_up_interruptible(&button_waitq);
}

int major;
static int buttons_init(void)
{
	/* ��ʼ������Ӷ�ʱ�� */
	init_timer(&button_timer);
	button_timer.function = buttons_timer_function;
	add_timer(&button_timer);
	/* ע���ַ����豸�������豸�ڵ� */
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
