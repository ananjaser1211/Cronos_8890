/*
 * charge_control.c - Battery charge control for Samsung UNIVERSAL platforms
 *
 * @Author	: Andrei F. <https://github.com/AndreiLux>
 * @Date	: August 2013 - May 2015
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/battery/sec_battery.h>

static struct sec_battery_info *info;
	
enum charge_control_type {
	INPUT = 0, CHARGE, OTHER
};

struct charge_control {
	const struct device_attribute	attribute;
	enum charge_control_type	type;
	unsigned int			index;
};

static ssize_t show_charge_property(struct device *dev,
				    struct device_attribute *attr, char *buf);

static ssize_t store_charge_property(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count);

#define control(name_, type_, index_)			\
{ 							\
	.attribute = {					\
			.attr = {			\
				  .name = name_,	\
				  .mode = 0664,		\
				},			\
			.show = show_charge_property,	\
			.store = store_charge_property,	\
		     },					\
	.type 	= type_ ,				\
	.index 	= index_ ,				\
}

#define charge(name_, index_)				\
	control(name_"_input", INPUT, index_),		\
	control(name_"_charge", CHARGE, index_)
	
enum other_types {
	OTHER_SO_INPUT_LIMIT = 0,
	OTHER_SO_CHARGE_LIMIT,
	OTHER_WPC_CHARGE_LIMIT
};

struct charge_control charge_controls[] = {
	charge("ac"	, POWER_SUPPLY_TYPE_MAINS),
	charge("hv"	, POWER_SUPPLY_TYPE_HV_MAINS_CHG_LIMIT),
	charge("hv_prep", POWER_SUPPLY_TYPE_HV_PREPARE_MAINS),
	charge("sdp"	, POWER_SUPPLY_TYPE_USB),
	charge("dcp"	, POWER_SUPPLY_TYPE_USB_DCP),
	charge("cdp"	, POWER_SUPPLY_TYPE_USB_CDP),
	charge("aca"	, POWER_SUPPLY_TYPE_USB_ACA),
	charge("wc"	, POWER_SUPPLY_TYPE_WIRELESS),
	charge("car"	, POWER_SUPPLY_TYPE_CARDOCK),
	charge("otg"	, POWER_SUPPLY_TYPE_OTG),
	
	charge("mhl_500", POWER_SUPPLY_TYPE_MHL_500),
	charge("mhl_900", POWER_SUPPLY_TYPE_MHL_900),
	charge("mhl_1500", POWER_SUPPLY_TYPE_MHL_1500),
	charge("mhl_2000", POWER_SUPPLY_TYPE_MHL_2000),
	charge("mhl_usb", POWER_SUPPLY_TYPE_MHL_USB),
	charge("mhl_usb100", POWER_SUPPLY_TYPE_MHL_USB_100),
	
	control("so_limit_input", OTHER, OTHER_SO_INPUT_LIMIT),
	control("so_limit_charge", OTHER, OTHER_SO_CHARGE_LIMIT),
	control("wpc_limit_charge", OTHER, OTHER_WPC_CHARGE_LIMIT),
};

#define cc(index) info->pdata->charging_current[index]

static unsigned int * pdata_field(int type) {
	switch (type) {
		case OTHER_SO_INPUT_LIMIT:
			return &info->pdata->chg_charging_limit_current;
		case OTHER_SO_CHARGE_LIMIT:
			return &info->pdata->chg_charging_limit_current_2nd;
		case OTHER_WPC_CHARGE_LIMIT:
			return &info->pdata->wpc_charging_limit_current;
		default:
			return NULL;
	}
}

static ssize_t show_charge_property(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct charge_control *control = (struct charge_control*)(attr);
	int val = 0;

	switch (control->type) {
		case INPUT:	
			val = cc(control->index).input_current_limit;
			break;
		case CHARGE:	
			val = cc(control->index).fast_charging_current;
			break;
		case OTHER:
			val = *pdata_field(control->index);
		default:
			break;
	}

	return sprintf(buf, "%d", val);
}

static ssize_t store_charge_property(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct charge_control *control = (struct charge_control*)(attr);
	unsigned int val;

	if (sscanf(buf, "%u", &val) != 1)
		return -EINVAL;

	switch (control->type) {
		case INPUT:
			cc(control->index).input_current_limit = val;
			break;
		case CHARGE:
			cc(control->index).fast_charging_current = val;
			break;
		case OTHER:
			*pdata_field(control->index) = val;
		default:
			break;
	}

	return count;
}

void charger_control_init(struct sec_battery_info *sec_info)
{
	int i;

	info = sec_info;

	for (i = 0; i < ARRAY_SIZE(charge_controls); i++)
		if (device_create_file(info->dev, &charge_controls[i].attribute))
			;;
}
