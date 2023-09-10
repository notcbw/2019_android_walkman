/*
 * Copyright 2011,2012,2013,2014,2015,2016,2017 Sony Corporation
 */
/*
 * regmon.h
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

#ifndef _REGMON_H_
#define _REGMON_H_

typedef struct {
	char         * name;
	unsigned int   address;
} regmon_reg_info_t;

typedef int (*write_reg_func_t)(
	void *       private_data,
	unsigned int address,
	unsigned int value
);

typedef int (*read_reg_func_t)(
	void *         private_data,
	unsigned int   address,
	unsigned int * value
);

typedef struct {

	/* need to set */
	char                  * name;
	regmon_reg_info_t     * reg_info;
	int                     reg_info_count;
	write_reg_func_t        write_reg;
	read_reg_func_t         read_reg;
	void                  * private_data;

	/* use inside */
	struct proc_dir_entry * proc_directory;
	unsigned int            target_node;
	unsigned int            value_node;
	unsigned int            now_address;

} regmon_customer_info_t;

int regmon_add(regmon_customer_info_t * customer);
int regmon_del(regmon_customer_info_t * customer);

#endif
