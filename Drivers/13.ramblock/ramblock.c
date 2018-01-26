#include <linux/module.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/dma.h>

static struct gendisk *ramblock_disk;
static struct quest_queue_t *ramblock_queue;
static int major;

static DEFINE_SPINLOCK(ramblock_lock);

#define RAMBLOCK_SIZE (1024*1024)
static unsigned char *ramblock_buf;

static int ramblock_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->heads = 2;
	geo->cylinders = 32;
	geo->sectors = RAMBLOCK_SIZE/2/32/512;
	return 0;
}

static struct block_device_operations ramblock_fops = {
	.owner = THIS_MODULE,
	.getgeo = ramblock_getgeo,
};

/*---------------------------------------------------------*/
static void do_ramblock_request(request_queue_t *q)
{
//	static int r_cnt = 0;
//	static int w_cnt = 0;
	struct request *req;
	
	while((req = elv_next_request(q)) != NULL)
	{
		unsigned long offset = req->sector * 512;
		unsigned long len = req->current_nr_sectors * 512;
		
		if(rq_data_dir(req) == READ)
			{
				memcpy(req->buffer, ramblock_buf + offset, len);
			}
		else
			{
				memcpy(ramblock_buf + offset, req->buffer, len);
			}
		
		end_request(req, 1);
	}
}

static int __init ramblock_init(void)
{
	ramblock_disk = alloc_disk(16);
	
	ramblock_queue = blk_init_queue(do_ramblock_request, &ramblock_lock);
	ramblock_disk->queue = ramblock_queue;
	
	major = register_blkdev(0, "ramblock");
	ramblock_disk->major = major;
	ramblock_disk->first_minor = 0;
	sprintf(ramblock_disk->disk_name, "ramblock");
	ramblock_disk->fops = &ramblock_fops;
	set_capacity(ramblock_disk, RAMBLOCK_SIZE/512);
	
	ramblock_buf = kzalloc(RAMBLOCK_SIZE, GFP_KERNEL);
	
	add_disk(ramblock_disk);
	
	return 0;
}

static void __exit ramblock_exit(void)
{
	unregister_blkdev(major, "ramblock");
	del_gendisk(ramblock_disk);
	put_disk(ramblock_disk);
	blk_cleanup_queue(ramblock_queue);
	kfree(ramblock_buf);
}

module_init(ramblock_init);
module_exit(ramblock_exit);
MODULE_LICENSE("GPL");
