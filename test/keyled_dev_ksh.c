#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>          
#include <linux/errno.h>       
#include <linux/types.h>       
#include <linux/fcntl.h>       
#include <linux/moduleparam.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define   LED_DEV_NAME            "keyled_dev_ksh"
#define   LED_DEV_MAJOR            240      
#define TIME_STEP		timer_val
#define DEBUG 1
#define IMX_GPIO_NR(bank, nr)       (((bank) - 1) * 32 + (nr))

DECLARE_WAIT_QUEUE_HEAD(WaitQueue_Read);

static int timer_val = 0;
static unsigned long ledvalue = 15;
static int sw_irq[8] = {0,};
static char sw_no = 0;

module_param(ledvalue, ulong, 0);
module_param(timer_val, int, 1);

int led[4] = {
	IMX_GPIO_NR(1, 16),   //16
	IMX_GPIO_NR(1, 17),	  //17
	IMX_GPIO_NR(1, 18),   //18
	IMX_GPIO_NR(1, 19),   //19
};
int key[8] = {
	IMX_GPIO_NR(1, 20),   //20
	IMX_GPIO_NR(1, 21),	  //21
	IMX_GPIO_NR(4, 8),    //104
	IMX_GPIO_NR(4, 9),    //105
	IMX_GPIO_NR(4, 5),    //101
	IMX_GPIO_NR(7, 13),	  //205
	IMX_GPIO_NR(1, 7),    //7
	IMX_GPIO_NR(1, 8),    //8
};

typedef struct
{
	struct timer_list timer;
	unsigned long led;
	int timer_val;
} __attribute__ ((packed)) KERNEL_TIMER_MANAGER;
/*
static int ledkey_request(struct file* filp)
{
	int ret = 0;
	int i;
	KERNEL_TIMER_MANAGER* pKTM = (KERNEL_TIMER_MANAGER*)filp->private_data;
	for (i = 0; i < ARRAY_SIZE(led); i++) {
		ret = gpio_request(led[i], "gpio led");
		if(ret<0){
			printk("#### FAILED Request gpio %d. error : %d \n", led[i], ret);
			break;
		} 
		gpio_direction_output(led[i], 0);
	}
	for (i = 0; i < ARRAY_SIZE(key); i++) {
		//		ret = gpio_request(key[i], "gpio key");
		pKTM->sw_irq[i] = gpio_to_irq(key[i]);
		if(ret<0){
			printk("#### FAILED Request gpio %d. error : %d \n", key[i], ret);
			break;
		} 
		//  		gpio_direction_input(key[i]); 
	}
	return ret;
}
*/
static void ledkey_free(struct file * filp)
{
	int i;
	KERNEL_TIMER_MANAGER* pKTM = (KERNEL_TIMER_MANAGER*)filp->private_data;
	for (i = 0; i < ARRAY_SIZE(led); i++){
		gpio_free(led[i]);
	}
	for (i = 0; i < ARRAY_SIZE(key); i++){
		//		gpio_free(key[i]);
		free_irq(pKTM->sw_irq[i], filp->private_data);
	}
}

static void led_write(unsigned char data)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(led); i++){
		gpio_set_value(led[i], (data >> i ) & 0x01);
	}
#if DEBUG
	printk("#### %s, data = %d\n", __FUNCTION__, data);
#endif
}

static void key_read(unsigned char * key_data)
{
	int i;
	char data=0;
	unsigned long temp;
	for(i=0;i<ARRAY_SIZE(key);i++)
	{
		temp = gpio_get_value(key[i]);
		if(temp)
		{
			data = i + 1;
			break;
		}
	}
#if DEBUG
	printk("#### %s, data = %d\n", __FUNCTION__, data);
#endif
	*key_data = data;
	return;
}

int ledkeydev_open (struct inode *inode, struct file *filp)
{
	KERNEL_TIMER_MANAGER * pKTM;
	int num0 = MAJOR(inode->i_rdev); 
	int num1 = MINOR(inode->i_rdev);

	pKTM = (KERNEL_TIMER_MANAGER*)kmalloc(sizeof(KERNEL_TIMER_MANAGER), GFP_KERNEL);
	printk( "ledkeydev open -> major : %d\n", num0 );
	printk( "ledkeydev open -> minor : %d\n", num1 );

	pKTM->sw_no = 0;
	filp->private_data = (void *)pKTM;
	irq_init(filp);

	return 0;
}

ssize_t ledkeydev_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	int ret;
	KERNEL_TIMER_MANAGER * pKTM = (KERNEL_TIMER_MANAGER*)filp->private_data;
#if DEBUG
	printk( "ledkeydev read -> buf : %08X, count : %08X \n", (unsigned int)buf, count );
#endif

	if(!(filp->f_flags & O_NONBLOCK))  //BLOCK Mode
	{
		if(sw_no == 0)
			interruptible_sleep_on(&WaitQueue_Read);
		//		wait_event_interruptible(WaitQueue_Read,sw_no);
		//		wait_event_interruptible_timeout(WaitQueue_Read,sw_no,100); //100: 1/100 * 100 = 1sec
	}

	ret=copy_to_user(buf,&pKTM->sw_no,count);
	pKTM->sw_no = 0;
	if(ret < 0)
		return -ENOMEM;
	return count;
}

ssize_t ledkeydev_write (struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	char kbuf;
	int ret;
#if DEBUG
	printk( "ledkeydev write -> buf : %08X, count : %08X \n", (unsigned int)buf, count );
#endif
	ret=copy_from_user(&kbuf,buf,count);
	if(ret < 0)
		return -ENOMEM;
	led_write(kbuf);
	return count;
}
static void kerneltimer_registertimer(KERNEL_TIMER_MANAGER *pKTM, unsigned long timeover)
{
	init_timer( &(pKTM->timer) );
	pKTM->timer.expires = get_jiffies_64() + timeover;  //10ms *100 = 1sec
	pKTM->timer.data    = (unsigned long) pKTM;
	pKTM->timer.function = kerneltimer_timeover;
	add_timer( &(pdata->timer) );
}
static void kerneltimer_timeover(unsigned long arg)
{
	KERNEL_TIMER_MANAGER* pdata = NULL;
	if( arg )
	{
		pdata = ( KERNEL_TIMER_MANAGER *)arg;
		led_write(pdata->led & 0x0f);
#if DEBUG
		printk("led : %#04x\n",(unsigned int)(pdata->led & 0x0000000f));
#endif
		pdata->led = ~(pdata->led);
		kerneltimer_registertimer( pdata, TIME_STEP);
	}
}
static int kerneltimer_init(void)
{
	led_init();

	printk("timeval : %d , sec : %d , size : %d\n",timeval,timeval/HZ, sizeof(KERNEL_TIMER_MANAGER ));

	ptrmng = (KERNEL_TIMER_MANAGER *)kmalloc( sizeof(KERNEL_TIMER_MANAGER ), GFP_KERNEL);
	if(ptrmng == NULL) return -ENOMEM;
	memset( ptrmng, 0, sizeof( KERNEL_TIMER_MANAGER));
	ptrmng->led = ledval;
	kerneltimer_registertimer( ptrmng, TIME_STEP);
	return 0;
}
static void kerneltimer_exit(void)
{
	if(timer_pending(&(ptrmng->timer)))
		del_timer(&(ptrmng->timer));
	if(ptrmng != NULL)
	{
		kfree(ptrmng);
	}
	led_write(0);
	led_exit();
}

static long ledkey_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	ioctl_test_info ctrl_info = {0,{0}};
	int err, size;
	if( _IOC_TYPE( cmd ) != IOCTLTEST_MAGIC ) return -EINVAL;
	if( _IOC_NR( cmd ) >= IOCTLTEST_MAXNR ) return -EINVAL;

	size = _IOC_SIZE( cmd );
	if( size )
	{
		err = 0;
		if( _IOC_DIR( cmd ) & _IOC_READ )
			err = access_ok( VERIFY_WRITE, (void *) arg, size );
		if( _IOC_DIR( cmd ) & _IOC_WRITE )
			err = access_ok( VERIFY_READ , (void *) arg, size );
		if( !err ) return err;
	}
	ptrmng = filp->private_data;
	switch( cmd )
	{
		case TIMER_START :
			if(!timer_pending(&(ptrmng->timer)))
				kerneltimer_start(filp);
			break;
		case TIMER_STOP :
			if(!timer_pending(&(ptrmng->timer)))
				kerneltimer_stop(filp);
			break;
		case TIMER_VALUE :
			err=copy_from_user(&ctrl_info, (void*)arg,size);
			ptrmng->time_val = ctrl_info.timer_val;
			break;	
	}	
	return 0;
}

static unsigned int ledkeydev_poll(struct file * filp, struct poll_table_struct * wait)
{
	int mask = 0;
	KERNEL_TIMER_MANAGER* pKTM = (KERNEL_TIMER_MANAGER*)filp->private_data;
	printk("_key : %ld \n", (wait->_key & POLLIN));
	if(wait->_key & POLLIN)
		poll_wait(filp, &WaitQueue_Read, wait);
	if(pKTM->sw_no > 0)
		mask = POLLIN;
	return mask;
}

static int ledkeydev_release (struct inode *inode, struct file *filp)
{
	printk( "ledkeydev release \n" );
	led_write(0);
	ledkey_free(filp);
	if(filp->private_data)
		kfree(filp->private_data);
	return 0;
}

struct file_operations ledkeydev_fops =
{
	.owner    = THIS_MODULE,
	.open     = ledkeydev_open,     
	.read     = ledkeydev_read,     
	.write    = ledkeydev_write,    
	.unlocked_ioctl = ledkeydev_ioctl,
	.poll	  = ledkeydev_poll,
	.release  = ledkeydev_release,  
};

irqreturn_t sw_isr(int irq, void *dev_id)
{
	int i;
	sw_no = 0;
	for(i = 0; i < ARRAY_SIZE(key); i++)
	{
		if(irq == sw_irq[i])
		{
			sw_no = i + 1;
			break;
		}
	}
	printk("IRQ : %d, sw_no : %d\n", irq, sw_no);
	wake_up_interruptible(&WaitQueue_Read);
	return IRQ_HANDLED;
}

int ledkeydev_init(struct file * filp)
{
	int result = 0;
	int i;
	char * sw_name[8] = {"key1", "key2", "key3", "key4", "key5", "key6", "key7", "key8"};
	KERNEL_TIMER_MANAGER* pKTM = (KERNEL_TIMER_MANAGER*)filp->private_data;
	printk( "ledkeydev ledkeydev_init \n" );    

	result = register_chrdev( LED_DEV_MAJOR, LED_DEV_NAME, &ledkeydev_fops);
	if (result < 0) return result;

	result = ledkey_request();
	if(result < 0)
	{
		return result;     /* Device or resource busy */
	}
	for(i = 0;i < ARRAY_SIZE(key); i++)
	{
		result = request_irq(sw_irq[i], sw_isr, IRQF_TRIGGER_RISING, sw_name[i], NULL);
		if(result)
		{
			printk("#### FAILED Request irq %d. error : %d \n", sw_irq[i], result);
			break;
		}
	}
	return result;
}

void ledkeydev_exit(void)
{
	led_write(0);
	printk( "ledkeydev ledkeydev_exit \n" );    
	unregister_chrdev( LED_DEV_MAJOR, LED_DEV_NAME );
	ledkey_free();
}

module_init(ledkeydev_init);
module_exit(ledkeydev_exit);
module_init(kerneltimer_init);
module_exit(kerneltimer_exit);

MODULE_DESCRIPTION("test module");
MODULE_LICENSE("Dual BSD/GPL");
