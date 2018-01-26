#define WINDOW_SIZE ...
#define WINDOW_ADDR ...

static struct map_info xxx_map = {
	.name = "xxx_Flash",
	.size = WINDOW_SIZE,
	.bankwidth = 1,
	.phys = WINDOW_ADDR
};

static struct mtd_partition xxx_partitions[] = {
	{
		.name = "Driver A",
		.offset = 0,
		.size = 0x0e0000
	},
	...
};

#define NUM_PARTITIONS ARRAY_SIZE(xxx_partitions)			//ARRY_SIZE

static struct mtd_info *mymtd;

static int __init init_xxx_map(void)
{
	int rc = 0;
	
	xxx_map.virt = ioremap_nocache(xxx_map.phys, xxx_map.size);
	
	if(!xxx_map.virt)
		{
			printk(KERN_ERR "Failed to ioremap_nocache\n");
			rc = -EIO;
			goto err2;
		}
	
	simple_map_init(&xxx_map);
	
	mymtd = do_map_probe("jedec_probe", &xxx_map);
	if(!mymtd)
		{
			rc = -ENXIO;
			goto err1;
		}
	
	mymtd->owner = THIS_MODULE;
	
	add_mtd_partitions(mymtyd, xxx_partitions, NUM_PARTITIONS);
	
	return 0;
	
err1:
	map_destroy(mymtd);
	iounmap(xxx_map.virt);
err2:
	return rc;
}

static void __exit claenup_xxx_map(void)
{
	if(mymtd)
		{
			del_mtd_partitions(mymtd);
			map_destroy(mymtd);
		}
	
	if(xxx_map.virt)
		{
			iounmap(xxx_map.virt);
			xxx_map.virt = NULL;
		}
}

/*-----------------------------------------------------*/

#define CHIP_PHYSICAL_ADDRESS ...
#define NUM_PATITIONS 2
static struct mtd_partition partition_info[] = {
	{
		.name = "Flash partition 1",
		.offset = 0,
		.size = 8 * 1024 *1024
	},
	{
		.name = "Flash partition 2",
		.offset = MTDPART_OFS_NEXT,
		.size = MTDPART_SIZ_FULL
	},
};

int __init board_init(void)
{
	struct nand_chip *this;
	int err = 0;
	
	board_mtd = kmalloc(sizeof(struct mtd_info) + sizeof(struct nand_chip), GFP_KERNEL);
	if(!board_mtd)
		{
			printk("Unable to allocate memory for NAND MTD device.\n");
			err = -ENOMEM;
			goto out;
		}
	memset((char *)board_mtd, 0, sizeof(struct mtd_info) + sizeof(struct nand_chip));
	
	baseaddr = (unsigned long) ioremap(CHIP_PHYSICAL_ADDRESS, 1024);
	if(!baseaddr)
		{
			printk("Ioremap to access NAND chip failed\n");
			err = -EIO;
			goto out_mtd;
		}
	
	/*获得私有数据指针*/
	this = (struct nand_chip *)(&board_mtd[1]);				//borad_mtd[0]-mtd_info; board_mtd[1]-nand_chip
	
	board_mtd->priv = this;
	
	this->IO_ADDR_R = baseaddr;
	this->IO_ADDR_W = baseaddr;
	
	this->hwcontrol = board_hwcontorl;								//GPIO方式的硬件控制
	this->chip_delay = CHIP_DEPENDEND_COMMAND_DELAY;
	this->dev_ready = borad_dev_ready;								//返回设备ready状态
	this->eccmode = NAND_ECC_SOFT;
	
	if(nand_scan(board_mtd, 1))
		{
			err = -ENXIO;
			goto out_ior;
		}
	
	add_mtd_partitions(board_mtd, partition_info, NUM_PARTITIONS);
	goto out;

out_ior:
	iounmap((void *)baseaddr);
out_mtd:
	kfree(board_mtd);
out:
	return err;
}

static void __exit board_exit(void)
{
	nand_release(board_mtd);
	iounmap((void *)baseaddr);
	kfree(board_mtd);
}

static void board_hwcontorl(struct mtd_info *mtd, int cmd)
{
	switch(cmd)
	{
		case NAND_CTL_SETCLE:
			/*Set CLE pin high*/
			break;
		
		case NAND_CTL_CLRCLE:
			/*Set CLE pin low*/
			break;
		
		case NAND_CTL_SETALE:
			/*Set ALE pin high*/
			break;
		
		case NAND_CTL_CLRALE:
			/*Set ALE pin low*/
			break;
		
		case NAND_CTL_SETNCE:
			/*Set nCE pin low*/
			break;
		
		case NAND_CTL_CLRNCE:
			/*Set nCE pin high*/
			break;
	}
}

static int board_dev_ready(struct mtd_info *mtd)
{
	return xxx_read_ready_bit();
}

/*-----------------------------------------------------*/

/*nand_chip中的nand_oobinfo成员的默认设置*/
/*useecc 定义 ECC 的放置模式*/
/*eccbytes 定义 ECC 字节数*/
/*eccpos 是 ECC 校验码的放置位置*/
/*oobfree 中记录还可被自由使用的 OOB 区域的开始位置和长度*/
static struct nand_ecclayout nand_oob_8 = {				//页尺寸：256，OOB字节数：8
	.eccbytes = 3,
	.eccpos = {0, 1, 2},
	.oobfree = {
		{.offset = 3,
		 .length = 2},
		{.offset = 6,
		 .length = 2}}
};

static struct nand_ecclayout nand_oob_16 = {			//页尺寸：512，OOB字节数：16
	.eccbytes = 6,
	.eccpos = {0, 1, 2, 3, 6, 7},
	.oobfree = {
		{.offset = 8,
		 . length = 8}}
};

static struct nand_ecclayout nand_oob_64 = {			//页尺寸：2048，OOB字节数：64
	.eccbytes = 24,
	.eccpos = {
		   40, 41, 42, 43, 44, 45, 46, 47,
		   48, 49, 50, 51, 52, 53, 54, 55,
		   56, 57, 58, 59, 60, 61, 62, 63},
	.oobfree = {
		{.offset = 2,
		 .length = 38}}
};
