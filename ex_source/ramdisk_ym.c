#include <linux/string.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/buffer_head.h>		/* for invalidate_bdev() */
#include <linux/backing-dev.h>
#include <linux/blkpg.h>
#include <linux/writeback.h>

#include <asm/uaccess.h>

static struct gendisk *rd_disks[CONFIG_BLK_DEV_RAM_COUNT];
static struct block_device *rd_bdev[CONFIG_BLK_DEV_RAM_COUNT];
static struct request_queue *rd_queue[CONFIG_BLK_DEV_RAM_COUNT];

int rd_size = CONFIG_BLK_DEV_RAM_SIZE;
static int rd_blocksize = CONFIG_BLK_DEV_RAM_BLOCKSIZE;

/*-----------------------------------------------------------*/

static void make_page_uptodate(struct page *page)
{
	if(page_has_buffer(page))
		{
			struct buffer_head *bh = page_buffers(page);
			struct buffer_head *head = bh;
			
			do{
				if(!buffer_uptodate(bh))
					{
						memset(bh->b_data, 0, bh->b_size);
						set_buffer_uptodate(bh);
					}
			}while((bh = bh->b_this_page) != head);
		}
	else
		{
			memset(page_address(page), 0, PAGE_CACHE_SIZE);
		}
	flush_dcache_page(page);
	SetPageUptodate(page);
}

/*------------------------------------*/

static int ramdisk_readpage(struct file *file, struct page *page)
{
	if(!PageUptodate(page))
		make_page_uptodate(page);
	unlock_page(page);
	return 0;
}

static int ramdisk_prepare_write(struct file *file, struct page *page, unsigned offset, unsigned to)
{
	if(!PageUptodate(page))
		make_page_uptodate(page);
	return 0;
}

static int ramdisk_commit_write(struct file *file, struct page *page, unsigned offset, unsigned to)
{
	set_page_dirty(page);
	return 0;
}

static int ramdisk_writepage(struct page *page, struct writeback_contorl *wbc)
{
	if(!PageUptodate(page))
		make_page_uptodate(page);
	SetPageDirty(page);
	if(wbc->for_reclaim)
		return AOP_WRITEPAGE_ACTIVATE;
	unlock_page(page);
	return 0;
}

static int ramdisk_writepages(struct address_space *mapping, struct writeback_contorl *wbc)
{
	return 0;
}

static int ramdisk_set_page_dirty(struct page *page)
{
	if(!TestSetPageDirty(page))
		return 1;
	return 0;
}

static const struct address_space_operations ramdisk_aops = {
	.readpage = ramdisk_readpage,
	.prepare_write = ramdisk_prepare_write,
	.commit_write = ramdisk_commit_write,
	.writepage = ramdisk_writepage,
	.set_page_dirty = ramdisk_set_page_dirty,
	.writepages = ramdisk_writepages,
};

/*-----------------------------------------------------------*/

static int rd_blkdev_pagecache_IO(int rw, struct bio_vec *vec, sector_t sector, struct address_space *mapping)
{
	pgoff_t index = sector >> (PAGE_CACHE_SHIFT - 9);
	unsigned int vec_offset = vec->bv_offset;
	int offset = (sector<<9) & ~PAGE_CACHE_MASK;
	int size = vec->bv_len;
	int err = 0;
	
	do{
		int count;
		struct page *page;
		char *src;
		char *dst;
		
		count = PAGE_CACHE_SIZE - offset;
		if(count > size)
			count = size;
		size -= count;
		
		page = grab_cache_page(mapping, index);
		if(!page)
			{
				err = -ENOMEM;
				goto out;
			}
		
		if(!PageUptodate(page))
			make_page_uptodate(page);
		
		index++;
		
		if(rw == READ)
			{
				src = kmap_atomic(page, KM_USER0) + offset;						//refer to LDD3 Chapter 15
				dst = kmap_atomic(vec->bv_page, KM_USER1) + vec_offset;
			}
		else
			{
				src = kmap_atomic(vec->bv_page, KM_USER0) + vec_offset;
				dst = kmap_atomic(page, KM_USER1) + offset;
			}
		offset = 0;
		vec_offser += count;
		
		memcpy(dst, src, count);
		
		kunmap_atomic(src, KM_USER0);
		kunmap_atomic(dst, KM_USER1);
		
		if(rw == READ)
			flush_dcache_page(vec->bv_page);
		else
			set_page_dirty(page);
			unlock_page(page);
			put_page(page);
	}while(size);

out:
	return err;
}

static int rd_make_request(request_queue_t *q, struct bio *bio)
{
	struct block_device *bdev = bio->bi_bdev;
	struct address_space *mapping = bdev->bd_inode->i_mapping;
	sector_t sector = bio->bi_sector;
	unsigned long len = bio->bi_size >> 9;
	int rw = bio_data_dir(bio);
	struct bio_vec *bvec;
	int ret = 0, i;
	
	if(sector + len > get_capacity(bdev->bd_disk))
		goto fail;
	
	if(rw == READA)
		rw = READ;
	
	bio_for_each_segment(bvec, bio, i)
	{
		ret |= rd_blkdev_pagecache_IO(rw, bvec, sector, mapping);
		sector += bvec->bv_len >> 9;
	}
	if(ret)
		goto fail;
	
	bio_endio(bio, bio->bi_size, 0);
	return 0;
fail:
	bio_io_error(bio, bio->bi_size);
	return 0;
}

/*------------------------------------*/

static int rd_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned lon arg)
{
	int error;
	struct block_device *bdev = inode->i_bdev;
	
	if(cmd != BLKFLSBUF)						//°ÑÔàÒ³Ð´»Ø´ÅÅÌ-flush
		return -ENOTTY;
		
	err = -EBUSY;
	mutex_lock(&bdev->bd_mutex);
	if(bdev->bd_openers <= 2)
		{
			truncate_inode_pages(bdev->bd_inode->i_mapping, 0);
			error = 0;
		}
		mutex_unlock(&bdev->bd_mutex);
		return error;
}

/*------------------------------------*/

/*
 * This is the backing_dev_info for the blockdev inode itself.  It doesn't need
 * writeback and it does not contribute to dirty memory accounting.
 */
static struct backing_dev_info rd_backing_dev_info = {
	.ra_pages = 0,
	.capabilities = BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK | BDI_CAP_MAP_COPY,
	.unplug_io_fn = default_unplug_io_fn,
};

/*
 * This is the backing_dev_info for the files which live atop the ramdisk
 * "device".  These files do need writeback and they do contribute to dirty
 * memory accounting.
 */
static struct backing_dev_info rd_file_backing_dev_info = {
	.ra_pages = 0,
	.capabilities = BDI_CAP_MAP_COPY,
	.unplug_io_fn = default_unplug_io_fn,
};

/*------------------------------------*/

static int rd_open(struct inode *inode, struct file *filp)
{
	unsigned uint = iminor(inode);
	
	if(rd_bdev[unit] == NULL)
		{
			struct block_device *bdev = inode->i_bdev;
			struct address_space *mapping;
			unsigned bsize;
			gfp_t gfp_mask;
			
			inode = igrab(bdev->bd_inode);
			rd_bdev[unit] = bdev;
			bdev->bd_openers++;
			bsize = bdev_hardsect_size(bdev);
			bdev->bd_block_size = bsize;
			inode->i_blkbits = blksize_bits(bsize);
			inode->i_size = get_capacity(bdev->bd_disk)<<9;
			
			mapping = inode->i_mapping;
			mapping->a_ops = &ramdisk_aops;
			mapping->backing_dev_info = &rd_backing_dev_info;
			bdev->bd_inode_backing_dev_info = &rd_file_backing_dev_info;
			
			gfp_mask = mapping_gfp_mask(mapping);
			gfp_mask &= ~(__GFP_FS | __GFP_IO);
			gfp_mask |= __GFP_HIGH;
			mapping_set_gfp_mask(mapping, gfp_mask);
		}
		
		return 0;
}

static struct block_device_operations rd_bd_op = {
	.owner = THIS_MODULE,
	.open = rd_open,
	.ioctl = rd_ioctl,
};

/*-----------------------------------------------------------*/

static int __init rd_init(void)
{
	int i;
	int err = -ENOMEM;
	
	if(rd_blocksize > PAGE_SIZE || rd_blocksize < 512 || (rd_blocksize & (rd_blocksize -1)))
		{
			printk("RANDISK: wrong blocksize %d, reverting to defaults\n", rd_blocksize);
			rd_blocksize = BLOCK_SIZE;
		}
		
	for(i = 0; i < CONFIG_BLK_DEV_RAM_COUNT; i++)
	{
		rd_disk[i] = alloc_disk(1);
		if(!rd_disks[i])
			goto out;
		
		rd_queue[i] = blk_alloc_queue(GFP_KERNEL);
		if(!rd_queue[i])
			{
				put_disk(rd_disks[i]);
				goto out;
			}
	}
	
	if(register_blkdev(RAMDISK_MAJOR, "ramdisk"))
		{
			err = -EIO;
			goto out;
		}
	
	for(i = 0; i < CONFIG_BLK_DEV_RAM_COUNT; i++)
	{
		struct gendisk *disk = rd_disks[i];
		
		blk_queue_make_request(rd_queue[i], &rd_make_request);
		blk_queue_hardsect_size(rd_queue[i], rd_blocksize);
		
		disk->major = RAMDISK_MAJOR;
		disk->first_minor = i;
		disk->fops = &rd_bd_op;
		disk->queue = rd_queue[i];
		disk->flags = GENHD_FL_SUPPRESS_PARTITION_INFO;
		sprintf(disk->disk_name, "ram%d", i);
		set_capacity(disk, rd_size * 2);						//rd_size is given in kB
		add_disk(rd_disk[i]);
	}
	
	printk("RAMDISK driver initialized: %d RAM disks of %dKB size %d blocksize\n",
				CONFIG_BLK_DEV_RAM_COUNT, rd_size, rd_blocksize);
	
	return 0;
out:
	while(i--)
	{
		put_disk(rd_disks[i]);
		blk_cleanup_queue(rd_queue[i]);
	}
	return err;
}

static void __exit rd_cleanup(void)
{
	int i;
	
	for(i = 0; i < CONFIG_BLK_DEV_RAM_COUNT; i++)
	{
		struct block_device *bdev = rd_bdev[i];
		rd_bdev[i] = NULL;
		if(bdev)
			{
				invalidate_bdev(bdev);
				blkdev_put(bdev);
			}
		del_gendisk(rd_disks[i]);
		put_disk(rd_disks[i]);
		blk_cleanup_queue(rd_queue[i]);
	}
	unregister_blkdev(RAMDISK_MAJOR, "ramdisk");
}

module_init(rd_init);
module_exit(rd_cleanup);

/*-----------------------------------------------------------*/

#ifndef MODULE
#endif

/*-----------------------------------------------------------*/

module_param(rd_size, int, 0);
MODULE_PARM_DESC(rd_size, "Size of each RAM disk in kbytes.");
module_param(rd_blocksize, int, 0);
MODULE_PARM_DESC(rd_blocksize, "Blocksize of each RAM disk in bytes.");

MODULE_ALIAS_BLOCKDEV_MAJOR(RAMDISK_MAJOR);

MODULE_LICENSE("GPL");
