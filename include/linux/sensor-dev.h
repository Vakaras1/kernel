/* include/linux/sensor-dev.h - sensor header file
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: luowei <lw@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/miscdevice.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <dt-bindings/sensor-dev.h>

#define SENSOR_ON		1
#define SENSOR_OFF		0
#define SENSOR_UNKNOW_DATA	-1

#define GPIO_HIGH 1
#define GPIO_LOW 0

#define MAX_NUM                 10
#define GYRO_FIFO_SIZE		1024 /* 1024 */

enum sensor_id {
	ID_INVALID = 0,

	ANGLE_ID_ALL,
	ANGLE_ID_KXTIK,
	ANGLE_ID_LIS3DH,

	ACCEL_ID_ALL,
	ACCEL_ID_LIS331,
	ACCEL_ID_LSM303DLX,
	ACCEL_ID_LIS3DH,
	ACCEL_ID_KXSD9,
	ACCEL_ID_KXTF9,
	ACCEL_ID_KXTIK,
	ACCEL_ID_KXTJ9,
	ACCEL_ID_BMA150,
	ACCEL_ID_BMA222,
	ACCEL_ID_BMA250,
	ACCEL_ID_ADXL34X,
	ACCEL_ID_MMA8450,
	ACCEL_ID_MMA845X,
	ACCEL_ID_MMA7660,
	ACCEL_ID_MPU6050,
	ACCEL_ID_MXC6225,
	ACCEL_ID_DMARD10,
	ACCEL_ID_LSM303D,
	ACCEL_ID_MC3230,
	ACCEL_ID_MPU6880,
	ACCEL_ID_MPU6500,
	ACCEL_ID_BMA2XX,
	ACCEL_ID_MIR3DA,
	ACCEL_ID_LSM6DS3,
	COMPASS_ID_ALL,
	COMPASS_ID_AK8975,
	COMPASS_ID_AK8963,
	COMPASS_ID_AK09911,
	COMPASS_ID_AK8972,
	COMPASS_ID_AMI30X,
	COMPASS_ID_AMI306,
	COMPASS_ID_YAS529,
	COMPASS_ID_YAS530,
	COMPASS_ID_HMC5883,
	COMPASS_ID_LSM303DLH,
	COMPASS_ID_LSM303DLM,
	COMPASS_ID_MMC314X,
	COMPASS_ID_HSCDTD002B,
	COMPASS_ID_HSCDTD004A,

	GYRO_ID_ALL,
	GYRO_ID_L3G4200D,
	GYRO_ID_L3G20D,
	GYRO_ID_EWTSA,
	GYRO_ID_K3G,
	GYRO_ID_MPU6880,
	GYRO_ID_MPU6500,
	GYRO_ID_LSM6DS3,
	LIGHT_ID_ALL,
	LIGHT_ID_CM3217,
	LIGHT_ID_CM3218,
	LIGHT_ID_CM3232,
	LIGHT_ID_AL3006,
	LIGHT_ID_STK3171,
	LIGHT_ID_ISL29023,
	LIGHT_ID_AP321XX,
	LIGHT_ID_PHOTORESISTOR,
	LIGHT_ID_US5152,
	LIGHT_ID_STK3410,

	PROXIMITY_ID_ALL,
	PROXIMITY_ID_AL3006,
	PROXIMITY_ID_STK3171,
	PROXIMITY_ID_AP321XX,
	PROXIMITY_ID_STK3410,

	TEMPERATURE_ID_ALL,
	TEMPERATURE_ID_MS5607,

	PRESSURE_ID_ALL,
	PRESSURE_ID_BMA085,
	PRESSURE_ID_MS5607,

	HALL_ID_ALL,
	HALL_ID_OCH165T,

	SENSOR_NUM_ID,
};

struct sensor_axis {
	int x;
	int y;
	int z;
};

typedef struct raw_data {
	struct sensor_axis gyro_axis;
	struct sensor_axis accel_axis;
	long long timestamp_usec;
	short temperature;
	long quaternion[4];
	int quat_flag;
	int use_irq;
} raw_data_t;

typedef struct raw_data_set {
	raw_data_t sensordata[200];
	int avail_num;
} raw_data_set_t;

typedef struct sensor_axisdata_fifo {
	unsigned char *sensordata;
	int data_len;
	long long timestamp;
	long long timestamp_interval;
} sensor_axisdata_fifo_t;

struct sensor_flag {
	atomic_t a_flag;
	atomic_t m_flag;
	atomic_t mv_flag;
	atomic_t open_flag;
	atomic_t debug_flag;
	long long delay;
	wait_queue_head_t open_wq;
};

struct sensor_operate {
	char *name;
	int type;
	int id_i2c;
	int range[2];
	int brightness[2];/*backlight min_brightness max_brightness*/
	int read_reg;
	int read_len;
	int id_reg;
	int id_data;
	int precision;
	unsigned short slave_addr;
	int ctrl_reg;
	int ctrl_data;
	int int_ctrl_reg;
	int int_status_reg;
	int trig;/*intterupt trigger*/
	int (*active)(struct i2c_client *client, int enable, int rate);
	int (*init)(struct i2c_client *client);
	int (*report)(struct i2c_client *client);
	int (*getdata)(struct i2c_client *client, void *data);
	int (*get_fifo_data)(struct i2c_client *client, void *data);
	int (*suspend)(struct i2c_client *client);
	int (*resume)(struct i2c_client *client);
	int (*interrupt_use)(struct i2c_client *client, int num, int enable);
	int (*calibration)(struct i2c_client *client, char *data);
	struct miscdevice *misc_dev;
};

/* Platform data for the sensor */
struct sensor_private_data {
	int type;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct work_struct work;
	struct delayed_work delaywork;	/*report second event*/
	struct sensor_axis axis;
	char sensor_data[40];		//max support40 bytes data
	atomic_t data_ready;
	wait_queue_head_t data_ready_wq;
	struct mutex data_mutex;
	struct mutex operation_mutex;
	struct mutex sensor_mutex;
	struct mutex i2c_mutex;
	int status_cur;
	int start_count;
	int devid;
	struct sensor_flag flags;
	struct i2c_device_id *i2c_id;
	struct sensor_platform_data *pdata;
	struct sensor_operate *ops;
	struct file_operations fops;
	struct miscdevice miscdev;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct 	early_suspend early_suspend;
#endif
};

struct sensor_platform_data {
	int type;
	int irq;
	int irq_pin;
	int power_pin;
	int reset_pin;
	int standby_pin;
	int irq_enable;         //if irq_enable=1 then use irq else use polling
	int poll_delay_ms;      //polling
	int x_min;              //filter
	int y_min;
	int z_min;
	int factory;
	int layout;
	unsigned char address;
	unsigned long irq_flags;
	signed char orientation[9];
	short m_layout[4][3][3];
	int* project_name;
};

 struct gsensor_platform_data {
         u16 model;
         u16 swap_xy;
         u16 swap_xyz;
         signed char orientation[9];
         int (*get_pendown_state)(void);
         int (*init_platform_hw)(void);
         int (*gsensor_platform_sleep)(void);
         int (*gsensor_platform_wakeup)(void);
         void (*exit_platform_hw)(void);
 };

 struct akm8975_platform_data {
         short m_layout[4][3][3];
         char project_name[64];
         int gpio_DRDY;
 };

 struct akm_platform_data {
        short m_layout[4][3][3];
        char project_name[64];
        char layout;
        char outbit;
        int gpio_DRDY;
        int gpio_RST;
 };

extern int sensor_register_slave(int type,struct i2c_client *client,
			struct sensor_platform_data *slave_pdata,
			struct sensor_operate *(*get_sensor_ops)(void));


extern int sensor_unregister_slave(int type,struct i2c_client *client,
			struct sensor_platform_data *slave_pdata,
			struct sensor_operate *(*get_sensor_ops)(void));

#if 0
#define DBG(x...) if((atomic_read(&sensor->flags.debug_flag) == sensor->pdata->type) || (atomic_read(&sensor->flags.debug_flag) == SENSOR_NUM_TYPES))printk(x)
#else
#define DBG(x...)
#endif

#define GSENSOR_IOCTL_MAGIC			'a'
#define GBUFF_SIZE				12	/* Rx buffer size */

/* IOCTLs for MMA8452 library */
#define GSENSOR_IOCTL_INIT		_IO(GSENSOR_IOCTL_MAGIC, 0x01)
#define GSENSOR_IOCTL_CLOSE		_IO(GSENSOR_IOCTL_MAGIC, 0x02)
#define GSENSOR_IOCTL_START		_IO(GSENSOR_IOCTL_MAGIC, 0x03)
#define GSENSOR_IOCTL_RESET		_IO(GSENSOR_IOCTL_MAGIC, 0x04)
#define GSENSOR_IOCTL_USE_INT1	_IOW(GSENSOR_IOCTL_MAGIC, 0x05, char)
#define GSENSOR_IOCTL_USE_INT2	_IOW(GSENSOR_IOCTL_MAGIC, 0x06, char)
#define GSENSOR_IOCTL_GETDATA \
_IOR(GSENSOR_IOCTL_MAGIC, 0x08, char[GBUFF_SIZE + 1])

/* IOCTLs for APPs */
#define GSENSOR_IOCTL_APP_SET_RATE _IOW(GSENSOR_IOCTL_MAGIC, 0x10, char)

#define COMPASS_IOCTL_MAGIC                   'c'
/* IOCTLs for APPs */
#define ECS_IOCTL_APP_SET_MODE		_IOW(COMPASS_IOCTL_MAGIC, 0x10, short)
#define ECS_IOCTL_APP_SET_MFLAG		_IOW(COMPASS_IOCTL_MAGIC, 0x11, short)
#define ECS_IOCTL_APP_GET_MFLAG		_IOW(COMPASS_IOCTL_MAGIC, 0x12, short)
#define ECS_IOCTL_APP_SET_AFLAG		_IOW(COMPASS_IOCTL_MAGIC, 0x13, short)
#define ECS_IOCTL_APP_GET_AFLAG		_IOR(COMPASS_IOCTL_MAGIC, 0x14, short)
#define ECS_IOCTL_APP_SET_TFLAG		_IOR(COMPASS_IOCTL_MAGIC, 0x15, short)/* NOT use */
#define ECS_IOCTL_APP_GET_TFLAG		_IOR(COMPASS_IOCTL_MAGIC, 0x16, short)/* NOT use */
#define ECS_IOCTL_APP_RESET_PEDOMETER   _IOW(COMPASS_IOCTL_MAGIC, 0x17)	/* NOT use */
#define ECS_IOCTL_APP_SET_DELAY		_IOW(COMPASS_IOCTL_MAGIC, 0x18, short)
#define ECS_IOCTL_APP_SET_MVFLAG	_IOW(COMPASS_IOCTL_MAGIC, 0x19, short)
#define ECS_IOCTL_APP_GET_MVFLAG	_IOR(COMPASS_IOCTL_MAGIC, 0x1A, short)
#define ECS_IOCTL_APP_GET_DELAY		_IOR(COMPASS_IOCTL_MAGIC, 0x1B, short)

#define GYROSENSOR_IOCTL_MAGIC			'f'
#define GYROSENSOR_IOCTL_CLOSE		_IO(GYROSENSOR_IOCTL_MAGIC, 0x00)
#define GYROSENSOR_IOCTL_START		_IO(GYROSENSOR_IOCTL_MAGIC, 0x01)
#define GYROSENSOR_IOCTL_RESET		_IO(GYROSENSOR_IOCTL_MAGIC, 0x02)
#define GYROSENSOR_IOCTL_GETDATA \
_IOWR(GYROSENSOR_IOCTL_MAGIC, 0x03, raw_data_set_t)
#define GYROSENSOR_IOCTL_CALIBRATION \
_IOWR(GYROSENSOR_IOCTL_MAGIC, 0x04, char *)
#define GYROSENSOR_IOCTL_GETDATA_FIFO \
_IOWR(GYROSENSOR_IOCTL_MAGIC, 0x05, raw_data_set_t)

#define LIGHTSENSOR_IOCTL_MAGIC 'l'
#define LIGHTSENSOR_IOCTL_GET_ENABLED		_IOR(LIGHTSENSOR_IOCTL_MAGIC, 1, int *)
#define LIGHTSENSOR_IOCTL_ENABLE		_IOW(LIGHTSENSOR_IOCTL_MAGIC, 2, int *)
#define LIGHTSENSOR_IOCTL_DISABLE		_IOW(LIGHTSENSOR_IOCTL_MAGIC, 3, int *)

#define PSENSOR_IOCTL_MAGIC 'p'
#define PSENSOR_IOCTL_GET_ENABLED 		_IOR(PSENSOR_IOCTL_MAGIC, 1, int *)
#define PSENSOR_IOCTL_ENABLE 			_IOW(PSENSOR_IOCTL_MAGIC, 2, int *)
#define PSENSOR_IOCTL_DISABLE       		_IOW(PSENSOR_IOCTL_MAGIC, 3, int *)


#define PRESSURE_IOCTL_MAGIC 'r'
#define PRESSURE_IOCTL_GET_ENABLED 		_IOR(PRESSURE_IOCTL_MAGIC, 1, int *)
#define PRESSURE_IOCTL_ENABLE 			_IOW(PRESSURE_IOCTL_MAGIC, 2, int *)
#define PRESSURE_IOCTL_DISABLE       		_IOW(PRESSURE_IOCTL_MAGIC, 3, int *)
#define PRESSURE_IOCTL_SET_DELAY       		_IOW(PRESSURE_IOCTL_MAGIC, 4, int *)


#define TEMPERATURE_IOCTL_MAGIC 't'
#define TEMPERATURE_IOCTL_GET_ENABLED 		_IOR(TEMPERATURE_IOCTL_MAGIC, 1, int *)
#define TEMPERATURE_IOCTL_ENABLE 		_IOW(TEMPERATURE_IOCTL_MAGIC, 2, int *)
#define TEMPERATURE_IOCTL_DISABLE       	_IOW(TEMPERATURE_IOCTL_MAGIC, 3, int *)
#define TEMPERATURE_IOCTL_SET_DELAY       	_IOW(TEMPERATURE_IOCTL_MAGIC, 4, int *)


extern int sensor_rx_data(struct i2c_client *client, char *rxData, int length);
extern int sensor_tx_data(struct i2c_client *client, char *txData, int length);
extern int sensor_write_reg(struct i2c_client *client, int addr, int value);
extern int sensor_read_reg(struct i2c_client *client, int addr);
extern int sensor_tx_data_normal(struct i2c_client *client, char *buf, int num);
extern int sensor_rx_data_normal(struct i2c_client *client, char *buf, int num);
extern int sensor_write_reg_normal(struct i2c_client *client, char value);
extern int sensor_read_reg_normal(struct i2c_client *client);

