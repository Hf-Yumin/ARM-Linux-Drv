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

static struct fb_info *s3c_lcd;

static volatile unsigned long *gpbcon;
static volatile unsigned long *gpccon;
static volatile unsigned long *gpdcon;	
static volatile unsigned long *gpgcon;
static volatile unsigned long *gpbdat;

static struct lcd_regs{
		unsigned long	lcdcon1;
		unsigned long	lcdcon2;
		unsigned long	lcdcon3;
		unsigned long	lcdcon4;
		unsigned long	lcdcon5;
    unsigned long	lcdsaddr1;
    unsigned long	lcdsaddr2;
    unsigned long	lcdsaddr3;
    unsigned long	redlut;
    unsigned long	greenlut;
    unsigned long	bluelut;
    unsigned long	reserved[9];
    unsigned long	dithmode;
    unsigned long	tpal;
    unsigned long	lcdintpnd;
    unsigned long	lcdsrcpnd;
    unsigned long	lcdintmsk;
    unsigned long	lpcsel;
};
static volatile struct lcd_regs *lcd_regs;

static u32 pseudo_palette[16];

static inline unsigned int chan_to_field(unsigned int chan, struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int s3c_lcdfb_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue, unsigned transp, struct fb_info *info)
{
	unsigned int val;
	
	if(regno > 16)
		return 1;
	
	val = chan_to_field(red, &info->var.red);
	val |= chan_to_field(green, &info->var.green);
	val |= chan_to_field(blue, &info->var.blue);
	
	pseudo_palette[regno] = val;
	return 0;
}

static struct fb_ops s3c_lcdfb_ops = {
	.owner				=	THIS_MODULE,
	.fb_setcolreg	=	s3c_lcdfb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

/* 分配一个 fb_info 结构 - 设置数据结构 - 硬件相关操作 - 注册 */
static int lcd_init(void)
{
	/* 1.分配 */
	s3c_lcd = framebuffer_alloc(0, NULL);
	
	/* 2.设置 */
	strcpy(s3c_lcd -> fix.id, "mylcd");
	//	s3c_lcd -> fix.smem_start = xxx;	它由dma_alloc_writecombine()函数来获得
	s3c_lcd -> fix.smem_len		= 240*320*16/8;
	s3c_lcd -> fix.type				=	FB_TYPE_PACKED_PIXELS;
	s3c_lcd -> fix.visual				=	FB_VISUAL_TRUECOLOR;
	s3c_lcd -> fix.line_length	=	240*2;
	
	s3c_lcd -> var.xres				=	240;
	s3c_lcd -> var.yres				=	320;
	s3c_lcd -> var.xres_virtual	=	240;
	s3c_lcd -> var.yres_virtual	=320;
	s3c_lcd -> var.bits_per_pixel	=	16;
	
	s3c_lcd -> var.red.offset		=	11;
	s3c_lcd -> var.red.length		=	5;
	s3c_lcd -> var.green.offset	=	5;
	s3c_lcd -> var.green.length	=	6;
	s3c_lcd -> var.blue.offset	=	0;
	s3c_lcd -> var.blue.length	=	5;
	
	s3c_lcd -> var.activate       = FB_ACTIVATE_NOW;
	
	s3c_lcd -> fbops					=	&s3c_lcdfb_ops;
	
	s3c_lcd -> screen_size			=	240*320*16/8;
	
	s3c_lcd -> pseudo_palette		=	pseudo_palette;
	
	/* 3.硬件相关操作: 配置GPIO - LCD控制器 - 分配framebuffer - 开启LCD */
	/* 3-1-1 引脚寄存器地址映射 */
	gpbcon	=	ioremap(0x56000010, 8);
	gpccon	=	ioremap(0x56000020, 4);
	gpdcon	=	ioremap(0x56000030, 4);
	gpgcon	=	ioremap(0x56000060, 4);
	
	gpbdat	=	gpbcon + 1;
		
	/* 3-1-2 配置寄存器 */
	*gpbcon &= ~3;
	*gpbcon |= 1;
	*gpbdat	&=	~1;
	
	*gpccon = 0xaaaaaaaa;
	*gpdcon = 0xaaaaaaaa;
	*gpgcon |= (3<<8);
	
	/* 3-2-1 LCD寄存器地址映射 */
	lcd_regs = ioremap(0x4D000000, sizeof(struct lcd_regs));
	
	/* 3-2-2 配置寄存器 */
	lcd_regs -> lcdcon1 = (4<<8) | (3<<5) | (0x0c<<1);										//[17:8]CLKVAL=4;[6:5]PNRMODE=3;[4:1]BPPMODE=0B1100
	lcd_regs -> lcdcon2 = (3<<24) | (319<<14) | (1<<6) | (0<<0);					//[31:24]VBPD=3;[23:14]LINEVAL=319;[13:6]VFPD=1;[5:0]VSPW=0
	lcd_regs -> lcdcon3 = (16<<19) | (239<<8) | (10<<0);									//[25:19]HBPD=16;[18:8]HOZVAL=239;[7:0]HFPD=10
	lcd_regs -> lcdcon4 = 4;																							//[7:0]HSPW=4			(sync pulse width)
	lcd_regs -> lcdcon5 = (1<<11) | (0<<10) | (1<<9) |(1<<8) | (1<<0);		//[11]FRM565;(invert)[10]INVVCLK;[9]INVVLINE;[8]INVVFRAME;[6]INVVDEN;[3]PWREN;[1]BSWP;[0]HWSWP
	
	/* 3-3-1 分配Framebuffer */
	s3c_lcd->screen_base = dma_alloc_writecombine(NULL, s3c_lcd->fix.smem_len, &s3c_lcd->fix.smem_start, GFP_KERNEL);
	
	/* 3-3-2 Framebuffer地址设定 */
	lcd_regs -> lcdsaddr1 = (s3c_lcd->fix.smem_start>>1) & (~(3<<30));
	lcd_regs -> lcdsaddr2 = ((s3c_lcd->fix.smem_start + s3c_lcd->fix.smem_len) >> 1) & 0x1fffff;
	lcd_regs -> lcdsaddr3 = (0x200<<11) | (240*16/16);
	
	/* 3-4 开启LCD */
	lcd_regs -> lcdcon1 |= 1;							//LCD控制器
	lcd_regs -> lcdcon5 |= (1<<3);				//LCD电源
	*gpbdat |= 1;													//背光
	
	/* 4.注册 */
	register_framebuffer(s3c_lcd);
	
	return 0;
}

static void lcd_exit(void)
{
	unregister_framebuffer(s3c_lcd);
	
	*gpbdat &= ~1;
	lcd_regs -> lcdcon5 &= ~(1<<3);
	lcd_regs -> lcdcon1 &= ~1;
	
	iounmap(lcd_regs);
	iounmap(gpbcon);
	iounmap(gpccon);
	iounmap(gpdcon);
	iounmap(gpgcon);
	
	framebuffer_release(s3c_lcd);
}

module_init(lcd_init);
module_exit(lcd_exit);
MODULE_LICENSE("GPL");
