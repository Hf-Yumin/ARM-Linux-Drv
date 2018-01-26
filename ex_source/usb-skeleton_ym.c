
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>

#define USB_SKEL_VENDOR_ID 0xfff0
#define USB_SKEL_PRODUCT_ID 0xfff0

static struct usb_device_id skel_table[] = {
		{ USB_DEVICE(USB_SKEL_VENDOR_ID, USB_SKEL_PRODUCT_ID)},
		{}
};
MODULE_DEVICE_TABLE(usb, skel_table);

static DEFINE_MUTEX(skel_open_lock);

#define USB_SKEL_MINOR_BASE		192
#define MAX_TANSFER						(PAGE_SIZE - 512)
#define WRITES_IN_FLIGHT 			8

struct usb_skel{
	struct usb_device		 	*udev;
	struct usb_interface 	*interface;
	struct semaphore  		*limit_sem;
	unsigned char 				*bulk_in_buffer;
	size_t 								bulk_in_size;
	__u8 									bulk_in_endpointAddr;
	__u8									bulk_out_endpointAddr;
	struct kref						kref;
	struct mutex					io_mutex;
}
#define to_skel_dev(d)		container_of(d, struct usb_skel, kref)

static struct usb_driver skel_driver;

static void skel_delete(struct kref *kref)
{
	struct usb_skel *dev = to_skel_dev(kref);
	
	usb_put_dev(dev->udev);
	kfree(dev->bulk_in_buffer);
	kfree(dev);
}

/*---------------------------------------------------------*/

static int skel_open(struct inode *inode, struct file *file)
{
	struct usb_skel *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;
	
	subminor = iminor(inode);
	
	mutex_lock(&skel_open_lock);
	interface = usb_find_interface(&skel_driver, subminor);
	if(!interface){
		mutex_unlock(&skel_open_lock);
		err("%s - error, can't find device for minor %d", __FUNCTION__, subminor);
		retval = -ENODEV;
		goto exit;
	}
	
	dev = usb_get_intfdata(interface);
	if(!dev){
		mutex_unlock(&skel_open_lock);
		retval = -ENODEV;
		goto exit;
	}
	
	/*increment our usage count for the device*/
	kref_get(&dev->kref);
	/*now we can drop the lock*/
	mutex_unlock(&skel_open_lock);
	
	/*prevent the device from being autosuspended*/
	retval = usb_autopm_get_interface(interface);
	if(retval){
		kref_put(&dev->kref, skel_delete);
		goto exit;
	}
	
	/*save our object in the file's private */
	file->private_data = dev;
	
exit:
	return retval;
}

static int skel_release(struct inode *inode, struct file *file)
{
	struct usb_skel *dev;
	
	dev = (struct usb_skel *)file->private_data;
	if(dev == NULL)
		return -ENODEV;
	
	/*allow the device to be autosuspended*/
	mutex_lock(&dev->io_mutex);
	if(dev->interface)
		usb_autopm_put_interface(dev->interface);
	mutex_unlock(&dev->io_mutex);
	
	/*decrement the count on our device*/
	kref_put(&dev->kref, skel_delete);
	return 0;
}

static size_t skel_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct usb_skel *dev;
	int retval;
	int bytes_read;				/*actual read count*/
	
	dev = (struct usb_skel *)file->private_data;
	
	mutex_lock(&dev->io_mutex);
	if(!dev->interface){			/*disconnect() was called*/
		retval = -ENODEV;
		goto exit;
	}
	
	/*do a blocking bulk read to get data from the device*/
	retval = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr), dev->bulk_in_buffer,
												min(dev->bulk_in_size, count), &bytes_read, 10000);
	/*if the read was successful, copy the data to usersapce*/
	if(!retval){
		if(copy_to_user(buffer, dev->bulk_in_buffer, bytes_read))
			retval = -EFAULT;
		else
			retval = bytes_read;
	}

exit:
	mutex_unlock(&dev->io_mutex);
	return retval;
}

static void skel_write_bulk_callback(struct urb *urb)				//usb_complete
{
	struct usb_skel *dev;
	dev = (struct usb_skel *)urb->context;		//attention!
	
	/*sync/async unlink fault aren't errors*/
	if(urb->status && !(urb->status == -ENOENT || urb->status == -ECONNRESET || urb->status == -ESHUTDOWN)){
		err("%s - nonzero write bulk status received: %d", __FUNCTION__, urb->status);
	}
	
	/*free up our allocate buffer*/
	usb_buffer_free(urb->dev, urb->transfer_buffer_length, urb->transfer_buffer, urb->transfer_dma);
	up(&dev->limit_sem);
}

static size_t skel_write(struct file *file, const char *user_buffer, size_t count, loff_t *ppos)
{
	struct usb_skel *dev;
	int retval = 0;
	struct urb *urb = NULL;
	char *buf = NULL;
	size_t writesize = min(count, (size_t)MAX_TANSFER);				//?MAX_TRANSFER
	
	dev = (struct usb_skel *)file->private_data;
	
	/*verify that we actually have some data to write*/
	if(count == 0)
		goto exit;
	
	/*limit the number of URBs in flight to stop a user from using up all RAM*/
	if(down_interruptible(&dev->limit_sem)){
		retval = -ERESTARTSYS;
		goto exit;
	}
	
	/*create a urb, and a buffer for it, and copy the data to urb*/
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if(!urb){
		retval = -ENOMEM;
		goto error;
	}
	
	buf = usb_buffer_alloc(dev->udev, writesize, GFP_KERNEL, &urb->transfer_dma);
	if(!buf){
		retval = -ENOMEM;
		goto error;
	}
	
	if(copy_from_user(buf, user_buffer, writesize)){
		retval = -EFAULT;
		goto error;
	}
	
	/*this lock makes sure we don't submit URBs to gone device*/
	mutex_lock(&dev->io_mutex);
	if(!dev->interface){			/*disconnect() was called*/
		mutex_unlock(&dev->io_mutex);
		retval = -ENODEV;
		goto error;
	}
	
	/*initialize the urb properly*/
	usb_fill_bulk_urb(urb, dev->udev, usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
										buf, writesize, skel_write_bulk_callback, dev);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	
	/*send the data out the bulk port*/
	retval = usb_submit_urb(urb, GFP_KERNEL);
	mutex_unlock(&dev->io_mutex);
	if(retval){
		err("%s - failed submitting write urb, error %d", __FUNCTION__, retval);
		goto error;
	}
	
	/*release our preference to this urb, the USB core will eventually free it entirely*/
	usb_free_urb(urb);
	
	return writesize;
	
error:
	if(urb){
		usb_buffer_free(dev->udev, writesize, buf, urb->transfer_dma);
		usb_free_urb(urb);
	}
	up(&dev->limit_sem);
	
exit:
	return retval;
}

static const struct file_operations skel_fops = {
	.owner 	= THIS_MODULE,
	.read		=	skel_read,
	.write	=	skel_write,
	.open		=	skel_open,
	.release	=	skel_release,
};

static struct usb_class_driver skel_class = {
	.name	=	"skel%d",
	.fops	=	&skel_fops,
	.minor_base = USB_SKEL_MINOR_BASE,
};

/*---------------------------------------------------------*/

static int skel_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_skel *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int i;
	int retval = -ENOMEM;
	
	/*allocate memory for our device state and initialize it*/
	dev = kzalloc(sizeof(*dev), GPF_KERNEL);
	if(!dev){
		err("Out of memory");
		goto error;
	}
	kref_init(&dev->kref);
	sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);
	mutex_init(&dev->io_mutex);
	
	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;
	
	/*set up the endpoint information*/
	/*use only the first bulk-in and bulk-out endpoint*/
	iface_desc = interface->cur_altsetting;
	for(i = 0; i < iface_desc->desc.bNumEndpoints; ++i){
		endpoint = &iface_desc->endpoint[i].desc;
		
		if(!dev->bulk_in_endpointAddr && usb_endpoint_is_bulk_in(endpoint)){
			/*we found a bulk in endpoint*/
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			dev->bulk_in_size = buffer_size;
			dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
			if(!dev->bulk_in_buffer){
				err("Could not allocate bulk_in_buffer");
				goto error;
			}
		}
		
		if(!dev->bulk_out_endpointAddr && usb_endpoint_is_bulk_out(endpoint)){
			/*we found a bulk out endpoint*/
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}
	
	if(!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)){
		err("Could not find both bulk-in and bulk-out endpoints");
		goto error;
	}
	
	/*save our data pointer in this interface device*/
	usb_set_intfdata(interface, dev);
	
	/*we can register the device now, as it is ready*/
	retval = usb_register_dev(interface, &skel_class);
	if(retval){
		/*something prevented us from registering this driver*/
		err("Not able to get a minor for this device.");
		usb_set_intfdata(interface, NULL);
		goto error;
	}
	info("USB Skeleton device now attached to USBSkel-%d", interface->minor);
	return 0;
	
error:
	if(dev)
		kref_put(&dev->kref, skel_delete);
	return retval;
}

static void skel_disconnect(struct usb_interface *interface)
{
	struct usb_skel *dev;
	int minor = interface->minor;
	
	/*prevent skel_open() from racing skel_disconnect()*/
	mutex_lock(&skel_open_lock);
	
	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	
	/*give back our minor*/
	usb_deregister_dev(interface, &skel_class);
	mutex_unlock(&skel_open_lock);
	
	/*prevent more I/O from starting*/
	mutex_lock(&dev->io_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->io_mutex);
	
	/*decrement our usage count*/
	kref_put(&dev->kref, skel_delete);
	
	info("USB Skeleton #%d now disconnected", minor);
}

static struct usb_driver skel_driver = {
	.name = "skeleton",
	.probe = skel_probe,
	.disconnect = skel_disconnect,
	.id_table = skel_table,
	.supports_autosuspend = 1,
}

/*---------------------------------------------------------*/

static int __init usb_skel_init(void)
{
	int result;
	
	result = usb_register(&skel_driver);
	if(result)
		err("usb_register failed.Error number %d", result);
	
	return result;
}

static void __exit usb_skel_exit(void)
{
	usb_deregister(&skel_driver);
}

module_init(usb_skel_init);
module_exit(usb_skel_exit);
MODULE_LICENSE("GPL");
