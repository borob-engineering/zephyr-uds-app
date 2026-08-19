/**
 * @file main.c
 * @brief Haupteinstiegspunkt für die Nucleo-G474RE UDS-Applikation
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

/* Externer Prototyp der Modul-Initialisierung */
extern int uds_init(void);

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
	LOG_INF("Starte UDS Applikation auf Nucleo-G474RE...");

	/* LED-Pin als Ausgang konfigurieren */
	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	} else {
		LOG_ERR("Kritischer Fehler: led0 DT-Knoten nicht bereit!");
	}

	/* Starte das generische UDS-Server-Modul */
	int ret = uds_init();
	if (ret < 0) {
		LOG_ERR("UDS Server-Start fehlgeschlagen: %d", ret);
		return ret;
	}

	LOG_INF("UDS Server-Thread laeuft erfolgreich im Hintergrund.");

	while (1) {
		/* Der Haupt-Thread schläft, da die UDS-Verarbeitung autark 
		 * im dedizierten Modul-Thread (uds_rx_thread) läuft. */
		k_sleep(K_FOREVER);
	}
	return 0;
}
