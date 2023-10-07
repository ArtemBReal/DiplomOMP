#include "libsDriver.h"

static int __init CharDriverInit(void)
{
    //выделяем major
    if((alloc_chrdev_region(&device,0,1,"CharDevice")) < 0)
	{
        pr_alert("Cannot allocate the major number..\n");
        return -1;
    }

    pr_alert("Major = %d , Minor = %d..\n",MAJOR(device),MINOR(device));

    //Создаем структуру CharDevice
    cdev_init(&CharDevice,&fops);
	
    //Добавляем символьное устройство в систему
    if((cdev_add(&CharDevice,device,1)) < 0)
	{
        pr_alert("Cannot add the device to the system...\n");
        goto r_class;
    }
	
    //Создаем структуру класса
    if((DeviceClass = class_create(THIS_MODULE,"my_class")) == NULL)
	{
        pr_alert("cannot create struct class...\n");
        goto r_class;
    }
	
    //Создаем устройство
    if((device_create(DeviceClass,NULL,device,NULL,"my_device"))== NULL)
	{
        pr_alert("cannot create the device..\n");
        goto r_device;
    }

	//Выделяем память под буфер
    if((ioBuffer = kmalloc(maxMessageLength, GFP_KERNEL)) == 0)
	{
        pr_alert("cannot allocate memory in kernel\n");
        return -1;
    }
	bufferStart = buffer; // указатель на начало буфера
	bufferEnd = buffer + bufferSize - 1; // указатель на конец буфера
	currentPosition = bufferStart; // указатель на текущую позицию в буфере
	
	dataAvailable = false;
	
    pr_alert("Device driver insert...done properly...\n");
    return 0;

r_device:
    class_destroy(DeviceClass);

r_class:
        unregister_chrdev_region(device,1);
        return -1;
}

static void __exit CharDriverExit(void)
{
	kfree(ioBuffer);
    device_destroy(DeviceClass,device);
    class_destroy(DeviceClass);
    cdev_del(&CharDevice);
    unregister_chrdev_region(device,1);
    pr_alert("Device driver is removed successfully..\n");
}

static int CharDriverOpen(struct inode *inode, struct file *file)
{
	driverOpenCounter++;
    pr_alert("Device file opened...!!!!\n");
    return 0;
}

static int CharDriverRelease(struct inode *inode , struct file *file)
{
    pr_alert("Device file closed.....!!!\n");
	driverOpenCounter--;
    return 0;
}

static ssize_t CharDriverRead(struct file *filp, char __user *userBuffer, size_t messageLength, loff_t *off)
{
	int tempBufferLenght = 0;
	if (!lastLap)
	{
		tempBufferLenght = bufferSize;
	}
	else
	{
		tempBufferLenght = remainder;
	}
	char tempBuffer[tempBufferLenght];
	currentPosition = bufferStart;
	for(int i = 0; i < tempBufferLenght ; ++i)
	{
		tempBuffer[i] = *currentPosition;
		currentPosition++;
	}
	currentPosition = bufferStart;
    pr_alert("Reading to %c\n", &userBuffer);
    //char localBuffer[1024] = {"Hello World"};
    pr_alert("Sending message: %s\n", tempBuffer);
    int lostBytes = copy_to_user(userBuffer, tempBuffer, tempBufferLenght);
    pr_alert("Lost bytes %d\n",lostBytes);

    return messageLength;
}

static ssize_t CharDriverWrite(struct file *filp, const char __user *userBuffer,size_t maxLength, loff_t *off)
{
    pr_alert("Writing");
    copy_from_user(&(data.message), userBuffer, maxLength);
    pr_alert("AFTER COPY FROM USER");
	
    int realWriteMessageLength = 0;
	
	//считаем настоящую длину сообщения
    char *ptr;
    ptr = data.message;
	pr_alert("CONTINUED WRITING");
    for(int i = 0; i <= maxLength ; ++i)
	{
		realWriteMessageLength++;
        ++ptr;
        if(*ptr == '\0') break;	
    }
	realMessageLength = realWriteMessageLength;
    /*		
    if((ioBuffer = krealloc(ioBuffer, (realWriteMessageLength + 1)*sizeof(char), GFP_KERNEL))== 0)
	{
     	pr_alert("cannot allocate memory in kernel\n");
        return -1;
    }
    */   	
    ptr = data.message;
    for(int i = 0; i < realWriteMessageLength; ++i)
    {
		if (currentPosition > bufferEnd)
		{
			currentPosition = bufferStart;
			WRITE_ONCE(dataAvailable,true);	
			wake_up_interruptible(&wait_q);
			pr_alert("Writer sleeping\n");
        	wait_event_interruptible(wait_q, (dataAvailable == false));
			pr_alert("Writer is back!\n");
			pr_alert("AAAAAA %d\n", i + 1);
			*currentPosition = *ptr;
			currentPosition++;
    		ptr++;
		}
		else 
		{
		pr_alert("%d\n", i + 1);
		*currentPosition = *ptr;
		currentPosition++;
    	ptr++;
		}
    }
    WRITE_ONCE(dataAvailable,true);	
	wake_up_interruptible(&wait_q);
	
    //pr_alert("Message: %s\n", ioBuffer);
    pr_alert("Message length = %d",realWriteMessageLength);
    
    return realWriteMessageLength ;
}

static long CharDriverIoctl(struct file *filp, unsigned int cmd, unsigned long address)
{
	pr_alert("IOCTL\n");
	
    switch(cmd)
	{
    	case WR_DATA:
    		//writersCounter++;
        	realMessageLength = CharDriverWrite(filp, (char*)address, sizeof(data), NULL); 
			//WRITE_ONCE(dataAvailable,true);	
			//wake_up_interruptible(&wait_q);
        	
        	//writersCounter--; 
        break;
			
        case RD_DATA:
        	//readersCounter++;
			lastLap = false;
        	if (dataAvailable == false)
        	{
        		pr_alert("Reader sleeping\n");
        		wait_event_interruptible(wait_q, (dataAvailable == true));	//ждём, когда можно будет читать		
        	}
			int i = 0;
			if(realMessageLength % bufferSize)
			{
				i = (realMessageLength / bufferSize) + 1;
				remainder = realMessageLength % bufferSize;
			}
			else i = realMessageLength / bufferSize;
			while(i > 0)
			{
				if (i == 1) lastLap = true;
				pr_alert("Reader is going to read %d bytes\n", bufferSize);
				CharDriverRead(filp, (char*)address, (size_t)(bufferSize*sizeof(char)),NULL);
				WRITE_ONCE(dataAvailable,false);	
				wake_up_interruptible(&wait_q);
				if (i > 1) wait_event_interruptible(wait_q, (dataAvailable == true));
				address += bufferSize;
				i--;
			}
        	
			//readersCounter--;
        break;
    }
    return 0;
}

module_init(CharDriverInit); 
module_exit(CharDriverExit); 

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Borisov, Voropanov, Podlesnyy");
MODULE_DESCRIPTION("Pipe device driver");
