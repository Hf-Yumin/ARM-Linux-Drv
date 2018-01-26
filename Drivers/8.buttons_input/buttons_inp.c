#include <linux/module.h>
#include <linux/version.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/irq.h>

#include <asm/gpio.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>

struct pin_desc{
	int irq;
	char *name;
	unsigned int pin;
	unsigned int key_val;
};

struct pin_desc pins_desc[3] = {
	{IRQ_EINT0, "S1", S3C2410_GPF0, KEY_L},
	{IRQ_EINT2, "S2", S3C2410_GPF2, KEY_S},
	{IRQ_EINT11, "S3", S3C2410_GPG3, KEY_ENTER},
};

static struct input_dev *buttons_dev;
static struct pin_desc *irq_pd;
static struct timer_list buttons_timer;

static irqreturn_t buttons_irq(int irq, void *dev_id)
{
	irq_pd = (struct pin_desc *)dev_id;
	mod_timer(&buttons_timer, jiffies + HZ/100);
	return IRQ_RETVAL(IRQ_HANDLED);
}

static void buttons_timer_function(unsigned long data)
{
	struct pin_desc *pindesc = irq_pd;
	unsigned int pinval;
	
	if(! pindesc)
		return;
	
	pinval = s3c2410_gpio_getpin(pindesc -> pin);
	/* 调用 input_event 对键值作处理 */
	if(pinval)
		{
			input_event(buttons_dev, EV_KEY, pindesc -> key_val, 0);
			input_sync(buttons_dev);
		}
	else
		{
			input_event(buttons_dev, EV_KEY, pindesc -> key_val, 1);
			input_sync(buttons_dev);			
		}
}

static int buttons_init(void)
{
	int i;
	
	/* 分配，设置，注册 input_dev 结构 */
	buttons_dev = input_allocate_device();
	
	set_bit(EV_KEY, buttons_dev -> evbit);
	set_bit(EV_REP, buttons_dev -> evbit);
	
	set_bit(KEY_L, buttons_dev -> keybit);
	set_bit(KEY_S, buttons_dev -> keybit);
	set_bit(KEY_ENTER, buttons_dev -> keybit);
	
	input_register_device(button_dev);
	
	/* 硬件（定时器，中断）的初始化 */
	init_timer(&buttons_timer);
	buttons_timer.function = buttons_timer_function;
	add_timer(&buttons_timer);
	
	for(i = 0; i < 3; i++)
	{
		request_irq(pins_desc[i].irq, buttons_irq, IRQT_BOTHEDGE, pins_desc[i].name, &pins_desc[i]);
	}
	
	return 0;
}

static void buttons_exit(void)
{
	int i;
	for(i = 0; i < 3; i++)
	{
		free_irq(pins_desc[i].irq, buttons_irq, IRQT_BOTHEDGE, pins_desc[i].name, &pins_desc[i]);
	}
	del_timer(&buttons_timer);
	input_unregister_device(button_dev);
	input_free_device(button_dev);
}

module_init(buttons_init);

module_exit(buttons_exit);

MODULE_LICENSE("GPL");
