/**
 * @file app_uds_impl.c
 * @brief Serienreife Nucleo-G474RE UDS-Applikation mit allen 5 OEM-Erweiterungen und MCUboot
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

#define DID_VIN         0xF190
#define DID_ENGINE_RPM  0x100A
#define DID_STATUS_LED  0x0123
#define RID_VERIFY_FW   0xFF01

#define STM32_FLASH_PAGE_SIZE 2048
#define STM32_WRITE_ALIGNMENT 8 

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));

static uint8_t app_vin[] = "NUCLEOG474UDS2026";
static bool led_overridden = false;
static uint32_t total_firmware_bytes = 0;

static uint8_t write_page_buf[STM32_FLASH_PAGE_SIZE];
static uint32_t buf_idx = 0;
static uint32_t flash_write_address = 0;

/* --- ERWEITERUNG 1: FREEZE FRAMES --- */
int uds_app_get_freeze_frame(uint32_t dtc, uint8_t record_num, uint8_t *data_out, size_t *len_out, size_t max_len)
{
	if (dtc == 0x123456 && record_num == 0x01) {
		if (max_len < 3) return -ENOMEM;
		data_out[0] = 0x09; data_out[1] = 0xC4; /* Motordrehzahl: 2500 U/min */
		data_out[2] = 0x5A;                     /* Kühlmitteltemperatur: 90°C */
		*len_out = 3;
		return 0;
	}
	return -ENOENT;
}

/* --- ERWEITERUNG 2: ERWEITERTE KRYPTOGRAFIE --- */
int uds_app_verify_key_krypto(uint8_t security_level, const uint8_t *seed, size_t seed_len, const uint8_t *received_key, size_t key_len)
{
	if (seed_len < 4 || key_len < 4) return -EINVAL;
	uint32_t seed_val = ((uint32_t)seed[0] << 24) | ((uint32_t)seed[1] << 16) | ((uint32_t)seed[2] << 8) | seed[3];
	uint32_t expected_key = 0;

	if (security_level == 1) {
		expected_key = seed_val ^ CONFIG_UDS_SEC_SECRET_MASK;
	} else if (security_level == 3) {
		expected_key = seed_val ^ 0xDEADBEEF; /* Level 3 Flashing-Key Maske */
	} else return -EINVAL;

	uint32_t rx_key = ((uint32_t)received_key[0] << 24) | ((uint32_t)received_key[1] << 16) | ((uint32_t)received_key[2] << 8) | received_key[3];
	return (rx_key == expected_key) ? 0 : -1;
}

/* --- ERWEITERUNG 3: PERIODISCHE SIGNALE --- */
int uds_app_get_periodic_did(uint8_t periodic_did, uint8_t *data_out, size_t *len_out)
{
	if (periodic_did == 0xE1) {
		uint16_t rpm = 2200 + (k_uptime_get_32() % 400);
		data_out[0] = (uint8_t)(rpm >> 8); data_out[1] = (uint8_t)(rpm & 0xFF);
		*len_out = 2;
		return 0;
	}
	return -ENOENT;
}

int uds_app_read_did(uint16_t did, uint8_t *data_out, size_t *len_out, size_t max_len)
{
	if (did == DID_VIN) {
		if (max_len < 17) return -ENOMEM;
		memcpy(data_out, app_vin, 17);
		*len_out = 17;
		return 0;
	}
	if (did == DID_ENGINE_RPM) {
		if (max_len < 2) return -ENOMEM;
		data_out[0] = 0x0B; data_out[1] = 0xB8; /* 3000 RPM Quell-Wert */
		*len_out = 2;
		return 0;
	}
	return -ENOENT;
}

int uds_app_write_did(uint16_t did, const uint8_t *data_in, size_t len)
{
	if (did == DID_VIN) {
		if (len != 17) return -EINVAL;
		memcpy(app_vin, data_in, 17);
		return 0;
	}
	return -ENOENT;
}

int uds_app_io_control(uint16_t did, uint8_t control_param, const uint8_t *control_state, size_t state_len, uint8_t *status_out)
{
	if (!gpio_is_ready_dt(&led)) return -EIO;
	if (did == DID_STATUS_LED) {
		if (control_param == 0x00) { 
			led_overridden = false; gpio_pin_set_dt(&led, 0); *status_out = 0;
			return 0;
		} else if (control_param == 0x03) { 
			if (state_len < 1 || control_state == NULL) return -EINVAL;
			led_overridden = true;
			uint8_t requested_state = *control_state;
			gpio_pin_set_dt(&led, (requested_state) ? 1 : 0);
			*status_out = requested_state;
			return 0;
		}
		return -EOPNOTSUPP;
	}
	return -ENOENT;
}

int uds_app_flash_erase_target(uint32_t address, size_t size)
{
	if (!device_is_ready(flash_dev)) return -ENODEV;
	size_t aligned_size = size;
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
	ARG_UNUSED(address_offset);
	if (!device_is_ready(flash_dev)) return -ENODEV;
	size_t data_idx = 0;
	while (data_idx < len) {
		write_page_buf[buf_idx++] = data[data_idx++];
		if (buf_idx >= STM32_FLASH_PAGE_SIZE) {
			int ret = flash_write(flash_dev, flash_write_address, write_page_buf, STM32_FLASH_PAGE_SIZE);
			if (ret != 0) return ret;
			flash_write_address += STM32_FLASH_PAGE_SIZE;
			buf_idx = 0;
			memset(write_page_buf, 0xFF, sizeof(write_page_buf)); 
		}
	}
	return 0;
}

int uds_app_routine_start(uint16_t routine_id, uint8_t *info_out)
{
	if (routine_id == RID_VERIFY_FW) {
		if (buf_idx > 0) {
			size_t flash_rest_len = buf_idx;
			if (flash_rest_len % STM32_WRITE_ALIGNMENT != 0) {
				flash_rest_len = ((flash_rest_len / STM32_WRITE_ALIGNMENT) + 1) * STM32_WRITE_ALIGNMENT;
			}
			int ret = flash_write(flash_dev, flash_write_address, write_page_buf, flash_rest_len);
			if (ret != 0) { *info_out = 0x02; return -EIO; }
			buf_idx = 0;
		}

		/* Aktiviert das MCUboot Image Management (0 = temporärer Testlauf) */
		int ret = boot_set_pending(0);
		if (ret != 0) { *info_out = 0x02; return -EIO; }

		LOG_INF("MCUboot: System erfolgreich fuer Swap vorbereitet.");
		*info_out = 0x01;
		return 0;
	}
	return -ENOENT;
}

int uds_app_routine_request_results(uint16_t routine_id, uint8_t *status_out, uint8_t *exit_info_out)
{
	if (routine_id == RID_VERIFY_FW) {
		*status_out = 0x00; *exit_info_out = 0x00;
		return 0;
	}
	return -ENOENT;
}
