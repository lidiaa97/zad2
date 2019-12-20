#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#define BUFF_SIZE 32
MODULE_LICENSE("Dual BSD/GPL");

dev_t my_dev_id;
static struct class *my_class;
static struct device *my_device;
static struct cdev *my_cdev;

DECLARE_WAIT_QUEUE_HEAD(readQ);
DECLARE_WAIT_QUEUE_HEAD(writeQ);
struct semaphore sem;


int fifo[16];
int posRead = 0;
int posWrite = 0;
int hex=1;
int first=0,next=0;
int endRead = 0;

int fifo_open(struct inode *pinode, struct file *pfile);
int fifo_close(struct inode *pinode, struct file *pfile);
ssize_t fifo_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t fifo_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);

struct file_operations my_fops =
{
	.owner = THIS_MODULE,
	.open = fifo_open,
	.read = fifo_read,
	.write = fifo_write,
	.release = fifo_close,
};


char *strtok(char * str, const char * delim)
{
    static char* p=0;
    if(str)
        p=str;
    else if(!p)
        return 0;
    str=p+strspn(p,delim);
    p=str+strcspn(str,delim);
    if(p==str)
        return p=0;
    p = *p ? *p=0,p+1 : 0;
    return str;
}

int fifo_open(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully opened fifo\n");
		return 0;
}

int fifo_close(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully closed fifo\n");
		return 0;
}

ssize_t fifo_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset) 
{
	int ret;
	char buff[BUFF_SIZE+1];
	long int len = 0;
	if (endRead){
		endRead = 0;
		return 0;
	}

	if(down_interruptible(&sem))
		return -ERESTARTSYS;
	while(next == first)
	{
		up(&sem);
		if(wait_event_interruptible(readQ,(next>first)))
			return -ERESTARTSYS;
		if(down_interruptible(&sem))
			return -ERESTARTSYS;
	}


	if(next > first)
	{
		if(hex==1)
		len = scnprintf(buff, BUFF_SIZE, "%X", fifo[first]);
		else
		len = scnprintf(buff, BUFF_SIZE, "%d", fifo[first]);
		ret = copy_to_user(buffer, buff, len);
		if(ret)
			return -EFAULT;
		printk(KERN_INFO "Succesfully read\n");
		if(first!=BUFF_SIZE)
			first++;
		else
			first=0;
		endRead = 1;
	}
	else
	{
			printk(KERN_WARNING "Fifo is empty\n"); 
	}

	up(&sem);
	wake_up_interruptible(&writeQ);

	return len;
}

ssize_t fifo_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset) 
{
	char buff[BUFF_SIZE];
	char buff_cpy[BUFF_SIZE];
	int value;
	int ret;
	int numberOfEl;
	
	ret = copy_from_user(buff, buffer, length);
	if(ret)
		return -EFAULT;
	buff[length-1] = '\0';
	strcpy(buff_cpy,buff);
	
	if(strcmp(buff,"hex")==0)
		hex=1;
	else if(strcmp(buff,"dec")==0)
		hex=0;
	

	if(next>=first)
		numberOfEl=next-first;
	else
		numberOfEl=BUFF_SIZE -next +first;

	if(down_interruptible(&sem))
		return -ERESTARTSYS;
	while(numberOfEl == 16)
	{
		up(&sem);
		if(wait_event_interruptible(writeQ,(numberOfEl<16)))
			return -ERESTARTSYS;
		if(down_interruptible(&sem))
			return -ERESTARTSYS;
	}

	if(numberOfEl<16)
	{	
		char *token;
		token=strtok(buff_cpy,",");
		printk(KERN_INFO "First number: %s", token);
		while(token!=NULL) {
		ret = sscanf(token,"%x",&value);
		
		printk(KERN_INFO "More numbers: %s", token);		
		token=strtok(NULL,",");
		if(ret==1)//one parameter parsed in sscanf
		{
			printk(KERN_INFO "Succesfully wrote value %d", value); 
			fifo[next] = value; 
			if(next!=BUFF_SIZE)
				next++;
			else
				next=0;
		}
		else
		{
			printk(KERN_WARNING "Wrong command format\n");
		}
			
}
	}
	else
	{
		printk(KERN_WARNING "Fifo is full\n"); 
	}

	up(&sem);
	wake_up_interruptible(&readQ);

	return length;
}

static int __init fifo_init(void)
{
   int ret = 0;
	int i=0;
	
	sema_init(&sem,1);

	//Initialize array
	for (i=0; i<16; i++)
		fifo[i] = 0;

   ret = alloc_chrdev_region(&my_dev_id, 0, 1, "fifo");
   if (ret){
      printk(KERN_ERR "failed to register char device\n");
      return ret;
   }
   printk(KERN_INFO "char device region allocated\n");

   my_class = class_create(THIS_MODULE, "fifo_class");
   if (my_class == NULL){
      printk(KERN_ERR "failed to create class\n");
      goto fail_0;
   }
   printk(KERN_INFO "class created\n");
   
   my_device = device_create(my_class, NULL, my_dev_id, NULL, "fifo");
   if (my_device == NULL){
      printk(KERN_ERR "failed to create device\n");
      goto fail_1;
   }
   printk(KERN_INFO "device created\n");

	my_cdev = cdev_alloc();	
	my_cdev->ops = &my_fops;
	my_cdev->owner = THIS_MODULE;
	ret = cdev_add(my_cdev, my_dev_id, 1);
	if (ret)
	{
      printk(KERN_ERR "failed to add cdev\n");
		goto fail_2;
	}
   printk(KERN_INFO "cdev added\n");
   printk(KERN_INFO "Hello world\n");

   return 0;

   fail_2:
      device_destroy(my_class, my_dev_id);
   fail_1:
      class_destroy(my_class);
   fail_0:
      unregister_chrdev_region(my_dev_id, 1);
   return -1;
}

static void __exit fifo_exit(void)
{
   cdev_del(my_cdev);
   device_destroy(my_class, my_dev_id);
   class_destroy(my_class);
   unregister_chrdev_region(my_dev_id,1);
   printk(KERN_INFO "Goodbye, cruel world\n");
}


module_init(fifo_init);
module_exit(fifo_exit);
