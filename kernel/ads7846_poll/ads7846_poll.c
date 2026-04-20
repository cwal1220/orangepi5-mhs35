// SPDX-License-Identifier: GPL-2.0-only
/*
 * Polling touchscreen driver for ADS7846 / TSC2046 compatible controllers.
 *
 * This is intended for boards where the SPI touch ADC responds correctly but
 * the PENIRQ/pendown GPIO path is unusable. Instead of relying on an IRQ, it
 * samples X/Y/Z1/Z2 at a fixed interval and reports events through the input
 * subsystem.
 */

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>

#define TS_DEFAULT_POLL_MS          10
#define TS_DEFAULT_SAMPLE_COUNT     5
#define TS_DEFAULT_PRESSURE_ON      80
#define TS_DEFAULT_PRESSURE_OFF     40
#define TS_DEFAULT_RAW_X_MIN        280
#define TS_DEFAULT_RAW_X_MAX        3760
#define TS_DEFAULT_RAW_Y_MIN        300
#define TS_DEFAULT_RAW_Y_MAX        3886
#define TS_DEFAULT_SCREEN_X         480
#define TS_DEFAULT_SCREEN_Y         320
#define TS_DEFAULT_CAL_X_NUM        9701
#define TS_DEFAULT_CAL_X_DEN        10000
#define TS_DEFAULT_CAL_X_OFF        6
#define TS_DEFAULT_CAL_Y_NUM        9304
#define TS_DEFAULT_CAL_Y_DEN        10000
#define TS_DEFAULT_CAL_Y_OFF        9

#define ADS_START               BIT(7)
#define ADS_A2A1A0_d_y          (1 << 4)
#define ADS_A2A1A0_d_z1         (3 << 4)
#define ADS_A2A1A0_d_z2         (4 << 4)
#define ADS_A2A1A0_d_x          (5 << 4)
#define ADS_12_BIT              0
#define ADS_DFR                 0
#define ADS_PD10_PDOWN          0
#define ADS_PD10_ADC_ON         1

#define READ_12BIT_DFR(x, adc) \
	(ADS_START | ADS_A2A1A0_d_##x | ADS_12_BIT | ADS_DFR | \
	 ((adc) ? ADS_PD10_ADC_ON : 0))

#define READ_Y()                READ_12BIT_DFR(y, 1)
#define READ_Z1()               READ_12BIT_DFR(z1, 1)
#define READ_Z2()               READ_12BIT_DFR(z2, 1)
#define READ_X()                READ_12BIT_DFR(x, 1)
#define PWRDOWN()               READ_12BIT_DFR(y, 0)

struct ads7846_poll {
	struct spi_device *spi;
	struct input_dev *input;
	struct regulator *reg;
	struct delayed_work work;
	struct mutex lock;
	struct touchscreen_properties core_prop;
	bool pendown;
	bool stopped;
	u16 pressure_max;
	u16 pressure_on;
	u16 pressure_off;
	u16 x_plate_ohms;
	u16 poll_ms;
	u8 sample_count;
	u16 raw_x_min;
	u16 raw_x_max;
	u16 raw_y_min;
	u16 raw_y_max;
	u16 screen_x;
	u16 screen_y;
	u16 cal_x_num;
	u16 cal_x_den;
	s16 cal_x_off;
	u16 cal_y_num;
	u16 cal_y_den;
	s16 cal_y_off;
};

static int ads7846_poll_cmp(const void *a, const void *b)
{
	const u16 *va = a;
	const u16 *vb = b;

	return (int)*va - (int)*vb;
}

static unsigned int ads7846_poll_map_axis(u16 value, u16 src_min, u16 src_max,
					  u16 dst_max)
{
	u32 num;

	if (src_max <= src_min || dst_max == 0)
		return 0;

	if (value <= src_min)
		return 0;
	if (value >= src_max)
		return dst_max - 1;

	num = value - src_min;
	num *= dst_max - 1;
	return DIV_ROUND_CLOSEST(num, src_max - src_min);
}

static unsigned int ads7846_poll_apply_cal(unsigned int value, u16 num, u16 den,
					   s16 off, u16 max)
{
	int corrected;

	if (!den)
		return clamp_t(unsigned int, value, 0, max - 1);

	corrected = DIV_ROUND_CLOSEST((int)value * (int)num, (int)den) + off;
	return clamp_t(unsigned int, corrected, 0, max - 1);
}

static u16 ads7846_poll_median(u16 *values, unsigned int count)
{
	sort(values, count, sizeof(values[0]), ads7846_poll_cmp, NULL);
	return values[count / 2];
}

static int ads7846_poll_read12(struct ads7846_poll *ts, u8 cmd, u16 *value)
{
	u8 tx[3] = { cmd, 0x00, 0x00 };
	u8 rx[3] = { 0x00, 0x00, 0x00 };
	struct spi_transfer xfer = {
		.tx_buf = tx,
		.rx_buf = rx,
		.len = sizeof(tx),
		.speed_hz = ts->spi->max_speed_hz,
		.bits_per_word = 8,
	};
	int ret;

	ret = spi_sync_transfer(ts->spi, &xfer, 1);
	if (ret)
		return ret;

	*value = ((rx[1] << 8) | rx[2]) >> 3;
	return 0;
}

static int ads7846_poll_read_state(struct ads7846_poll *ts,
				   u16 *x, u16 *y, u16 *z1, u16 *z2)
{
	u16 xs[16], ys[16], z1s[16], z2s[16];
	unsigned int i;
	int ret;

	if (ts->sample_count > ARRAY_SIZE(xs))
		return -EINVAL;

	for (i = 0; i < ts->sample_count; i++) {
		ret = ads7846_poll_read12(ts, READ_X(), &xs[i]);
		if (ret)
			return ret;
		ret = ads7846_poll_read12(ts, READ_Y(), &ys[i]);
		if (ret)
			return ret;
		ret = ads7846_poll_read12(ts, READ_Z1(), &z1s[i]);
		if (ret)
			return ret;
		ret = ads7846_poll_read12(ts, READ_Z2(), &z2s[i]);
		if (ret)
			return ret;
	}

	ret = ads7846_poll_read12(ts, PWRDOWN(), &xs[0]);
	if (ret)
		return ret;

	*x = ads7846_poll_median(xs, ts->sample_count);
	*y = ads7846_poll_median(ys, ts->sample_count);
	*z1 = ads7846_poll_median(z1s, ts->sample_count);
	*z2 = ads7846_poll_median(z2s, ts->sample_count);
	return 0;
}

static void ads7846_poll_report_up(struct ads7846_poll *ts)
{
	if (!ts->pendown)
		return;

	input_report_abs(ts->input, ABS_PRESSURE, 0);
	input_report_key(ts->input, BTN_TOUCH, 0);
	input_sync(ts->input);
	ts->pendown = false;
}

static void ads7846_poll_report_down(struct ads7846_poll *ts,
				     u16 x, u16 y, u16 pressure)
{
	unsigned int sx;
	unsigned int sy;

	if (!ts->pendown) {
		input_report_key(ts->input, BTN_TOUCH, 1);
		ts->pendown = true;
	}

	/*
	 * The current Orange Pi 5 + MHS35 orientation needs both axes inverted
	 * relative to the first-pass mapping:
	 * screen X follows raw Y reversed, and screen Y follows raw X.
	 */
	sx = ts->screen_x - 1 -
	     ads7846_poll_map_axis(y, ts->raw_y_min, ts->raw_y_max, ts->screen_x);
	sy = ads7846_poll_map_axis(x, ts->raw_x_min, ts->raw_x_max, ts->screen_y);

	sx = ads7846_poll_apply_cal(sx, ts->cal_x_num, ts->cal_x_den,
				     ts->cal_x_off, ts->screen_x);
	sy = ads7846_poll_apply_cal(sy, ts->cal_y_num, ts->cal_y_den,
				     ts->cal_y_off, ts->screen_y);

	touchscreen_report_pos(ts->input, &ts->core_prop, sx, sy, false);
	input_report_abs(ts->input, ABS_PRESSURE, pressure);
	input_sync(ts->input);
}

static void ads7846_poll_work(struct work_struct *work)
{
	struct ads7846_poll *ts =
		container_of(to_delayed_work(work), struct ads7846_poll, work);
	u16 x, y, z1, z2;
	u32 rt = 0;
	u16 pressure = 0;
	bool active = false;
	int ret;

	mutex_lock(&ts->lock);
	if (ts->stopped)
		goto out_unlock;

	ret = ads7846_poll_read_state(ts, &x, &y, &z1, &z2);
	if (ret) {
		dev_err_ratelimited(&ts->spi->dev, "poll read failed: %d\n", ret);
		goto out_reschedule;
	}

	if (x && x != 0x0fff && y != 0x0fff && z1 >= ts->pressure_on) {
		if (likely(z1)) {
			rt = z2;
			rt -= z1;
			rt *= ts->x_plate_ohms;
			rt = DIV_ROUND_CLOSEST(rt, 16);
			rt *= x;
			rt /= z1;
			rt = DIV_ROUND_CLOSEST(rt, 256);
		}

		if (rt && rt <= ts->pressure_max) {
			pressure = min_t(u32, ts->pressure_max - rt, ts->pressure_max);
			active = true;
		} else if (!rt && z1 >= ts->pressure_on) {
			pressure = min_t(u16, z1, ts->pressure_max);
			active = true;
		}
	}

	if (!active && z1 > ts->pressure_on) {
		pressure = min_t(u16, z1, ts->pressure_max);
		active = true;
	}

	if (active) {
		ads7846_poll_report_down(ts, x, y, pressure);
	} else if (ts->pendown && z1 <= ts->pressure_off) {
		ads7846_poll_report_up(ts);
	}

out_reschedule:
	if (!ts->stopped)
		schedule_delayed_work(&ts->work, msecs_to_jiffies(ts->poll_ms));
out_unlock:
	mutex_unlock(&ts->lock);
}

static int ads7846_poll_parse_dt(struct device *dev, struct ads7846_poll *ts)
{
	struct device_node *node = dev->of_node;
	u32 value;

	ts->poll_ms = TS_DEFAULT_POLL_MS;
	ts->sample_count = TS_DEFAULT_SAMPLE_COUNT;
	ts->pressure_on = TS_DEFAULT_PRESSURE_ON;
	ts->pressure_off = TS_DEFAULT_PRESSURE_OFF;
	ts->x_plate_ohms = 400;
	ts->raw_x_min = TS_DEFAULT_RAW_X_MIN;
	ts->raw_x_max = TS_DEFAULT_RAW_X_MAX;
	ts->raw_y_min = TS_DEFAULT_RAW_Y_MIN;
	ts->raw_y_max = TS_DEFAULT_RAW_Y_MAX;
	ts->screen_x = TS_DEFAULT_SCREEN_X;
	ts->screen_y = TS_DEFAULT_SCREEN_Y;
	ts->cal_x_num = TS_DEFAULT_CAL_X_NUM;
	ts->cal_x_den = TS_DEFAULT_CAL_X_DEN;
	ts->cal_x_off = TS_DEFAULT_CAL_X_OFF;
	ts->cal_y_num = TS_DEFAULT_CAL_Y_NUM;
	ts->cal_y_den = TS_DEFAULT_CAL_Y_DEN;
	ts->cal_y_off = TS_DEFAULT_CAL_Y_OFF;

	if (!node)
		return -EINVAL;

	if (!of_property_read_u32(node, "ti,poll-period-ms", &value))
		ts->poll_ms = clamp_val(value, 1, 1000);

	if (!of_property_read_u32(node, "ti,poll-sample-count", &value))
		ts->sample_count = clamp_val(value, 1, 15);

	if (!of_property_read_u32(node, "ti,poll-pressure-on", &value))
		ts->pressure_on = value;

	if (!of_property_read_u32(node, "ti,poll-pressure-off", &value))
		ts->pressure_off = value;

	if (!of_property_read_u32(node, "ti,x-plate-ohms", &value))
		ts->x_plate_ohms = value;

	if (!of_property_read_u32(node, "ti,x-min", &value))
		ts->raw_x_min = value;
	if (!of_property_read_u32(node, "ti,x-max", &value))
		ts->raw_x_max = value;
	if (!of_property_read_u32(node, "ti,y-min", &value))
		ts->raw_y_min = value;
	if (!of_property_read_u32(node, "ti,y-max", &value))
		ts->raw_y_max = value;
	if (!of_property_read_u32(node, "touchscreen-size-x", &value))
		ts->screen_x = value;
	if (!of_property_read_u32(node, "touchscreen-size-y", &value))
		ts->screen_y = value;
	if (!of_property_read_u32(node, "ti,cal-x-num", &value))
		ts->cal_x_num = value;
	if (!of_property_read_u32(node, "ti,cal-x-den", &value))
		ts->cal_x_den = value;
	if (!of_property_read_u32(node, "ti,cal-x-off", &value))
		ts->cal_x_off = (s16)value;
	if (!of_property_read_u32(node, "ti,cal-y-num", &value))
		ts->cal_y_num = value;
	if (!of_property_read_u32(node, "ti,cal-y-den", &value))
		ts->cal_y_den = value;
	if (!of_property_read_u32(node, "ti,cal-y-off", &value))
		ts->cal_y_off = (s16)value;

	return 0;
}

static int ads7846_poll_open(struct input_dev *input)
{
	struct ads7846_poll *ts = input_get_drvdata(input);

	mutex_lock(&ts->lock);
	ts->stopped = false;
	schedule_delayed_work(&ts->work, 0);
	mutex_unlock(&ts->lock);

	return 0;
}

static void ads7846_poll_close(struct input_dev *input)
{
	struct ads7846_poll *ts = input_get_drvdata(input);

	mutex_lock(&ts->lock);
	ts->stopped = true;
	cancel_delayed_work_sync(&ts->work);
	ads7846_poll_report_up(ts);
	mutex_unlock(&ts->lock);
}

static int ads7846_poll_probe(struct spi_device *spi)
{
	struct ads7846_poll *ts;
	struct input_dev *input;
	int err;

	if (spi->max_speed_hz > 500000)
		spi->max_speed_hz = 500000;

	spi->bits_per_word = 8;
	spi->mode &= ~SPI_MODE_X_MASK;
	spi->mode |= SPI_MODE_0;
	err = spi_setup(spi);
	if (err)
		return err;

	ts = devm_kzalloc(&spi->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	input = devm_input_allocate_device(&spi->dev);
	if (!input)
		return -ENOMEM;

	ts->spi = spi;
	ts->input = input;
	ts->stopped = true;
	mutex_init(&ts->lock);
	INIT_DELAYED_WORK(&ts->work, ads7846_poll_work);

	err = ads7846_poll_parse_dt(&spi->dev, ts);
	if (err)
		return err;

	ts->reg = devm_regulator_get_optional(&spi->dev, "vcc");
	if (!IS_ERR(ts->reg)) {
		err = regulator_enable(ts->reg);
		if (err)
			return err;
	} else if (PTR_ERR(ts->reg) == -EPROBE_DEFER) {
		return PTR_ERR(ts->reg);
	} else {
		ts->reg = NULL;
	}

	input->name = "ADS7846 Poll Touchscreen";
	input->phys = "spi/input0";
	input->id.bustype = BUS_SPI;
	input->open = ads7846_poll_open;
	input->close = ads7846_poll_close;

	input_set_drvdata(input, ts);
	input_set_capability(input, EV_KEY, BTN_TOUCH);
	input_set_abs_params(input, ABS_X, 0, ts->screen_x - 1, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, ts->screen_y - 1, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, 4095, 0, 0);

	touchscreen_parse_properties(input, false, &ts->core_prop);
	ts->pressure_max = input_abs_get_max(input, ABS_PRESSURE) ?: 4095;

	spi_set_drvdata(spi, ts);

	err = input_register_device(input);
	if (err)
		return err;

	dev_info(&spi->dev,
		 "polling touchscreen, %u ms period, %u samples, z1 on/off %u/%u, raw x %u-%u, raw y %u-%u, screen %ux%u, cal x %u/%u %+d, cal y %u/%u %+d\n",
		 ts->poll_ms, ts->sample_count, ts->pressure_on,
		 ts->pressure_off, ts->raw_x_min, ts->raw_x_max,
		 ts->raw_y_min, ts->raw_y_max, ts->screen_x, ts->screen_y,
		 ts->cal_x_num, ts->cal_x_den, ts->cal_x_off,
		 ts->cal_y_num, ts->cal_y_den, ts->cal_y_off);
	return 0;
}

static void ads7846_poll_remove(struct spi_device *spi)
{
	struct ads7846_poll *ts = spi_get_drvdata(spi);

	mutex_lock(&ts->lock);
	ts->stopped = true;
	cancel_delayed_work_sync(&ts->work);
	mutex_unlock(&ts->lock);

	if (ts->reg)
		regulator_disable(ts->reg);
}

static int __maybe_unused ads7846_poll_suspend(struct device *dev)
{
	struct ads7846_poll *ts = dev_get_drvdata(dev);

	mutex_lock(&ts->lock);
	ts->stopped = true;
	cancel_delayed_work_sync(&ts->work);
	ads7846_poll_report_up(ts);
	mutex_unlock(&ts->lock);

	return 0;
}

static int __maybe_unused ads7846_poll_resume(struct device *dev)
{
	struct ads7846_poll *ts = dev_get_drvdata(dev);

	mutex_lock(&ts->lock);
	ts->stopped = false;
	if (input_device_enabled(ts->input))
		schedule_delayed_work(&ts->work, 0);
	mutex_unlock(&ts->lock);

	return 0;
}

static SIMPLE_DEV_PM_OPS(ads7846_poll_pm, ads7846_poll_suspend,
			 ads7846_poll_resume);

static const struct of_device_id ads7846_poll_of_match[] = {
	{ .compatible = "ti,tsc2046-poll" },
	{ .compatible = "ti,ads7846-poll" },
	{ }
};
MODULE_DEVICE_TABLE(of, ads7846_poll_of_match);

static const struct spi_device_id ads7846_poll_ids[] = {
	{ "ads7846_poll", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ads7846_poll_ids);

static struct spi_driver ads7846_poll_driver = {
	.driver = {
		.name = "ads7846_poll",
		.pm = &ads7846_poll_pm,
		.of_match_table = ads7846_poll_of_match,
	},
	.probe = ads7846_poll_probe,
	.remove = ads7846_poll_remove,
	.id_table = ads7846_poll_ids,
};
module_spi_driver(ads7846_poll_driver);

MODULE_AUTHOR("OpenAI");
MODULE_DESCRIPTION("Polling ADS7846/TSC2046 touchscreen driver");
MODULE_LICENSE("GPL");
