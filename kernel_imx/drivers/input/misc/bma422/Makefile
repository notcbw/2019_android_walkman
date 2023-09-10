#
# Makefile for Bosch sensor driver.
#

obj-y += bs_log.o
ifeq ($(CONFIG_BOSCH_DRIVER_LOG_FUNC),y)
	EXTRA_CFLAGS += -DBOSCH_DRIVER_LOG_FUNC
endif
EXTRA_CFLAGS += -DBMA422
obj-$(CONFIG_SENSORS_BMA4XY)    += bma4xy_driver.o bma4.o bma4xy_i2c.o bma422.o
ifeq ($(CONFIG_BMA4XY_MAG_INTERFACE_SUPPORT),y)
		EXTRA_CFLAGS += -DBMA4XY_MAG_INTERFACE_SUPPORT
endif




