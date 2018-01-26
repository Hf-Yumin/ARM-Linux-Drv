#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>
 
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
 
#include <asm/io.h>
 
#include <asm/arch/regs-nand.h>
#include <asm/arch/nand.h>

#define		TACLS		0
#define		TWRPH0	1
#define		TWRPH1	0

struct s3c_nand_regs {
	unsigned long nfconf;
	unsigned long nfcont;
	unsigned long nfcmd;
	unsigned long nfaddr;
	unsigned long nfdata;
	unsigned long nfeccd0;
	unsigned long nfeccd1;
	unsigned long nfsecc;
	unsigned long nfstat;
	unsigned long nfestat0;
	unsigned long nfestat1;
	unsigned long nfmecc0;
	unsigned long nfmecc1;
	unsigned long nfsecc;
	unsigned long nfsblk;
	unsigned long nfeblk;
};

static struct mtd_info *s3c_mtd;
static struct clk *clk;
static volatile struct s3c_nand_regs *s3c_nand_regs;

static struct mtd_partition s3c_nand_parts[] = {
	[0] = {
		.name = "bootloader",
		.offset = 0,
		.size = 0x00040000,
	},
	[1] = {
		.name = "params",
		.offset = MTDPART_OFS_APPEND,
		size = 0x00020000,
	},
	[2] = {
		.name = "kernel",
		.offset = MTDPART_OFS_APPEND,
		.size = 0x00200000,
	},
	[3] = {
		.name = "root",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	}
};

static void s3c2440_select_chip(struct mtd_info *mtd, int chipnr)
{
	if(chipnr == -1)
		{
			s3c_nand_regs->nfcont |= (1<<1);
		}
	else
		{
			s3c_nand_regs->nfcont &= ~(1<<1);
		}
}

static void s3c2440_cmd_ctrl(struct mtd_info *mtd, int dat, unsigned int ctrl)
{
	if(ctrl & NAND_CLE)
		{
			s3c_nand_regs->nfcmd = dat;
		}
	else
		{
			s3c_nand_regs->nfaddr = dat;
		}
}

static void s3c2440_dev_ready(struct mtd_info *mtd)
{
	return(s3c_mtd_regs->nfstat & (1<<0));
}

static int s3c_nand_init(void)
{
	struct nand_chip *s3c_nand;
	int err = 0;
	
	s3c_mtd = kzalloc(sizeof(struct mtd_info) + sizeof(struct nand_chip), GFP_KERNEL);
	if(!s3c_mtd)
		{
			printk(KERN_ERR "Failed to allocate mtd_info struct.\n");
			err = -ENOMEM;
			goto out1;
		}
	
	s3c_nand_regs = ioremap(0x4E000000, sizeof(struct s3c_nand_regs));
	if(!s3c_nand_regs)
		{
			printk(KERN_ERR "Ioremap to access NAND registers failed.\n");
			err = -EIO;
			goto out2;
		}
	
	/*nand chip*/
	s3c_nand = (struct nand_chip *)(&s3c_mtd[1]);
	s3c_mtd->priv = s3c_nand;
	
	s3c_nand->IO_ADDR_R = &s3c_nand_regs->nfdata;
	s3c_nand->IO_ADDR_W = &s3c_nand_regs->nfdata;
	s3c_nand->select_chip = s3c2440_select_chip;
	s3c_nand->cmd_ctrl		=	s3c2440_cmd_ctrl;
	s3c_nand->dev_ready		=	s3c2440_dev_ready;
	s3c_nand->ecc.mode		=	NAND_ECC_SOFT;
	
	/*Hardware Init*/
	/*1.enable clock*/
	clk = clk_get(NULL, "nand");
	if(!clk || IS_ERR(clk))
		{
			printk(KERN_ERR "Failed to get nand clock source.\n");
			err = -ENOENT;
			goto out3;
		}
	clk_enable(clk);
	/*2.set up nand registers (refer to data sheet)*/
	s3c_nand_regs->nfconf = (TACLS<<12) | (TWRPH0<<8) | (TWRPH1<<4);
	s3c_nand_regs->nfcont = (1<<1) | (1<<0);
	
	/*scan nand chip and add partitions*/
	s3c_mtd->owner = THIS_MODULE;
	if(nand_scan(s3c_mtd, 1))
		{
			printk(KERN_ERR "Failed to find the nand chip.\n");
			err = -ENXIO;
			goto out4;
		}
	add_mtd_partitions(s3c_mtd, s3c_nand_parts, 4);
	
	return 0;

out4:
	clk_disable(clk);
	clk_put(clk);
out3:
	iounmap(s3c_regs);
out2:
	kfree(s3c_mtd);
out1:
	return err;
}

static void __exit s3c_nand_exit(void)
{
	del_mtd_partitions(s3c_mtd);
	if(clk)
		{
			clk_disable(clk);
			clk_put(clk);
			clk = NULL;
		}
	iounmap(s3c->regs);
	kfree(s3c_mtd);
}

module_init(s3c_nand_init);
module_exit(s3c_nand_exit);
MODULE_LICENSE("GPL");
