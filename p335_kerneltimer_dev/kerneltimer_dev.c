#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/gpio.h>
#include <linux/moduleparam.h>

#define  KERNELTIMER_DEV_NAME            "ledkeydev"
#define  KERNELTIMER_DEV_MAJOR            240 

#define TIME_STEP	timeval  //KERNEL HZ=100
#define DEBUG 1
#define IMX_GPIO_NR(bank, nr)       (((bank) - 1) * 32 + (nr))
static int timeval = 100;	//f=100HZ, T=1/100 = 10ms, 100*10ms = 1Sec
module_param(timeval,int ,0);  //module_param(매개변수 이름, 유형, 권한)  0 : 읽기/쓰기 가능
static int ledval = 0;
module_param(ledval,int ,0);
typedef struct
{
	struct timer_list timer;
	unsigned long 	  led;
} __attribute__ ((packed)) KERNEL_TIMER_MANAGER;

static KERNEL_TIMER_MANAGER* ptrmng = NULL;
void kerneltimer_timeover(unsigned long arg);

int key[] = {
	IMX_GPIO_NR(1, 20),
	IMX_GPIO_NR(1, 21),
	IMX_GPIO_NR(4, 8),
	IMX_GPIO_NR(4, 9),
	IMX_GPIO_NR(4, 5),
	IMX_GPIO_NR(7, 13),
	IMX_GPIO_NR(1, 7),
	IMX_GPIO_NR(1, 8),
};

int led[] = {
	IMX_GPIO_NR(1, 16),
	IMX_GPIO_NR(1, 17),
	IMX_GPIO_NR(1, 18),
	IMX_GPIO_NR(1, 19),
};

static int kerneltimer_open (struct inode *inode, struct file *filp)
{
	int num0 = MAJOR(inode->i_rdev);  //num0에 inode의 i_rdev 필드의 주번호 저장
	int num1 = MINOR(inode->i_rdev);  //num1에 inode의 i_rdev 필드의 부번호 저장
	printk( "call open -> major : %d\n", num0 );
	printk( "call open -> minor : %d\n", num1 );

	return 0;
}

static void key_read(char * key_data)
{
	int i;
	char data=0;

	for(i=0;i<ARRAY_SIZE(key);i++)
	{   
		if(gpio_get_value(key[i]))
		{
			data = i+1;
			break;
		}
	}   
#if DEBUG
	printk("#### %s, data = %d\n", __FUNCTION__, data);
#endif
	*key_data = data;  //key_data 포인터가 가리키는 메모리 위치에 data 저장
	return;
}

static int led_init(void)
{
	int ret = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(led); i++) {
		ret = gpio_request(led[i], "gpio led");  //led[i]에 해당하는 GPIO핀 요청, "gpio led":GPIO핀 이름
		if(ret){
			printk("#### FAILED Request gpio %d. error : %d \n", led[i], ret);
		} 
		else {
			gpio_direction_output(led[i], 0);  //led[i]에 해당하는 GPIO핀을 출력방향으로 설정, 0 : 초기출력 값
		}
	}
	return ret;
}

static void led_exit(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(led); i++){
		gpio_free(led[i]);
	}
}

void led_write(char data)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(led); i++){
		gpio_set_value(led[i], ((data >> i) & 0x01));  //led[i]의 GPIO핀 값을 data의 i번째 비트 값으로 설정
#if DEBUG
		printk("#### %s, data = %d(%d)\n", __FUNCTION__, data,(data>>i));
#endif
	}
}

ssize_t kerneltimer_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos){
	char kbuf;
	int ret;
	ret = copy_from_user(&kbuf,buf,count);  //buf에서 count크기만큼 데이터를 읽어와 kbuf변수에 복사 -> 오류코드 or 0 반환
	ptrmng->led = kbuf;  //모듈 내부의 ptrmng 구조체에 있는 led 변수에 kbuf 값을 저장
	return count;
}

ssize_t kerneltimer_read(struct file *filp, char *buf, size_t count, loff_t *f_pos){
	char kbuf;
	int ret;
	key_read(&kbuf);  //kbuf 변수에 키 입력 값을 읽어옴
	ret = copy_to_user(buf,&kbuf,count);  //kbuf 변수 값을 count크기만큼 buf로 복사
	return count;
}

void kerneltimer_registertimer(KERNEL_TIMER_MANAGER *pdata, unsigned long timeover)
{
	init_timer( &(pdata->timer) );
	pdata->timer.expires = get_jiffies_64() + timeover;  //10ms *100 = 1sec
	pdata->timer.data	 = (unsigned long)pdata ;
	pdata->timer.function = kerneltimer_timeover;
	add_timer( &(pdata->timer) );
}

void kerneltimer_timeover(unsigned long arg)  //타이머가 만료되면 호출되는 콜백 함수
{
	KERNEL_TIMER_MANAGER* pdata = NULL;
	if( arg )
	{
		pdata = ( KERNEL_TIMER_MANAGER *)arg;  //arg는 pdata->timer.data에 전달된 데이터, pdata는 구조체 포인터롤 형변환
		led_write(pdata->led & 0x0f);  //pdata->led의 하위 4비트만 사용
#if DEBUG
		printk("led : %#04x\n",(unsigned int)(pdata->led & 0x0000000f));
#endif
		pdata->led = ~(pdata->led);
		kerneltimer_registertimer( pdata, TIME_STEP);  //새로운 타이머 등록, TIME_STEP:타이머가 다시 동작하는 시간 간격
	}
}

static int kerneltimer_release (struct inode *inode, struct file *filp)
{
	printk( "Kernel Timer Release \n" );
	return 0;
}

struct file_operations kerneltimer_fops =
{
	.owner		=	THIS_MODULE,
	.write		=	kerneltimer_write,
	.read		=	kerneltimer_read,
	.open		=	kerneltimer_open,
	.release	=	kerneltimer_release,
};

int kerneltimer_init(void)
{
	int result;
	led_init();

	printk("timeval : %d , sec : %d , size : %d\n",timeval,timeval/HZ, sizeof(KERNEL_TIMER_MANAGER ));
	result = register_chrdev(KERNELTIMER_DEV_MAJOR,KERNELTIMER_DEV_NAME, &kerneltimer_fops);
	if(result < 0) return result;

	ptrmng = (KERNEL_TIMER_MANAGER *)kmalloc( sizeof(KERNEL_TIMER_MANAGER ), GFP_KERNEL);
	if(ptrmng == NULL) return -ENOMEM;
	memset( ptrmng, 0, sizeof( KERNEL_TIMER_MANAGER));
	ptrmng->led = ledval;
	kerneltimer_registertimer( ptrmng, TIME_STEP);
	return 0;
}

void kerneltimer_exit(void)
{
	if(timer_pending(&(ptrmng->timer)))
		del_timer(&(ptrmng->timer));
	if(ptrmng != NULL)
	{
		kfree(ptrmng);
	}
	unregister_chrdev(KERNELTIMER_DEV_MAJOR, KERNELTIMER_DEV_NAME);
	led_write(0);
	led_exit();
}
module_init(kerneltimer_init);
module_exit(kerneltimer_exit);
MODULE_LICENSE("Dual BSD/GPL");
