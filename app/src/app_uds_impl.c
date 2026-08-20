/**
 * @file app_uds_impl.c
 * @brief Nucleo-G474RE spezifische UDS Applikations-Implementierung mit Flash-Treiber-API
 */

#include <zephyr/canbus/uds_app_interface.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/flash.h> /* NEU: Zugriff auf Zephyrs HAL Flash API */
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_uds, LOG_LEVEL_INF);

#define DID_VIN        0xF190
#define DID_STATUS_LED 0x0123

#define STM32_FLASH_PAGE_SIZE 2048 /* 2 KB Sektorgroesse beim STM32G4 */

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
/* Holt die Instanz des board-spezifischen internen Flash-Controllers */
static const struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));

static uint8_t app_vin[17] = "NUCLEOG474UDS2026";
static bool led_overridden = false;

int uds_app_read_did(uint16_t did, uint8_t *data_out, size_t *len_out, size_t max_len)
{
	if (did == DID_VIN) {
		if (max_len < 17) return -ENOMEM;
		memcpy(data_out, app_vin, 17);
		*len_out = 17;
		return 0;
	}
	return -ENOENT;
}

int uds_app_write_did(uint16_t did, const uint8_t *data_in, size_t len)
{
	if (did == DID_VIN) {
		if (len != 17) return -EINVAL;
		memcpy(app_vin, data_in, 17);
		LOG_INF("App: Neue VIN erfolgreich im RAM gespeichert.");
		return 0;
	}
	return -ENOENT;
}

int uds_app_io_control(uint16_t did, uint8_t control_param, const uint8_t *control_state, size_t state_len, uint8_t *status_out)
{
	if (!gpio_is_ready_dt(&led)) return -EIO;

	if (did == DID_STATUS_LED) {
		if (control_param == 0x00) { 
			led_overridden = false;
			gpio_pin_set_dt(&led, 0); 
			*status_out = 0;
			return 0;
		} else if (control_param == 0x03) { 
			if (state_len < 1) return -EINVAL;
			led_overridden = true;
			uint8_t requested_state = control_state[0];
			gpio_pin_set_dt(&led, requested_state ? 1 : 0);
			*status_out = requested_state;
			return 0;
		}
		return -EOPNOTSUPP;
	}
	return -ENOENT;
}

uint32_t uds_app_calculate_key(const uint8_t *seed, size_t len)
{
	if (len < 4) return 0;
	uint32_t seed_val = ((uint32_t)seed[0] << 24) | ((uint32_t)seed[1] << 16) | 
	                    ((uint32_t)seed[2] << 8)  | (uint32_t)seed[3];
	return seed_val ^ CONFIG_UDS_SEC_SECRET_MASK;
}

/* --- ECHTE HARDWARE SPEICHER-ANBINDUNG --- */

/**
 * @brief Implementierung Service 0x34: Bereitet den STM32-Flash auf die neuen Daten vor
 */
int uds_app_flash_erase_target(uint32_t address, size_t size)
{
	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash Controller Treiber nicht bereit!");
		return -ENODEV;
	}

	/* STM32 Hardware-Schutz: Die Groesse muss auf Page-Grenzen (2KB) gerundet werden */
	size_t aligned_size = size;
	if (aligned_size % STM32_FLASH_PAGE_SIZE != 0) {
		aligned_size = ((aligned_size / STM32_FLASH_PAGE_SIZE) + 1) * STM32_FLASH_PAGE_SIZE;
		LOG_WRN("Erasing: Groesse nicht ausgerichtet. Runde auf: %zu Bytes", aligned_size);
	}

	LOG_INF("Hardware-Flash: Loesche Bereich ab Addr 0x%08X (Groesse: %zu Bytes)...", address, aligned_size);
	
	/* Führt die physische Löschung der Flash-Zellen aus */
	int ret = flash_erase(flash_dev, address, aligned_size);
	if (ret != 0) {
		LOG_ERR("Fehler beim physischen Loeschen des Flashs: %d", ret);
		return ret;
	}

	LOG_INF("Hardware-Flash: Bereich erfolgreich bereinigt.");
	return 0;
}

/**
 * @brief Implementierung Service 0x36: Schreibt einen ankommenden ISO-TP Block in den Flash
 */
int uds_app_flash_write_block(uint32_t address_offset, const uint8_t *data, size_t len)
{
	if (!device_is_ready(flash_dev)) return -ENODEV;

	/* Hinweis: STM32G4 erlaubt das Schreiben ab 64-Bit Double-Word Ausrichtung (8 Bytes). 
	 * Zephyrs HAL stm32_flash_driver fängt kleinere Reste im RAM-Buffer automatisch ab */
	int ret = flash_write(flash_dev, address_offset, data, len);
	if (ret != 0) {
		LOG_ERR("Fehler beim Schreiben auf Adresse 0x%08X: %d", address_offset, ret);
		return ret;
	}

	return 0;
}
