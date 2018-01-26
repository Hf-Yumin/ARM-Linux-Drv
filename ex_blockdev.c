static int __init xxx_init(void)
{
	/*分配gendisk*/
	xxx_disks = alloc_disk(1);
	if(!xxx_disks)
		{
			goto out;
		}
	
	/*注册块设备*/
	if(register_blkdev(XXX_MAJOR, "xxx"))
		{
			err = -EIO;
			goto out;
		}
	
	/*分配请求队列*/
	xxx_queue = blk_alloc_queue(GFP_KERNEL);
	if(!xxx_queue)
		{
			goto out_queue;
		}
	/*绑定“请求队列”和“制造请求函数”*/
	blk_queue_make_request(xxx_queue, &xxx_make_request);
	/*硬件扇区尺寸设置*/
	blk_queue_hardsect_size(xxx_queue, xxx_blocksize);
	
	/*gendisk初始化*/
	xxx_disks->major = XXX_MAJOR;
	xxx_disks->first_minor = 0;
	xxx_disks->fops = &xxx_op;
	xxx_disks->queue = xxx_queue;
	sprintf(xxx_disks->disk_name, "xxx%d", i);
	set_capacity(xxx_disks, xxx_size);	//xxx_size以512B为单位
	add_disk(xxx_disks);								//添加gendisk
	
	return 0;
	
out_queue:
	unregister_blkdev(XXX_MAJOR, "xxx");
out:
	put_disk(xxx_disks);
	blk_cleanup_queue(xxx_queue);
	
	return -ENOMEM;
}

static int __init xxx_init(void)
{
	xxx_disks = alloc_disk(1);
	if(!xxx_disks)
		{
			goto out;
		}
	
	if(register_blkdev(XXX_MAJOR, "xxx"))
		{
			err = -EIO;
			goto out;
		}
	
	xxx_queue = blk_init_queue(xxx_request, xxx_lock);		//请求队列初始化
	if(!xxx_queue)
		{
			goto out_queue;
		}
	
	blk_queue_hardsect_size(xxx_queue, xxx_blocksize);
	
	xxx_disk->major = XXX_MAJOR;
	xxx_disk->first_minor = 0;
	xxx_disk->fops = &xxx_op;
	xxx_disk->queue = xxx_queue;
	set_capacity(xxx_disks, xxx_size);
	add_disk(xxx_disks);
	
	return 0;
	
out_queue:
	unregister_blkdev(XXX_MAJOR, "xxx");
out:
	put_disk(xxx_disks);
	blk_cleanup_queue(xxx_queue);
	return -ENOMEM;
}

static void __exit xxx_exit(void)
{
	if(bdev)
		{
			invalidate_bdev(xxx_bdev, 1);
			blkdev_put(xxx_bdev);
		}
	del_gendisk(xxx_disks);
	put_disk(xxx_disks);
	blk_cleanup_queue(xxx_queue[i]);
	unregister_blkdev(XXX_MAJOR, "xxx");
}

/*-----------------------------------------------------------*/
int xxx_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	long size;
	struct hd_geometry geo;
	struct xxx_dev *dev = filp->private_data;
	
	switch(cmd)
	{
		case HDIO_GETGEO:
			size = dev->size * (hardsect_size / KERNEL_SECTOR_SIZE);
			geo.cylinders = (size & ~0x3f) >> 6;
			geo.heads = 4;
			geo.sectors = 16;
			geo.start = 4;
			if(copy_to_user(void __user *)arg, &geo, sizeof(geo))
				{
					return -EFAULT;
				}
			return 0;
	}
	
	return -ENOTTY;
}

/*-----------------------------------------------------------*/
static void xxx_request(request_queue_t *q)
{
	struct request *req;
	while((req = elv_next_request(q)) != NULL)
	{
		struct xxx_dev *dev = req->rq_disk->private_data;
		if(!blk_fs_request(req))
			{
				printk(KERN_NOTICE "Skip non-fs request\n");
				end_request(req, 0);
				continue;						//不是文件系统请求，跳出本次循环执行下一次
			}
		xxx_transfer(dev, req->sector, req->current_nr_sectors, req->buffer, rq_data_dir(req));
		end_request(req, 1);
	}
}

/*-----------------------------------------------------------*/
static void xxx_transfer(struct xxx_dev *dev, unsigned long sector, unsigned long nsect, char *buffer, int write)
{
	unsigned long offset = sector * KERNEL_SECTOR_SIZE;
	unsigned long nbytes = nsect * KERNEL_SECTOR_SIZE;
	if((offset + nbytes) > dev->size)
		{
			printk(KERNEL_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
			return;
		}
	if(write)
		{
			write_dev(offset, buffer, nbytes);
		}
	else
		{
			read_dev(offset, buffer, nbytes);
		}
}

void end_request(struct request *req, int uptodate)
{
	if(!end_that_request_first(req, uptodate, req->hard_cur_sectors)
		{
			add_disk_randomness(req->rq_disk);
			blkdev_derequeue_request(req);
			end_that_requeue_last(req);
		}
}

/*-----------------------------------------------------------*/
static void xxx_full_request(request_queue_t *q)
{
	struct request *request;
	int sectors_xferred;
	struct xxx_dev *dev = q->queuedata;
	/*遍历每个请求*/
	while((req = elv_next_request(q)) != NULL)
	{
		if(!blk_fs_request(req))
			{
				printk(KERN_NOTICE "Skip non-fs request\n");
				end_request(req, 0);
				continue;
			}
		sectors_xferred = xxx_xfer_request(dev, req);
		if(!end_that_request_first(req, 1, sectors_xferred))
			{
				blkdev_dequeue_request(req);
				end_that_request_last(req);
			}
	}
}

/*处理请求*/
static int xxx_xfer_request(struct xxx_dev *dev, struct request *req)
{
	struct bio *bio;
	int nsect = 0;
	/*遍历请求中的每个bio*/
	rq_for_each_bio(bio, req)
	{
		xxx_xfer_bio(dev, bio);
		nsect += bio->bio_size / KERN_SECTOR_SIZE;
	}
	return nsect;
}

/*处理bio*/
static int xxx_xfer_bio(struct xxx_dev *dev, struct bio *bio)
{
	int i;
	struct bio_vec *bvec;
	sector_t sector = bio->bi-sector;
	
	/*遍历每一段*/
	bio_for_each_segment(bvec,bio, i)
	{
		char *buffer = __bio_kmap_atomic(bio, i, KM_USER0);
		xxx_fransfer(dev, sector, bio_cur_sectors(bio), buffer, bio_data_dir(bio) == WRITE);
		sector += bio_cur_sectors(bio);
		__bio_kunmap_atomic(bio, KM_USER0);
	}
	return 0;
}

/*-----------------------------------------------------------*/
static int xxx_make_request(request_queue_t *q, struct bio *bio)
{
	struct xxx_dev *dev = q->queuedata;
	int status;
	status = xxx_xfer_bio(dev, bio);
	bio_endbio(bio, bio->bi_size, status);
	return 0;
}
