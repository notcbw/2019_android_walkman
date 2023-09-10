/*
 * regmon.c
 *
 * Copyright 2011-2017 Sony Corporation.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include "../../fs/proc/internal.h"

#include <misc/regmon.h>

/****************/
/*@ definitions */
/****************/

#define CUSTOMER_MAX 256

#define KERN_REPORT KERN_WARNING

#define minimum(_a,_b) ((_a) < (_b) ? (_a) : (_b))
#define maximum(_a,_b) ((_a) > (_b) ? (_a) : (_b))
#define lower(_c)      ( ( (_c)>='A' && (_c)<='Z' ) ?  ( (_c) + ('a'-'A') ) : (_c) )

/***************/
/*@ prototypes */
/***************/

/* entry_routines */
static int  __init regmon_init(void);
static void __exit regmon_exit(void);

/* target_operations */
static ssize_t regmon_write_target(
	struct file       * file,
	const char __user * buf,
	size_t              size,
	loff_t            * pos
);
static ssize_t regmon_read_target(
	struct file * file,
	char __user * buf,
	size_t        size,
	loff_t      * pos
);

/* value_operations */
static ssize_t regmon_write_value(
	struct file       * file,
	const char __user * buf,
	size_t              size,
	loff_t            * pos
);
static ssize_t regmon_read_value(
	struct file * file,
	char __user * buf,
	size_t        size,
	loff_t      * pos
);

/* basic_routines */
static int chknum(char * string);
static unsigned int strtohex(char * string);
static unsigned int strtodec(char * string);
static int stricmp(char * str1, char * str2);

/**************/
/*@ variables */
/**************/

arch_initcall(regmon_init);
module_exit(regmon_exit);

static struct file_operations target_operations = {
	.write= regmon_write_target,
	.read = regmon_read_target,
};

static struct file_operations value_operations = {
	.write= regmon_write_value,
	.read = regmon_read_value,
};

static struct proc_dir_entry * regmon_directory = NULL;

static regmon_customer_info_t * customer_info[CUSTOMER_MAX];
static int                      customer_count = CUSTOMER_MAX;

/*******************/
/*@ entry_routines */
/*******************/

static int __init regmon_init(void)
{
	memset(customer_info,0,sizeof(customer_info));

	regmon_directory=proc_mkdir("regmon",NULL);
	if(regmon_directory==NULL){
		printk(KERN_ERR "%s(): proc_mkdir() error\n",__FUNCTION__);
		return(-1);
	}

	return(0);
}

static void __exit regmon_exit(void)
{
	if(regmon_directory!=NULL){
		remove_proc_entry("regmon",NULL);
		regmon_directory=NULL;
	}

	return;
}

/*********************/
/*@ service_routines */
/*********************/

int regmon_add(regmon_customer_info_t * customer)
{
	struct proc_dir_entry * entry;
	int index;

	for(index=0; index<customer_count; index++){
		if(customer_info[index]==NULL)
			break;
	}

	if(index>=customer_count){
		printk(KERN_ERR "%s(): no space of customer\n",__FUNCTION__);
		return(-ENOMEM);
	}

	customer->proc_directory=proc_mkdir(customer->name,regmon_directory);
	if(customer->proc_directory==NULL){
		printk(KERN_ERR "%s(): proc_mkdir() error\n",__FUNCTION__);
		return(-ENOMEM);
	}

	entry=proc_create_data(
		"target",
		0600,
		customer->proc_directory,
		&target_operations,
		NULL
	);
	if(entry==NULL){
		printk(KERN_ERR "%s(): create_proc_entry(target) error\n",__FUNCTION__);
		remove_proc_entry(customer->name,regmon_directory);
		return(-ENOMEM);
	}

	customer->target_node = entry->low_ino;

	entry=proc_create_data(
		"value",
		0600,
		customer->proc_directory,
		&value_operations,
		NULL
	);
	if(entry==NULL){
		printk(KERN_ERR "%s(): create_proc_entry(value) error\n",__FUNCTION__);
		remove_proc_entry(customer->name,regmon_directory);
		return(-ENOMEM);
	}

	customer->value_node = entry->low_ino;

	customer_info[index]=customer;

	return(0);
}
EXPORT_SYMBOL(regmon_add);

int regmon_del(regmon_customer_info_t * customer)
{
	int index;

	for(index=0; index<customer_count; index++){
		if(customer_info[index]==customer)
			break;
	}

	if(index>=customer_count){
		printk(KERN_ERR "%s(): no customer\n",__FUNCTION__);
		return(-EINVAL);
	}

	remove_proc_entry("value", customer->proc_directory);
	remove_proc_entry("target", customer->proc_directory);

	remove_proc_entry(customer->name,regmon_directory);

	customer_info[index]=NULL;

	return(0);
}
EXPORT_SYMBOL(regmon_del);

/**********************/
/*@ target_operations */
/**********************/

static ssize_t regmon_write_target(
	struct file       * file,
	const char __user * buf,
	size_t              size,
	loff_t            * pos
)
{
	regmon_customer_info_t * customer;
	char tmp[256];
	char * p;
	int index;
	int count;
	int rv;
	int n;

	for(index=0; index<customer_count; index++){

		if(customer_info[index]==NULL)
			continue;

		if (file->f_path.dentry->d_inode->i_ino
			== customer_info[index]->target_node)
			break;
	}

	if(index>=customer_count){
		printk(
			KERN_ERR
			"%s(): invalid node %d\n",
			__FUNCTION__,
			(int)file->f_path.dentry->d_inode->i_ino
		);
		return(-ENODEV);
	}

	customer=customer_info[index];

	if(*pos!=0){
		*pos=*pos+size;
		return(size);
	}

	count=minimum(255,size);

	rv=copy_from_user((void *)tmp,(void *)buf,count);
	if(rv!=0){
		printk(
			KERN_ERR
			"%s(%s): copy_from_user error\n",
			__FUNCTION__,
			customer->name
		);
		return(-EIO);
	}
	tmp[count]=0;

	p=strrchr(tmp,'\n');
	if(p!=NULL)
		*p=0;

	for(n=0; n<customer->reg_info_count; n++){

		if(stricmp(tmp,customer->reg_info[n].name)==0)
			break;
	}

	if(n<customer->reg_info_count){
		customer->now_address=customer->reg_info[n].address;
	}
	else{

		if(chknum(tmp)<0)
			return(-EINVAL);

		if(tmp[0]=='0' && tmp[1]=='x')
			customer->now_address=strtohex(tmp+2);
		else
			customer->now_address=strtodec(tmp);
	}

	*pos=*pos+size;

	return(size);
}

static ssize_t regmon_read_target(
	struct file * file,
	char __user * buf,
	size_t        size,
	loff_t      * pos
)
{
	int index;

	regmon_customer_info_t * customer;

	int number;
	loff_t total;
	loff_t count;

	char tmp[256];
	loff_t length;
	loff_t start;
	loff_t delta;

	int rv;

	for(index=0; index<customer_count; index++){

		if(customer_info[index]==NULL)
			continue;

		if (file->f_path.dentry->d_inode->i_ino
			== customer_info[index]->target_node)
			break;
	}

	if(index>=customer_count){
		printk(
			KERN_ERR
			"%s(): invalid node %d\n",
			__FUNCTION__,
			(int)file->f_path.dentry->d_inode->i_ino
		);
		return(-ENODEV);
	}

	customer=customer_info[index];

	number=0;
	total=0;
	count=0;

	while(size>0){

		length=12+strlen(customer->reg_info[number].name)+1;

		if(*pos>=total && *pos<total+length){

			snprintf(
				tmp,
				255,
				"0x%08X: %s\n",
				customer->reg_info[number].address,
				customer->reg_info[number].name
			);

			start=*pos-total;

			delta=minimum(size,length-start);

			rv=copy_to_user(
				(void *)(buf+count),
				(void *)(tmp+start),
				delta
			);
			if(rv!=0){
				printk(
					KERN_ERR
					"%s(%s): copy_to_user error\n",
					__FUNCTION__,
					customer->name
				);
				return(-EIO);
			}

			count=count+delta;
			*pos=*pos+delta;
			size=size-delta;
		}

		total=total+length;
		number++;

		if(number>=customer->reg_info_count)
			break;
	}

	return(count);
}

/*********************/
/*@ value_operations */
/*********************/

static ssize_t regmon_write_value(
	struct file       * file,
	const char __user * buf,
	size_t              size,
	loff_t            * pos
)
{
	regmon_customer_info_t * customer;
	char tmp[256];
	char * p;
	int index;
	int count;
	unsigned int value;
	int rv;

	for(index=0; index<customer_count; index++){

		if(customer_info[index]==NULL)
			continue;

		if (file->f_path.dentry->d_inode->i_ino
			== customer_info[index]->value_node)
			break;
	}

	if(index>=customer_count){
		printk(
			KERN_ERR
			"%s(): invalid node %d\n",
			__FUNCTION__,
			(int)file->f_path.dentry->d_inode->i_ino
		);
		return(-ENODEV);
	}

	customer=customer_info[index];

	if(*pos!=0){
		*pos=*pos+size;
		return(size);
	}

	count=minimum(255,size);

	rv=copy_from_user((void *)tmp,(void *)buf,count);
	if(rv!=0){
		printk(
			KERN_ERR
			"%s(%s): copy_from_user error\n",
			__FUNCTION__,
			customer->name
		);
		return(-EIO);
	}
	tmp[count]=0;

	p=strrchr(tmp,'\n');
	if(p!=NULL)
		*p=0;

	if(chknum(tmp)<0)
		return(-EINVAL);

	if(tmp[0]=='0' && tmp[1]=='x')
		value=strtohex(tmp+2);
	else
		value=strtodec(tmp);

	rv=customer->write_reg(
		customer->private_data,
		customer->now_address,
		value
	);
	if(rv<0){
		printk(
			KERN_ERR
			"%s(%s): customer->write_reg() error\n",
			__FUNCTION__,
			customer->name
		);
		return(rv);
	}

	*pos=*pos+size;

	return(size);
}

static ssize_t regmon_read_value(
	struct file * file,
	char __user * buf,
	size_t        size,
	loff_t      * pos
)
{
	regmon_customer_info_t * customer;
	char tmp[256];
	int index;
	int count;
	unsigned int value;
	int rv;

	for(index=0; index<customer_count; index++){

		if(customer_info[index]==NULL)
			continue;

		if (file->f_path.dentry->d_inode->i_ino
			== customer_info[index]->value_node)
			break;
	}

	if(index>=customer_count){
		printk(
			KERN_ERR
			"%s(): invalid node %d\n",
			__FUNCTION__,
			(int)file->f_path.dentry->d_inode->i_ino
		);
		return(-ENODEV);
	}

	customer=customer_info[index];

	if(*pos!=0){
		return(0);
	}

	rv=customer->read_reg(
		customer->private_data,
		customer->now_address,
		&value
	);
	if(rv<0){
		printk(
			KERN_ERR
			"%s(%s): customer->read_reg() error\n",
			__FUNCTION__,
			customer->name
		);
		return(rv);
	}

	snprintf(tmp,255,"0x%08X\n",value);

	count=minimum(11,size);

	rv=copy_to_user((void *)buf,(void *)tmp,count);
	if(rv!=0){
		printk(
			KERN_ERR
			"%s(%s): copy_to_user error\n",
			__FUNCTION__,
			customer->name
		);
		return(-EIO);
	}

	*pos=*pos+count;

	return(count);
}

/*******************/
/*@ basic_routines */
/*******************/

static int chknum(char * string)
{
	while(*string==' ')
		string++;

	if(*string=='0' && *(string+1)=='x'){

		string++;
		string++;

		if(*string==0)
			return(-1);

		while(*string!=0){

			if(strchr("0123456789ABCDEFabcdef",*string)==NULL)
				break;

			string++;
		}
	}

	else{

		if(*string==0)
			return(-1);

		while(*string!=0){

			if(strchr("0123456789",*string)==NULL)
				break;

			string++;
		}
	}

	while(*string!=0){

		if(*string!=' ')
			return(-1);

		string++;
	}

	return(0);
}

static unsigned int strtohex(char * string)
{
	unsigned int ret;
	int ing;

	ret=0;
	ing=0;

	while(*string!=0){

		if(*string==' '){
			if(ing)
				break;
		}
		else if(*string>='0' && *string<='9'){
			ret=ret*0x10+*string-'0';
			ing=1;
		}
		else if(*string>='a' && *string<='f'){
			ret=ret*0x10+*string-'a'+10;
			ing=1;
		}
		else if(*string>='A' && *string<='F'){
			ret=ret*0x10+*string-'A'+10;
			ing=1;
		}
		else{
			break;
		}

		string++;
	}

	return(ret);
}

static unsigned int strtodec(char * string)
{
	unsigned int ret;
	int ing;

	ret=0;
	ing=0;

	while(*string!=0){

		if(*string==' '){
			if(ing)
				break;
		}
		else if(*string>='0' && *string<='9'){
			ret=ret*10+*string-'0';
			ing=1;
		}
		else{
			break;
		}

		string++;
	}

	return(ret);
}

static int stricmp(char * str1, char * str2)
{
	unsigned char c1;
	unsigned char c2;

	while(1){

		c1=*str1;
		c2=*str2;

		if(c1!=c2){
			c1=lower(c1);
			c2=lower(c2);
			if(c1!=c2)
				return(c1 < c2 ? -1 : 1);
		}

		if(c1==0)
			break;

		str1++;
		str2++;
	}

	return(0);
}

