#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>

static struct class *first_drv_class;
static struct class_device *firstdrv_class_dev;

volatile unsigned long *gpfcon = NULL;
volatile unsigned long *gpfdat = NULL;

static int first_drv_open(struct inode *inode, struct file *file)
{
	*gpfcon &= ~((0x03<<(4*2)) | (0x03<<(5*2)) | (0x03<<(6*2)));
	*gpfcon |= ((0x01<<(4*2)) | (0x01<<(5*2)) | (0x01<<(6*2)));
	return 0;
}

static ssize_t first_drv_write(struct file *file, const char __user *buf, size_t count, loff_t *opps)
{
	int val;
	copy_from_user(&val, buf, count);
	
	if(val == 1)
		{
			*gpfdat &= ~((0x1<<4) | (0x1<<5) | (0x1<<6));
		}
	else
		{
			*gpfdat |= ((0x1<<4) | (0x1<<5) | (0x1<<6));
		}
		
	return 0;
}

static struct file_operations first_drv_fops = {
	.owner	=		THIS_MODULE,
	.open		=		first_drv_open,
	.write	=		first_drv_write,
};

int major;
static int first_drv_init(void)
{
	major = register_chrdev(0, "first_drv", &first_drv_fops)£»
	
	firstdrv_class = class_create(THIS_MODULE, "firstdrv");
	firstdrv_class_dev = class_device_create(first_class, NULL, MKDEV(major, 0), NULL,"xyz");
	
	gpfcon = (volatile unsigned long *)ioremap(0x56000050, 16)
	gpfdat = gpfcon + 1;
	
	return 0;
}

static void first_drv_exit(void)
{
	unregister_chrdev(major, "first_drv");
	
	class_device_unregister(firstdrv_class_dev);
	class_destory(firstdrv_class);
	
	iounmap(gpfcon);
}

moudle_init(first_drv_init);
module_exit(first_drv_exot);

MODULE_LICENCE("GPL");
