//设备驱动分离；平台设备驱动
//s3c2410fb.c

static struct s3c2410fb_mach_info *mach_info;

static int __init s3c2410fb_map_video_memory(struct s3c2410fb_info *fbi)
{
	dprintk("map_video_memory(fbi=%p)\n", fbi);
	
	fbi->map_size = PAGE_ALIGN(fbi->fb->fix.smem_len + PAGE_SIZE);
	fbi->map_cpu = dma_alloc_writecombine(fbi->dev, fbi->map_size, &fbi->map_dma, GPF_KERNEL);
	fbi->map_size = fbi->fb->fix.smem_len;			//??
	
	if(fbi->map_cpu)
		{
			dprintk("map_video_memory:clear %p:%08x\n", fbi->map_cpu, fbi->map_size);
			
			fbi->screen_dma					=	fbi->map_dma;
			fbi->fb->screen_base		=	fbi->map_cpu;
			fbi->fb->fix.smem_start	=	fbi->screen_dma;
			
			dprintk("map_video_memory: dma=%08x, cpu=%p, size=%08x\n", fbi->map_dma, fbi->map_cpu, fbi->fb->fix.smem_len);	
		}
		return fbi->map_cpu ? 0　：-ENOMEM;
}



static void s3c2410fb_write_palette(struct s3c2410fb_info *fbi)
{
	unsigned int i;
	unsigned long ent;
	
	fbi->palette_ready = 0;
	
	for(i = 0; i < 256; i++)
	{
		if((ent = fbi->palette_buffer[i]) == PALETTE_BUFF_CLEAR)
			continue;
		writel(ent, S3C2410_TFTPAL(i));
			
		if(readw(S3C2410_TFTPAL(i)) == ent)
			fbi->palette_buffer[i] = PALETTE_BUFF_CLEAR;
		else
			fbi->palette_ready = 1;
	}
}

static irqreturn_t s3c2410fb_irq(int irq, void *dev_id)
{
	struct s3c2410fb_info *fbi = dev_id;
	unsigned long lcdirq = readl(S3C2410_LCDINTPND);
	
	if(lcdirq & S3C2410_LCDINT_FRSYNC)			//The frame has asserted the interrupt request.
		{
			if(fbi->palette_ready)
				s3c2410fb_write_palette(fbi);
			
			writel(S3C2410_LCDINT_FRSYNC, S3C2410_LCDINTPND);
			writel(S3C2410_LCDINT_FRSYNC, S3C2410_LCDSRCPND);
		}
	return IRQ_HANDLED;
}

static driver_name[] = "s3c2410fb";
static int __init s3c2410fb_probe(struct platform_device *pdev)
{
	struct s3c2410fb_info *info;
	struct fb_info *fbinfo;
	struct s3c2410_hw *mregs;
	int ret;
	int irq;
	int i;
	u32 lcdcon1;
	
	mach_info = pdev->dev.platform_data;
	if(mach_info == NULL)
		{
			dev_err(&pdev->dev, "no platform data for lcd, cannot attach\n");
			return -EINVAL;
		}
	mregs = &mach_info->regs;
	
	irq = platform_get_irq(pdev, 0);		//从platform_device中的“资源”获取中断信息
	if(irq < 0)
		{
			dev_err(&pdev->dev, "no irq for device\n");
			return -ENOENT;
		}
		
	fbinfo = framebuffer_alloc(sizeof(struct s3c2410fb_info), pdev->dev);
	if(!fbinfo)
		{
			return -ENOMEM;
		}
	
	info = fbinfo->par;
	info->fb = fbinfo;
	info->dev = &pdev->dev;
	
	platform_set_drvdata(pdev, fbinfo);
	
	dprintk("devinit\n");
	
	strcpy(fbinfo->fix.id, driver_name);					//driver_name[] = "s3c2410fb"
	
	memcpy(&info->regs, &mach_info->regs, sizeof(info->regs));
	info->mach_info = pdev->dev.platform_data;

//	/*关闭LCD控制器*/
//	info->regs.lcdcon1 &= ~S3C2410_LCDCON1_ENVID;
//	lcdcon1 = readl(S3C2410_LCDCON1);
//	writel(lcdcon1 & ~S3C2410_LCDCON1_ENVID, S3C2410_LCDCON1);
//	
//	/*关闭背光*/
//	a3c2410_gpio_setpin(S3C2410_GPB0, 0);
	
	//初始化固定参数
	fbinfo->fix.type = FB_TYPE_PACKED_PIXELS;
	fbinfo->fix.type_aux = 0;
	fbinfo->fix.xpanstep = 0;
	fbinfo->fix.ypanstep = 0;
	fbinfo->fix.ywrapstep = 0;
	fbinfo->fix.accel = FB_ACCEL_NONE;
	
	//初始化可变参数
	fbinfo->var.nonstd = 0;
	fbinfo->var.activate = FB_ACTIVATE_NOW;
	fbinfo->var.height = mach_info->height;
	fbinfo->var.width = mach_info->width;
	fbinfo->var.accel_flags = 0;
	fbinfo->var.vmode = FB_VMODE_NONINTERLACED;
	
	fbinfo->fb_ops = &s3c2410fb_ops;
	fbinfo->flags = FBINFO_FLAG_DEFAULT;
	fbinfo->pseudo_palette = &info->pseudo_pal;
	
	//初始化可变参数中的分辨率和BPP
	fbinfo->var.xres = mach_info->xres.defval;
	fbinfo->var.xres.virtual = mach_info->xres.defval;
	fbinfo->var.yres = mach_info->yres.defval;
	fbinfo->var.yres.virtual = mach_info->yres.defval;
	fbinfo->var.bits_per_pixel = mach_info->bpp.defval;
	
	//上下边界和垂直同步
	fbinfo->var.upper_margin 	= S3C2410_LCDCON2_GET_VBPD(mregs->lcdcon2) + 1;
	fbinfo->var.lower_margin 	= S3C2410_LCDCON2_GET_VFPD(mregs->lcdcon2) + 1;
	fbinfo->var.vsync_len 		= S3C2410_LCDCON2_GET_VSPW(mregs->lcdcon2) + 1;
	
	//左右边界和垂直同步
	fbinfo->var.left_margin	 	= S3C2410_LCDCON3_GET_HBPD(mregs->lcdcon3) + 1;
	fbinfo->var.right_margin 	= S3C2410_LCDCON3_GET_HFPD(mregs->lcdcon3) + 1;
	fbinfo->var.hsync_len 		= S3C2410_LCDCON4_GET_HSPW(mregs->lcdcon4) + 1;
	
	//RGB参数的位数及位置
	fbinfo->var.red.offset		=	11;
	fbinfo->var.green.offset	=	5;
	fbinfo->var.blue.offst		=	0;
	fbinfo->var.trandp.offset	=	0;
	fbinfo->var.red.length		=	5;
	fbinfo->var.green.length	=	6;
	fbinfo->var.blue.length		=	5;
	fbinfo->var.transp.length	=	0;
	
	fbinfo->fix.smem_len = mach_info->xres.max * mach_info->yres.max * mach_info->bpp.max/8;
	
	for(i = 0; i < 256; i++)
		info->palette_buffer[i] = PALETTE_BUFF_CLEAR;
		
	//申请 I/O内存区
	if(!request_mem_region((unsigned long)S3C24XX_VA_LCD, SZ_1M, "s3c2410-lcd"))		//retrn 0 means fail
		{
			ret = -EBUSY;
			goto dealloc_fb;
		}
	dprintk("got LCD region\n");
	
	//申请中断
	ret = request_irq(irq, s3c2410fb_irq, IRQF_DISALED, pdev->name, info);				//return 0 means success
	if(ret)
		{
			dev_err(&pdev->dev, "cannot get irq %d - err %d", irq, ret);
			ret = -EBUSY;
			goto release_mem;
		}
		
	//获得时钟并使能
	info->clk = clk_get(NULL,"lcd");
	if(! info->clk || IS_ERR(info->clk))
		{
			printk(KERN_ERR "failed to get lcd clock source\n");
			ret = -ENOENT;
			goto release_irq;
		}
	clk_enable(info->clk);
	dprintk("got and enable clock\n");
	msleep(1);
	
	//初始化显示缓冲区
	ret = s3c2410fb_map_video_memory(info);
	if(ret)
		{
			printk(KERN_ERR "failed to allocate vedio RAM: %d\n", ret);
			ret = -ENOMEM;
			goto release_clock;
		}
	dprintk("got video memory\n");
	
	ret = s3c2410fb_init_registers(info);
	ret = s3c2410fb_check_var(&fbinfo->var, fbinfo);
	
	//注册fb_info结构
	ret = register_framebuffer(fbinfo);
	if(ret < 0)
		{
			printk(KERN_ERR "failed to register framebuffer device: %d\n", ret);
			goto free_video_memory;
		}
	
	//创建设备文件
	device_create_file(&pdev->dev, &dev_attr_debug);
	printk(KERN_INFO "fb%d: %s frame buffer device\n", fbinfo->node, fbinfo->fix.id);
	
	return 0;
	
	free_video_memory:
		s3c2410fb_unmap_video_memory(info);
	release_clock:
		clk_disable(info->clk);
		clk_put(info->clk);
	release_irq:
		free_irq(irq, info);
	release_mem:
		release_mem_region((unsigned long)S3C24XX_VA_LCD, S3C24XX_SZ_LCD);
	dealloc_fb:
		framebuffer_release(fbinfo);
		return ret;
}

s3c2410fb_remove
s3c2410fb_suspend
s3c2410fb_resume

static struct flatform_driver s3c2410fb_driver = {
	.probe = s3c2410fb_probe,
	.remove = s3c2410fb_remove,
	.suspend = s3c2410fb_suspend,
	.resume = s3c2410fb_resume,
	.driver = {
		.name = "s3c2410-lcd",
		.owner = THIS_MODULE,
	},
};

static int __devinit s3c2410fb_init(void)
{
	return platform_driver_register(&s3c2410fb_driver);
}

static void __exit s3c2410fb_cleanup(void)
{
	platform_driver_unregister(&s3c2410fb_driver);
}

module_init(s3c2410fb_init);
module_exit(s3c2410fb_cleanup);
MODULE_LISENCE("GPL");
