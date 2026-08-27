/*
 * Copyright (c) 2026 borob-engineering
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Application entry point and non-volatile storage initialization for the UDS ECU.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Resolve storage partition metadata from the active Devicetree context */
#define STORAGE_NODE       DT_NODELABEL(storage_partition)
#define STORAGE_DEV        DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(STORAGE_NODE))
#define STORAGE_OFFSET     DT_REG_ADDR(STORAGE_NODE)
#define STORAGE_SIZE       DT_REG_SIZE(STORAGE_NODE)

/** @brief Global application managed Non-Volatile Storage (NVS) filesystem handle. */
struct nvs_fs app_nvs;

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* External module prototype from the UDS server subsystem */
extern int uds_init(void);

/**
 * @brief Main application thread initializing hardware layers and mounting local filesystems.
 */
int main(void)
{
	struct flash_pages_info page_info;
	int ret;

	LOG_INF("Starting UDS Application context...");

	/* 1. Configure the board status indicator LED */
	if (device_is_ready(led.port)) {
		(void)gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	} else {
		LOG_WRN("Status LED hardware port not ready or disabled.");
	}

	/* 2. Verify physical flash device hosting our storage partition layout */
	if (!device_is_ready(STORAGE_DEV)) {
		LOG_ERR("Kritischer Fehler: Flash-Hardware fuer Storage ist nicht bereit!");
		return -ENODEV;
	}

	app_nvs.flash_device = STORAGE_DEV;
	app_nvs.offset = STORAGE_OFFSET;

	/* 3. Extract exact page layout semantics at the allocated partition boundaries */
	ret = flash_get_page_info_by_offs(app_nvs.flash_device, app_nvs.offset, &page_info);
	if (ret != 0) {
		LOG_ERR("Kritischer Fehler: Kann Flash-Page-Geometrie nicht lesen: %d", ret);
		return ret;
	}

	app_nvs.sector_size = page_info.size;
	app_nvs.sector_count = STORAGE_SIZE / page_info.size;

	/* 4. Mount the global Key-Value Non-Volatile Storage structure */
	ret = nvs_mount(&app_nvs);
	if (ret != 0) {
		LOG_ERR("Kritischer Fehler: NVS-Dateisystem Mount fehlgeschlagen: %d", ret);
		return ret;
	}

	LOG_INF("NVS storage filesystem mounted successfully.");

	/* 5. Initialize the decoupled core UDS server thread layer */
	ret = uds_init();
	if (ret < 0) {
		LOG_ERR("Kritischer Fehler: UDS Server-Start fehlgeschlagen: %d", ret);
		return ret;
	}

	LOG_INF("UDS Core Subsystem successfully spawned in background context.");

	/* The main thread drops into low-power idling as network handlers utilize tasks */
	for (;;) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
