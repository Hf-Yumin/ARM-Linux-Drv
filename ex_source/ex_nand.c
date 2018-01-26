/*from: linux/drivers/mtd/nand/s3c2410.c*/

static struct nand_ecclayout nand_hw_eccoob = {
	.eccbytes = 3,
	.eccpos = {0, 1, 2},
	.oobfree = {{8, 8}}
};

struct s3c2410_nand_mtd {
	struct mtd_info			mtd;
	struct nand_chip		chip;
	struct s3c2410_nand_set		*set;
	struct s3c2410_nand_info	*info;
	int				scan_res;
};

enum s3c_cpu_type {
	TYPE_S3C2410,
	TYPE_S3C2412,
	TYPE_S3C2440,
};

struct s3c2410_nand_info {
	/* mtd info */
	struct nand_hw_control		controller;
	struct s3c2410_nand_mtd		*mtds;
	struct s3c2410_platform_nand	*platform;

	/* device info */
	struct device			*device;
	struct resource			*area;
	struct clk			*clk;
	void __iomem			*regs;
	void __iomem			*sel_reg;
	int				sel_bit;
	int				mtd_count;

	enum s3c_cpu_type		cpu_type;
};



/*------------------------------------------------------*/
static int s3c24xx_nand_probe(struct platform_device *pdev, enum s3c_cpu_type cpu_type)
static int s3c2410_nand_remove(struct platform_device *pdev)

/* PM Support */
#ifdef CONFIG_PM
static int s3c24xx_nand_suspend(struct platform_device *dev, pm_message_t pm)
static int s3c24xx_nand_resume(struct platform_device *dev)
#else
#define s3c24xx_nand_suspend NULL
#define s3c24xx_nand_resume NULL
#endif

static int s3c2410_nand_probe(struct platform_device *dev)
{
	return s3c24xx_nand_probe(dev, TYPE_S3C2410);
}

static struct platform_driver s3c2410_nand_driver = {
	.probe		= s3c2410_nand_probe,
	.remove		= s3c2410_nand_remove,
	.suspend	= s3c24xx_nand_suspend,
	.resume		= s3c24xx_nand_resume,
	.driver		= {
		.name	= "s3c2410-nand",
		.owner	= THIS_MODULE,
	},
};

/*------------------------------------------------------*/
static int __init s3c2410_nand_init(void)
static void __exit s3c2410_nand_exit(void)