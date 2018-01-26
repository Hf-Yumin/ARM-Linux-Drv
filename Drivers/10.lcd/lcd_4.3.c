/*头文件是直接从 "s3c2410fb.c" 抄过来的 :P*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/div64.h>

#include <asm/mach/map.h>
#include <asm/arch/regs-lcd.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/fb.h>

static int s3c_fb_setcolreg(unsigned int regno, unsigned int red,
			     unsigned int green, unsigned int blue,
			     unsigned int transp, struct fb_info *info);

static struct fb_info *s3c_fb;

static struct fb_ops s3c_fb_ops = {
	.owner = THIS_MODULE,
	.fb_setcolreg	= s3c_fb_setcolreg,
	.fb_fillrect	=	cfb_fillrect,
	.fb_copyarea	=	cfb_copyarea,
	.fb_imageblit	=	cfb_imageblit,
};

static u32 s3c_pseudo_pal[16];

struct lcd_regs{
	unsigned long lcdcon1;
	unsigned long lcdcon2;
	unsigned long lcdcon3;
	unsigned long lcdcon4;
	unsigned long lcdcon5;
	unsigned long lcdsaddr1;
	unsigned long lcdsaddr2;
	unsigned long lcdsaddr3;
	unsigned long redlut;
	unsigned long greenlut;
	unsigned long bluelut;
	unsigned long reserved[8];
	unsigned long dithmode;
	unsigned long tpal;
	unsigned long lcdintpnd;
	unsigned long lcdsrcpnd;
	unsigned long lcdintmsk;
	unsigned long tconsel;
};

static volatile unsigned long *gpbcon;
static volatile unsigned long *gpbdat;
static volatile unsigned long *gpccon;
static volatile unsigned long *gpdcon;
static volatile unsigned long *gpgcon;

static volatile struct lcd_regs *lcd_regs;

static inline unsigned int chan_to_field(unsigned int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int s3c_fb_setcolreg(unsigned int regno, unsigned int red,
			     unsigned int green, unsigned int blue,
			     unsigned int transp, struct fb_info *info)
{
	unsigned int val;
	if(regno > 16)
		return 1;
	
	val = chan_to_field(red, &info->var.red);
	val |= chan_to_field(green, &info->var.green);
	val |= chan_to_field(blue, &info->var.blue);
	
	s3c_pseudo_pal[regno] = val;
	return 0;
}

static __init int lcd_init(void)
{
	s3c_fb = framebuffer_alloc(0, NULL);				//分配一个fb_info结构体
	
	/*设置fb_info*/
	strcpy(s3c_fb->fix.id, "mylcd");
	s3c_fb -> fix.smem_len	=	480*272*2;
	s3c_fb -> fix.type			=	FB_TYPE_PACKED_PIXELS;
	s3c_fb -> fix.visual		=	FB_VISUAL_TRUECOLOR;
	s3c_fb -> fix.line_length	=	480*2;
	
	s3c_fb -> var.xres	=	480;
	s3c_fb -> var.yres	=	272;
	s3c_fb -> var.xres_virtual	=	480;
	s3c_fb -> var.yres_virtual	=	272;
	s3c_fb -> var.bits_per_pixel	=	16;
	
	s3c_fb -> var.red.offset	=	11;
	s3c_fb -> var.red.length	=	5;
	s3c_fb -> var.green.offset	=	5;
	s3c_fb -> var.green.length	=	6;
	s3c_fb -> var.blue.offset	=	0;
	s3c_fb -> var.blue.length	=	5;
	
	s3c_fb -> var.activate = FB_ACTIVATE_NOW;
	
	s3c_fb -> fbops	=	&s3c_fb_ops;						//底层操作函数
	s3c_fb -> flags		=	FBINFO_FLAG_DEFAULT;
	s3c_fb -> pseudo_palette	=	&s3c_pseudo_pal;	//伪色表
	s3c_fb -> screen_size	=	480*272*2;						/* Amount of ioremapped VRAM or 0 */ 
	
	/*设置寄存器用于LCD*/
	
	/*配置GPIO*/
	gpbcon = ioremap(0x56000010, 8);
	gpbdat = gpbcon+1;
	gpccon = ioremap(0x56000020, 4);
	gpdcon = ioremap(0x56000030, 4);
	gpgcon = ioremap(0x56000060, 4);

  *gpccon  = 0xaaaaaaaa;  
	*gpdcon  = 0xaaaaaaaa;  
	
	*gpbcon &= ~(3);   
	*gpbcon |= 1;
	*gpbdat &= ~1;    

	*gpgcon |= (3<<8); 
	
	/*根据时序要求，设置LCD控制器*/
	lcd_regs = ioremap(0x4D000000, sizeof(struct lcd_regs));

	lcd_regs->lcdcon1  = (4<<8) | (3<<5) | (0x0c<<1);
	lcd_regs->lcdcon2  = (1<<24) | (271<<14) | (1<<6) | (9);
	lcd_regs->lcdcon3 = (1<<19) | (479<<8) | (1);
	lcd_regs->lcdcon4 = 40;
	lcd_regs->lcdcon5 = (1<<11) | (0<<10) | (1<<9) | (1<<8) | (1<<0);
	
	/*分配framebuffer，把地址告诉LCD控制器*/
	s3c_fb -> screen_base = dma_alloc_writecombine(NULL, s3c_fb->fix.smem_len, &s3c_fb->fix.smem_start, GFP_KERNEL);
	lcd_regs->lcdsaddr1  = (s3c_fb->fix.smem_start >> 1) & ~(3<<30);
	lcd_regs->lcdsaddr2  = ((s3c_fb->fix.smem_start + s3c_fb->fix.smem_len) >> 1) & 0x1fffff;
	lcd_regs->lcdsaddr3  = (480*16/16);
	
	/* 启动LCD */
	lcd_regs->lcdcon1 |= (1<<0); /* 使能LCD控制器 */
	lcd_regs->lcdcon5 |= (1<<3); /* 使能LCD本身 */
	*gpbdat |= 1;     /* 输出高电平, 使能背光 */	
	
	register_framebuffer(s3c_fb);								//注册fb_info
	
	return 0;
}

static __exit void lcd_exit(void)
{
	unregister_framebuffer(s3c_fb);
	
	lcd_regs->lcdcon1 &= ~(1<<0);
	lcd_regs->lcdcon5 &= ~(1<<3);
	*gpbdat &= ~1;
	
	dma_free_writecombine(NULL, s3c_fb->fix.smem_len, s3c_fb->screen_base, s3c_fb->fix.smem_start);
	
	iounmap(lcd_regs);
	iounmap(gpbcon);
	iounmap(gpccon);
	iounmap(gpdcon);
	iounmap(gpgcon);
	
	framebuffer_release(s3c_fb);
}

module_init(lcd_init);
module_exit(lcd_exit);

MODULE_LICENSE("GPL");
