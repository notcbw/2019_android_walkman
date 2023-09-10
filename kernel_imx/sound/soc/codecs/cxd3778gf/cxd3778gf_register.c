/*
 * Copyright 2013,2014,2015,2016,2017,2018 Sony Corporation
 */
/*
 * cxd3778gf_register.c
 *
 * CXD3778GF CODEC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* #define TRACE_PRINT_ON */
/* #define DEBUG_PRINT_ON */
#define TRACE_TAG "------- "
#define DEBUG_TAG "        "

#include <linux/dma-mapping.h>
#include "cxd3778gf_common.h"

static int cxd3778gf_register_write_safe(
	unsigned int    address,
	unsigned char * value,
	int             size
);

static int cxd3778gf_register_read_safe(
	unsigned int    address,
	unsigned char * value,
	int             size
);

static int cxd3778gf_register_write_core(
	unsigned int    address,
	unsigned char * value,
	int             size
);

static int cxd3778gf_register_read_core(
	unsigned int    address,
	unsigned char * value,
	int             size
);

static int initialized = FALSE;
static struct i2c_client * i2c_client = NULL;
static unsigned int i2c_timing;
static struct mutex access_mutex;
static struct i2c_client *ext_i2c_client;
static bool use_ext_bus;

int cxd3778gf_register_initialize(struct i2c_client * client)
{

	print_trace("%s()\n",__FUNCTION__);

	mutex_init(&access_mutex);

	i2c_client=client;
	i2c_timing = 400;

	initialized=TRUE;

	mutex_lock(&access_mutex);
	use_ext_bus = FALSE;
	mutex_unlock(&access_mutex);

	return(0);
}

int cxd3778gf_ext_register_initialize(struct i2c_client *client)
{
	pr_debug("%s()\n", __func__);

	ext_i2c_client = client;

	return 0;
}

int cxd3778gf_register_use_ext_bus(bool enable)
{
	int ret = 0;
	static unsigned int users;

	pr_debug("%s: switch bus from %d to %d\n",
		    __func__, use_ext_bus, enable);

	mutex_lock(&access_mutex);
	if (NULL == ext_i2c_client) {
		pr_warn("%s: external i2c client not registered\n", __func__);
		use_ext_bus = false;
		ret = -ENODEV;
		goto out;
	}

	if (enable) {
		if (users++) {
			pr_debug("%s: switch bus canceled, user(%d)\n",
						__func__, users);
			goto out;
		}
	} else {
		if (--users) {
			pr_debug("%s: switch bus canceled, user(%d)\n",
						__func__, users);
			goto out;
		}
	}

	cxd3778gf_ext_enable_i2c_bus(enable);
	use_ext_bus = enable;

out:
	mutex_unlock(&access_mutex);

	return ret;
}

int cxd3778gf_register_finalize(void)
{
	print_trace("%s()\n",__FUNCTION__);

	if(!initialized)
		return(0);

	initialized=FALSE;

	return(0);
}

int cxd3778gf_ext_register_finalize(void)
{
	pr_debug("%s()\n", __func__);

	ext_i2c_client = NULL;

	return 0;
}

int cxd3778gf_register_write_multiple(
	unsigned int    address,
	unsigned char * value,
	int             size
)
{
	int rv;

	print_trace("%s(0x%02X,0x%02X...,%d)\n",__FUNCTION__,address,value[0],size);

	mutex_lock(&access_mutex);

	rv=cxd3778gf_register_write_safe(address,value,size);
	if(rv<0) {
		back_trace();
		mutex_unlock(&access_mutex);
		return(rv);
	}

#ifdef DEBUG_PRINT_ON
	{
		int n;

		for(n=0;n<size;n++){

			if(n==0)
				printk(KERN_INFO DEBUG_TAG "W %02X :",address);
			else if((n&0xF)==0)
				printk(KERN_INFO DEBUG_TAG "     :");

			printk(" %02X",value[n]);

			if((n&0xF)==0xF)
				printk("\n");
		}

		if((n&0xF)!=0x0)
			printk("\n");
	}
#endif

	mutex_unlock(&access_mutex);

	return(0);
}

int cxd3778gf_register_read_multiple(
	unsigned int    address,
	unsigned char * value,
	int             size
)
{
	int rv;

	print_trace("%s(0x%02X,%d)\n",__FUNCTION__,address,size);

	mutex_lock(&access_mutex);

	rv=cxd3778gf_register_read_safe(address,value,size);
	if(rv<0) {
		back_trace();
		mutex_unlock(&access_mutex);
		return(rv);
	}

#ifdef DEBUG_PRINT_ON
	{
		int n;

		for(n=0;n<size;n++){

			if(n==0)
				printk(KERN_INFO DEBUG_TAG "R %02X :",address);
			else if((n&0xF)==0)
				printk(KERN_INFO DEBUG_TAG "     :");

			printk(" %02X",value[n]);

			if((n&0xF)==0xF)
				printk("\n");
		}

		if((n&0xF)!=0x0)
			printk("\n");
	}
#endif

	mutex_unlock(&access_mutex);

	return(0);
}

int cxd3778gf_register_modify(
	unsigned int address,
	unsigned int value,
	unsigned int mask
)
{
	unsigned int old;
	unsigned int now;
	unsigned char uc;
	int rv;

	print_trace("%s(0x%02X,0x%02X,0x%02X)\n",__FUNCTION__,address,value,mask);

	mutex_lock(&access_mutex);

	rv=cxd3778gf_register_read_safe(address,&uc,1);
	if(rv<0) {
		back_trace();
		mutex_unlock(&access_mutex);
		return(rv);
	}

	old=uc;

	now=(old&~mask)|(value&mask);

	uc=now;

	rv=cxd3778gf_register_write_safe(address,&uc,1);
	if(rv<0) {
		back_trace();
		mutex_unlock(&access_mutex);
		return(rv);
	}

	print_debug("M %02X : %02X -> %02X\n",address,old,now);

	mutex_unlock(&access_mutex);

	return(0);
}

int cxd3778gf_register_write(
	unsigned int address,
	unsigned int value
)
{
	unsigned char uc;
	int rv;

	print_trace("%s(0x%02X,0x%02X)\n",__FUNCTION__,address,value);

	mutex_lock(&access_mutex);

	uc=value;

	rv=cxd3778gf_register_write_safe(address,&uc,1);
	if(rv<0) {
		back_trace();
		mutex_unlock(&access_mutex);
		return(rv);
	}

	print_debug("W %02X : %02X\n",address,value);

	mutex_unlock(&access_mutex);

	return(0);
}

int cxd3778gf_register_read(
	unsigned int   address,
	unsigned int * value
)
{
	unsigned char uc;
	int rv;

	print_trace("%s(0x%02X)\n",__FUNCTION__,address);

	mutex_lock(&access_mutex);

	rv=cxd3778gf_register_read_safe(address,&uc,1);
	if(rv<0) {
		back_trace();
		mutex_unlock(&access_mutex);
		return(rv);
	}

	*value=uc;

	print_debug("R %02X : %02X\n",address,*value);

	mutex_unlock(&access_mutex);

	return(0);
}

static int cxd3778gf_register_write_safe(
	unsigned int    address,
	unsigned char * value,
	int             size
)
{
	int retry;
	int rv;

	retry=REGISTER_ACCESS_RETRY_COUNT;

	while(retry>=0){
		rv=cxd3778gf_register_write_core(address,value,size);
		if(rv==0)
			break;

		retry--;
	}

	return(rv);
}

static int cxd3778gf_register_read_safe(
	unsigned int    address,
	unsigned char * value,
	int             size
)
{
	int retry;
	int rv;

	retry=REGISTER_ACCESS_RETRY_COUNT;

	while(retry>=0){
		rv=cxd3778gf_register_read_core(address,value,size);
		if(rv==0)
			break;

		retry--;
	}

	return(rv);
}

static int cxd3778gf_register_write_core(
	unsigned int    address,
	unsigned char * value,
	int             size
)
{
	struct i2c_msg msg;
	struct i2c_client *client;
	int rv;

	if (use_ext_bus) {
		if (1 == size) {
			client = ext_i2c_client;
		} else {
			cxd3778gf_ext_enable_i2c_bus(0);
			client = i2c_client;
		}
	} else {
		client = i2c_client;
	}

#ifdef CONFIG_MTK_I2C
	u8 *buffer =NULL;
	u32 phyAddr = 0;
#else
	unsigned char buf[512];
#endif

	if (client == NULL) {
		print_error("not initialized.\n");
		return(-ENODEV);
	}

	if(size>320){
		print_error("invalid size.\n");
		return(-EINVAL);
	}

	msg.addr  = client->addr;
	msg.flags = 0;
	msg.len   = size+1;
#ifdef CONFIG_MTK_I2C
	buffer = dma_alloc_coherent(0, size, &phyAddr, GFP_KERNEL);
	buffer[0]=(unsigned char)address;
	memcpy(buffer+1,value,size);

	msg.buf   = (u8*)phyAddr;
	msg.ext_flag = I2C_DMA_FLAG;
	msg.timing = i2c_timing;
#else
	buf[0]=(unsigned char)address;
	memcpy(buf+1,value,size);
	msg.buf   = buf;
#endif
	rv = i2c_transfer(client->adapter, &msg, 1);
	if(rv<0) {
		print_fail("i2c_transfer(): code %d error occurred.\n",rv);
		back_trace();
#ifdef CONFIG_MTK_I2C
		dma_free_coherent(0, size, buffer, phyAddr);
#endif
		return(rv);
	}

	if(rv!=1){
		print_error("count mismacth.\n");
#ifdef CONFIG_MTK_I2C
		dma_free_coherent(0, size, buffer, phyAddr);
#endif
		return(-EIO);
	}

#ifdef CONFIG_MTK_I2C
	dma_free_coherent(0, size, buffer, phyAddr);
#endif

	if (use_ext_bus) {
		if (1 == size)
			usleep_range(CXD3778GF_EXT_WAIT_TIME,
				CXD3778GF_EXT_WAIT_TIME + 100);
		else
			cxd3778gf_ext_enable_i2c_bus(1);
	}

	return(0);
}

static int cxd3778gf_register_read_core(
	unsigned int    address,
	unsigned char * value,
	int             size
)
{
	struct i2c_msg msg[2];
	unsigned char addr;
	struct i2c_client *client;
	int rv;
	unsigned char uc;

	if (use_ext_bus) {
		if (1 == size) {
			client = ext_i2c_client;
		} else {
			cxd3778gf_ext_enable_i2c_bus(0);
			client = i2c_client;
		}
	} else {
		client = i2c_client;
	}

#ifdef CONFIG_MTK_I2C
	u32 phyAddr = 0;
	u8 *buffer =NULL;
#endif

	if (client == NULL) {
		print_error("not initialized.\n");
		return(-ENODEV);
	}

	if (use_ext_bus && 1 == size) {
		uc = address;
		rv = cxd3778gf_register_write_core(0xff, &uc, 1);
		if (rv) {
			pr_err("%s: write reg for read reg failed\n", __func__);
			return rv;
		}
	}

	if(size>320){
		print_error("invalid size.\n");
		return(-EINVAL);
	}

	addr=(unsigned char)address;

	msg[0].addr  = client->addr;
	msg[0].flags = 0;
	msg[0].len   = 1;
	msg[0].buf   = &addr;
#ifdef CONFIG_MTK_I2C
	msg[0].ext_flag   = 0;
	msg[0].timing = i2c_timing;
#endif
	msg[1].addr  = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = size;
#ifdef CONFIG_MTK_I2C
	buffer = dma_alloc_coherent(0, size, &phyAddr, GFP_KERNEL);
	msg[1].buf   = (u8 *)phyAddr;
	msg[1].ext_flag = I2C_DMA_FLAG;
	msg[1].timing = i2c_timing;
#else
	msg[1].buf   = value;
#endif
	rv = i2c_transfer(client->adapter, msg, 2);
	if(rv<0) {
		print_fail("i2c_transfer(): code %d error occurred.\n",rv);
		back_trace();
#ifdef CONFIG_MTK_I2C
		dma_free_coherent(0, size, buffer, phyAddr);
#endif
		return(rv);
	}

	if(rv!=2){
		print_error("count mismacth.\n");
#ifdef CONFIG_MTK_I2C
		dma_free_coherent(0, size, buffer, phyAddr);
#endif
		return(-EIO);
	}

#ifdef CONFIG_MTK_I2C
	memcpy(value, buffer, size);
	dma_free_coherent(0, size, buffer, phyAddr);
#endif

	if (use_ext_bus) {
		if (1 == size)
			usleep_range(CXD3778GF_EXT_WAIT_TIME,
				CXD3778GF_EXT_WAIT_TIME + 100);
		else
			cxd3778gf_ext_enable_i2c_bus(1);
	}

	return(0);
}

