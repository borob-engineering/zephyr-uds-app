/**
 * @file app_uds_impl.c
 * @brief Nucleo-G474RE spezifische UDS Applikations-Implementierung
 */

#include <zephyr/canbus/uds_app_interface.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(app_uds, LOG_LEVEL_INF);

#define DID_VIN        0xF190
#define DID_STATUS_LED 0x0123

/* Devicetree-Alias für die On-Board LED des Nucleo-G474 (LD2 ist meist led0) */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static uint8_t app_vin[17] = "NUCLEOG474UDS2026";
static bool led_overridden = false;

/* Implementierung: DID Lesen (Service 0x22) */
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

/* Implementierung: DID Schreiben (Service 0x2E) */
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

/* Implementierung: Echter Hardware-Stellgliedtest (Service 0x2F) */
int uds_app_io_control(uint16_t did, uint8_t control_param, const uint8_t *control_state, size_t state_len, uint8_t *status_out)
{
	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("GPIO LED-Hardware nicht bereit!");
		return -EIO;
	}

	if (did == DID_STATUS_LED) {
		if (control_param == 0x00) { /* returnControlToECU */
			led_overridden = false;
			gpio_pin_set_dt(&led, 0); /* LED im Normalbetrieb ausschalten */
			*status_out = 0;
			LOG_INF("App: LED-Kontrolle an ECU zurückgegeben.");
			return 0;
		} else if (control_param == 0x03) { /* shortTermAdjustment */
			if (state_len < 1) return -EINVAL;
			
			led_overridden = true;
			uint8_t requested_state = control_state[0];
			
			/* Hardware-Pin auf dem Nucleo-Board physisch schalten */
			gpio_pin_set_dt(&led, requested_state ? 1 : 0);
			
			*status_out = requested_state;
			LOG_INF("App: Hardware-LED via UDS ueberschrieben! Zustand: %d", requested_state);
			return 0;
		}
		return -EOPNOTSUPP;
	}
	return -ENOENT;
}

/* Krypto-Schlüsselberechnung (Service 0x27) */
uint32_t uds_app_calculate_key(const uint8_t *seed, size_t len)
{
	if (len < 4) return 0;
	uint32_t seed_val = ((uint32_t)seed[0] << 24) | ((uint32_t)seed[1] << 16) | 
	                    ((uint32_t)seed[2] << 8)  | (uint32_t)seed[3];
	
	return seed_val ^ CONFIG_UDS_SEC_SECRET_MASK;
}
