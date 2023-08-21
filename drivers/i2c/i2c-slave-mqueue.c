// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 - 2018, Intel Corporation.

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>

/**
 * 在执行完 make defconfig 命令后, 会在/linux-6.0.9/include/generated
 * 目录下生成一个 [autoconfig.h], 改头文件中会保存与[.config]文件对应的
 * CONFIG信息, 比如下面的 CONFIG_I2C_SLAVE_MQUEUE_MESSAGE_SIZE;
*/
#define MQ_MSGBUF_SIZE		CONFIG_I2C_SLAVE_MQUEUE_MESSAGE_SIZE // 256
#define MQ_QUEUE_SIZE		CONFIG_I2C_SLAVE_MQUEUE_QUEUE_SIZE	 // 32
#define MQ_QUEUE_NEXT(x)	(((x) + 1) & (MQ_QUEUE_SIZE - 1))

struct mq_msg {
	int	len;
	u8	*buf;
};

struct mq_queue {
	/**
	 * // include\linux\sysfs.h
	 * 
	 * struct bin_attribute {
	 * 		struct attribute	attr;
	 * 		size_t			size;
	 * 		void			*private;
	 * 		struct address_space *(*f_mapping)(void);
	 * 		ssize_t (*read)(struct file *, struct kobject *, struct bin_attribute *, char *, loff_t, size_t);
	 * 		ssize_t (*write)(struct file *, struct kobject *, struct bin_attribute *, char *, loff_t, size_t);
	 * 		int (*mmap)(struct file *, struct kobject *, struct bin_attribute *attr, struct vm_area_struct *vma);
	 * };
	*/
	struct bin_attribute bin;
	struct kernfs_node *kn;
	struct i2c_client *client; // i2c slave device;

	spinlock_t lock; /* spinlock for queue index handling */
	int in;
	int out;

	struct mq_msg *curr;
	int truncated; /* drop current if truncated */
	struct mq_msg *queue;
};

static bool dump_debug __read_mostly;
static int dump_debug_bus_id __read_mostly;

#define I2C_HEX_DUMP(client, buf, len) \
	do { \
		if (dump_debug && \
		    (client)->adapter->nr == dump_debug_bus_id) { \
			char dump_info[100] = {0,}; \
			snprintf(dump_info, sizeof(dump_info), \
				 "bus_id:%d: ", (client)->adapter->nr); \
			print_hex_dump(KERN_ERR, dump_info, DUMP_PREFIX_NONE, \
				       16, 1, buf, len, true); \
		} \
	} while (0)

static int i2c_slave_mqueue_callback(struct i2c_client *client,
				     enum i2c_slave_event event, u8 *val)
{
	struct mq_queue *mq = i2c_get_clientdata(client);
	struct mq_msg *msg = mq->curr;
	int ret = 0;

	switch (event) {
	case I2C_SLAVE_WRITE_REQUESTED:
	case I2C_SLAVE_GCALL_REQUESTED:
		mq->truncated = 0;

		msg->len = 1;
		msg->buf[0] = event == I2C_SLAVE_GCALL_REQUESTED ?
			      0 : client->addr << 1;
		break;

	case I2C_SLAVE_WRITE_RECEIVED:
		if (msg->len < MQ_MSGBUF_SIZE) {
			msg->buf[msg->len++] = *val;
		}
		else {
			dev_err(&client->dev, "message is truncated!\n");
			mq->truncated = 1;
			ret = -EINVAL;
		}
		break;

	case I2C_SLAVE_STOP:
		if (unlikely(mq->truncated || msg->len < 2))
			break;

		spin_lock(&mq->lock);
		mq->in = MQ_QUEUE_NEXT(mq->in);
		mq->curr = &mq->queue[mq->in];
		mq->curr->len = 0;

		/* Flush the oldest message */
		if (mq->out == mq->in)
			mq->out = MQ_QUEUE_NEXT(mq->out);
		spin_unlock(&mq->lock);

		kernfs_notify(mq->kn);
		break;

	default:
		*val = 0xFF;
		break;
	}

	return ret;
}

static ssize_t i2c_slave_mqueue_bin_read(struct file *filp,
					 struct kobject *kobj,
					 struct bin_attribute *attr,
					 char *buf, loff_t pos, size_t count)
{
	struct mq_queue *mq;
	struct mq_msg *msg;
	unsigned long flags;
	bool more = false;
	ssize_t ret = 0;

	mq = dev_get_drvdata(container_of(kobj, struct device, kobj));

	spin_lock_irqsave(&mq->lock, flags);
	if (mq->out != mq->in) {
		msg = &mq->queue[mq->out];

		if (msg->len <= count) {
			ret = msg->len;
			memcpy(buf, msg->buf, ret);
			I2C_HEX_DUMP(mq->client, buf, ret);
		} else {
			ret = -EOVERFLOW; /* Drop this HUGE one. */
		}

		mq->out = MQ_QUEUE_NEXT(mq->out);
		if (mq->out != mq->in)
			more = true;
	}
	spin_unlock_irqrestore(&mq->lock, flags);

	if (more)
		kernfs_notify(mq->kn);

	return ret;
}

static int i2c_slave_mqueue_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev; // 获取 struct i2c_client中的dev字段指针;
	struct mq_queue *mq;
	int ret, i;
	void *buf;

	mq = devm_kzalloc(dev, sizeof(*mq), GFP_KERNEL);
	if (!mq)
		return -ENOMEM;

	BUILD_BUG_ON(!is_power_of_2(MQ_QUEUE_SIZE));

	mq->client = client; // 就是设备节点: i2c-slave-mqueue@13(设备树中的信息)

	/**
	 * 申请 MQ_QUEUE_SIZE 个 MQ_MSGBUF_SIZE 大小的内存空间;
	*/
	buf = devm_kmalloc_array(dev, MQ_QUEUE_SIZE, MQ_MSGBUF_SIZE,
				 GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mq->queue = devm_kzalloc(dev, sizeof(*mq->queue) * MQ_QUEUE_SIZE,
				 GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < MQ_QUEUE_SIZE; i++)
		mq->queue[i].buf = buf + i * MQ_MSGBUF_SIZE;

	/**
	 * struct i2c_client -> struct device -> void *driver_data;
	 * client->dev->driver_data
	 * Driver data, set and get with dev_set_drvdata/dev_get_drvdata
	*/
	i2c_set_clientdata(client, mq);

	spin_lock_init(&mq->lock);
	mq->curr = &mq->queue[0];

	sysfs_bin_attr_init(&mq->bin);
	mq->bin.attr.name = "slave-mqueue";
	mq->bin.attr.mode = 0400;
	mq->bin.read = i2c_slave_mqueue_bin_read;
	mq->bin.size = MQ_MSGBUF_SIZE * MQ_QUEUE_SIZE;

	ret = sysfs_create_bin_file(&dev->kobj, &mq->bin);
	if (ret)
		return ret;

	mq->kn = kernfs_find_and_get(dev->kobj.sd, mq->bin.attr.name);
	if (!mq->kn) {
		sysfs_remove_bin_file(&dev->kobj, &mq->bin);
		return -EFAULT;
	}

	ret = i2c_slave_register(client, i2c_slave_mqueue_callback);
	if (ret) {
		kernfs_put(mq->kn);
		sysfs_remove_bin_file(&dev->kobj, &mq->bin);
		return ret;
	}

	return 0;
}

static void i2c_slave_mqueue_remove(struct i2c_client *client)
{
	struct mq_queue *mq = i2c_get_clientdata(client);

	i2c_slave_unregister(client);

	kernfs_put(mq->kn);
	sysfs_remove_bin_file(&client->dev.kobj, &mq->bin);

	return;
}

/**
 * 设备树示例:
 * 
 * // i2c#, # is count from 0
 * 
 * &i2c1 {
 *    status = "okay";
 *    // To PCIe slot SMBUS
 *    
 *    // 静态指定
 *    i2c-slave-mqueue@13 {
 *    	compatible = "slave-mqueue";
 *    	reg = <(0x13 | I2C_OWN_SLAVE_ADDRESS)>;
 *    	i2c-protocol;
 *    };
 * 
 * 	  // 动态指定:
 * 		参考链接: https://www.kernel.org/doc/html/latest/i2c/i2c-sysfs.html
 * 		# 将bmcSlaveAddr为0x13的设备挂载到i2c-1下面;
 *		$ echo "slave-mqueue 0x1013" > /sys/bus/i2c/devices/i2c-1/new_device
 * };
 * 
*/
static const struct i2c_device_id i2c_slave_mqueue_id[] = {
	{ "slave-mqueue", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c_slave_mqueue_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id i2c_slave_mqueue_of_match[] = {
	{ .compatible = "slave-mqueue", .data = (void *)0 },
	{ },
};
MODULE_DEVICE_TABLE(of, i2c_slave_mqueue_of_match);
#endif

static struct i2c_driver i2c_slave_mqueue_driver = {
	/**
	 * struct i2c_driver 中的 struct device_driver driver;
	*/
	.driver = {
		.name	= "i2c-slave-mqueue",
		.of_match_table = of_match_ptr(i2c_slave_mqueue_of_match),
	},
	.probe		= i2c_slave_mqueue_probe,
	.remove		= i2c_slave_mqueue_remove,
	.id_table	= i2c_slave_mqueue_id,
};
/**
 * #define module_i2c_driver(__i2c_driver) \
				module_driver(__i2c_driver, i2c_add_driver, \
							  i2c_del_driver)
*/
module_i2c_driver(i2c_slave_mqueue_driver);

module_param_named(dump_debug, dump_debug, bool, 0644);
MODULE_PARM_DESC(dump_debug, "debug flag for dump printing");
module_param_named(dump_debug_bus_id, dump_debug_bus_id, int, 0644);
MODULE_PARM_DESC(dump_debug_bus_id, "bus id for dump debug printing");

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Haiyue Wang <haiyue.wang@linux.intel.com>");
MODULE_DESCRIPTION("I2C slave mode for receiving and queuing messages");
