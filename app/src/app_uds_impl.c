/*
 * Copyright (c) 2026 borob-engineering
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Production-ready application UDS interface implementation for Nucleo-G474RE.
 *
 * This file contains the concrete implementation of the weak driver execution hooks.
 * It integrates with Zephyr's NVS filesystem for permanent parameter tracking
 * and links into MCUboot for verified dual-bank firmware upgrades.
 */

#include <zephyr/canbus/uds_app_interface.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_uds, LOG_LEVEL_INF);

extern int boot_set_pending(int permanent);
extern void uds_trigger_event_response(uint8_t event_type, const uint8_t *payload, size_t len);
extern struct nvs_fs app_nvs; /* Globally managed NVS context from main.c */

#define DID_VIN         0xF190
#define DID_ENGINE_RPM  0x100A
#define DID_STATUS_LED  0x0123
#define RID_VERIFY_FW   0xFF01

#define STM32_FLASH_PAGE_SIZE 2048
#define STM32_WRITE_ALIGNMENT 8 
#define NVS_DTC_START_ID      0x0100
#define NVS_DTC_MAX_ENTRIES   16

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));

static uint8_t app_vin[VIN_SIZE] = "NUCLEOG474UDS2026";
static bool led_overridden;
static uint32_t total_firmware_bytes;

static uint8_t write_page_buf[STM32_FLASH_PAGE_SIZE];
static uint32_t buf_idx;
static uint32_t flash_write_address;

/* --- BRÜCKEN-FUNKTIONEN ZUM ECHTEN TREIBER-SPEICHER (NVS) --- */

struct nvs_fs *uds_app_get_nvs_context(void)
{
	return &app_nvs;
}

int uds_app_clear_persistent_dtcs(struct nvs_fs *fs, uint32_t dtc_group)
{
	int i;
	int ret;

	if (fs == NULL) {
		return -EINVAL;
	}

	if (dtc_group == 0xFFFFFF) {
		for (i = 0; i < NVS_DTC_MAX_ENTRIES; i++) {
			ret = nvs_delete(fs, NVS_DTC_START_ID + i);
			if (ret != 0 && ret != -ENOENT) {
				LOG_ERR("Failed to delete NVS entry ID 0x%04X: %d", NVS_DTC_START_ID + i, ret);
				return -EIO;
			}
		}
		LOG_INF("All persistent diagnostic trouble codes cleared from NVS storage.");
		return 0;
	}

	return -ENOTSUP;
}

int uds_app_write_persistent_data(struct nvs_fs *fs, uint16_t nvs_id, const uint8_t *data, size_t len)
{
	int ret;

	if (fs == NULL || data == NULL || len == 0) {
		return -EINVAL;
	}

	ret = nvs_write(fs, nvs_id, data, len);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/* --- ERWEITERUNG 1: FREEZE FRAMES --- */
int uds_app_get_freeze_frame(uint32_t dtc, uint8_t record_num, uint8_t *data_out, size_t *len_out, size_t max_len)
{
	if (dtc == 0x123456 && record_num == 0x01) {
		if (max_len < 3) {
			return -ENOMEM;
		}
		data_out[0] = 0x09;
		data_out[1] = 0xC4;
		data_out[2] = 0x5A;
		*len_out = 3;
		return 0;
	}
	return -ENOENT;
}
/* --- ERWEITERUNG 2: ERWEITERTE KRYPTOGRAFIE --- */
int uds_app_verify_key_krypto(uint8_t security_level, const uint8_t *seed, size_t seed_len, const uint8_t *received_key, size_t key_len)
{
	uint32_t seed_val;
	uint32_t expected_key = 0;
	uint32_t rx_key;

	if (seed_len < 4 || key_len < 4) {
		return -EINVAL;
	}

	seed_val = ((uint32_t)seed[0] << 24) | ((uint32_t)seed[1] << 16) | ((uint32_t)seed[2] << 8) | seed[3];

	if (security_level == 1) {
		expected_key = seed_val ^ CONFIG_UDS_SEC_SECRET_MASK;
	} else if (security_level == 3) {
		expected_key = seed_val ^ 0xDEADBEEF;
	} else {
		return -EINVAL;
	}

	rx_key = ((uint32_t)received_key[0] << 24) | ((uint32_t)received_key[1] << 16) | ((uint32_t)received_key[2] << 8) | received_key[3];
	
	return (rx_key == expected_key) ? 0 : -EACCES;
}

/* --- ERWEITERUNG 3: PERIODISCHE SIGNALE --- */
int uds_app_get_periodic_did(uint8_t periodic_did, uint8_t *data_out, size_t *len_out)
{
	uint16_t rpm;

	if (periodic_did == 0xE1) {
		rpm = 2200 + (k_uptime_get_32() % 400);
		data_out[0] = (uint8_t)(rpm >> 8);
		data_out[1] = (uint8_t)(rpm & 0xFF);
		*len_out = 2;
		return 0;
	}
	return -ENOENT;
}

int uds_app_read_did(uint16_t did, uint8_t *data_out, size_t *len_out, size_t max_len)
{
	if (did == DID_VIN) {
		if (max_len < VIN_SIZE) {
			return -ENOMEM;
		}
		memcpy(data_out, app_vin, VIN_SIZE);
		*len_out = VIN_SIZE;
		return 0;
	}
	if (did == DID_ENGINE_RPM) {
		if (max_len < 2) {
			return -ENOMEM;
		}
		data_out[0] = 0x0B;
		data_out[1] = 0xB8; /* 3000 RPM */
		*len_out = 2;
		return 0;
	}
	return -ENOENT;
}

int uds_app_write_did(uint16_t did, const uint8_t *data_in, size_t len)
{
	if (did == DID_VIN) {
		if (len != VIN_SIZE) {
			return -EINVAL;
		}
		memcpy(app_vin, data_in, VIN_SIZE);
		return 0;
	}
	return -ENOENT;
}

int uds_app_io_control(uint16_t did, uint8_t control_param, const uint8_t *control_state, size_t state_len, uint8_t *status_out)
{
	uint8_t requested_state;

	if (!gpio_is_ready_dt(&led)) {
		return -EIO;
	}

	if (did == DID_STATUS_LED) {
		if (control_param == 0x00) { 
			led_overridden = false;
			(void)gpio_pin_set_dt(&led, 0);
			*status_out = 0;
			return 0;
		} else if (control_param == 0x03) { 
			if (state_len < 1 || control_state == NULL) {
				return -EINVAL;
			}
			led_overridden = true;
			requested_state = *control_state;
			(void)gpio_pin_set_dt(&led, (requested_state) ? 1 : 0);
			*status_out = requested_state;
			return 0;
		}
		return -EOPNOTSUPP;
	}
	return -ENOENT;
}

int uds_app_flash_erase_target(uint32_t address, size_t size)
{
	size_t aligned_size = size;

	if (!device_is_ready(flash_dev)) {
		return -ENODEV;
	}

	if (aligned_size % STM32_FLASH_PAGE_SIZE != 0) {
		aligned_size = ((aligned_size / STM32_FLASH_PAGE_SIZE) + 1) * STM32_FLASH_PAGE_SIZE;
	}

	total_firmware_bytes = size;
	flash_write_address = address;
	buf_idx = 0; 
	memset(write_page_buf, 0xFF, sizeof(write_page_buf));
	
	return flash_erase(flash_dev, address, aligned_size);
}

int uds_app_flash_write_block(uint32_t address_offset, const uint8_t *data, size_t len)
{
	size_t data_idx = 0;
	int ret;

	ARG_UNUSED(address_offset);

	if (!device_is_ready(flash_dev)) {
		return -ENODEV;
	}

	while (data_idx < len) {
		write_page_buf[buf_idx++] = data[data_idx++];
		if (buf_idx >= STM32_FLASH_PAGE_SIZE) {
			ret = flash_write(flash_dev, flash_write_address, write_page_buf, STM32_FLASH_PAGE_SIZE);
			if (ret != 0) {
				return ret;
			}
			flash_write_address += STM32_FLASH_PAGE_SIZE;
			buf_idx = 0;
			memset(write_page_buf, 0xFF, sizeof(write_page_buf)); 
		}
	}
	return 0;
}

int uds_app_routine_start(uint16_t routine_id, uint8_t *info_out)
{
	size_t flash_rest_len;
	int ret;

	if (routine_id == RID_VERIFY_FW) {
		if (buf_idx > 0) {
			flash_rest_len = buf_idx;
			if (flash_rest_len % STM32_WRITE_ALIGNMENT != 0) {
				flash_rest_len = ((flash_rest_len / STM32_WRITE_ALIGNMENT) + 1) * STM32_WRITE_ALIGNMENT;
			}
			ret = flash_write(flash_dev, flash_write_address, write_page_buf, flash_rest_len);
			if (ret != 0) {
				*info_out = 0x02;
				return -EIO;
			}
			buf_idx = 0;
		}

		ret = boot_set_pending(0);
		if (ret != 0) {
			*info_out = 0x02;
			return -EIO;
		}

		LOG_INF("MCUboot: System successfully prepared for dual-bank image swap sequence.");
		*info_out = 0x01;
		return 0;
	}
	return -ENOENT;
}

int uds_app_routine_request_results(uint16_t routine_id, uint8_t *status_out, uint8_t *exit_info_out)
{
	if (routine_id == RID_VERIFY_FW) {
		*status_out = 0x00;
		*exit_info_out = 0x00;
		return 0;
	}
	return -ENOENT;
}

/* --- NEU: ERWEITERUNG 5: READ SCALING DATA BY IDENTIFIER (Service 0x24) --- */
int uds_app_read_scaling_data(uint16_t did, uint8_t *buf_out, size_t *len_out, size_t max_len)
{
	if (did == DID_ENGINE_RPM) {
		if (max_len < 7) {
			return -ENOMEM;
		}
		/* ISO 14229-1 linear scaling layout: y = (a * x) + b */
		buf_out[0] = 0x01; /* ScalingByte: 0x01 means linear formula identifier */
		buf_out[1] = 0x00; /* Coefficient 'a' Multiplier High Byte (value = 1) */
		buf_out[2] = 0x01; /* Coefficient 'a' Multiplier Low Byte */
		buf_out[3] = 0x00; /* Coefficient 'a' Divisor High Byte    (value = 1) */
		buf_out[4] = 0x01; /* Coefficient 'a' Divisor Low Byte */
		buf_out[5] = 0x00; /* Coefficient 'b' Offset High Byte     (value = 0) */
		buf_out[6] = 0x00; /* Coefficient 'b' Offset Low Byte */
		
		*len_out = 7;
		return 0;
	}
	return -ENOENT;
}
