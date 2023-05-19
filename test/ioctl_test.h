#ifndef __IOCTL_H__
#define __IOCTL_H__

#define MAGIC 't'
typedef struct
{
	unsigned int timer_val;
} __attribute__((packed)) ioctl_test_info;

#define TIMER_VALUE				_IOW(TIMER_MAGIC, 0,keyled_data)
#define TIMER_START	 			_IO(TIMER_MAGIC, 1) 
#define TIMER_STOP			 	_IO(TIMER_MAGIC, 2)
#define TIMER_MAXNR				3
#endif
