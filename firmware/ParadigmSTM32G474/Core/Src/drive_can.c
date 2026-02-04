/*
 * drive_can.c
 *
 *  Created on: Jan 27, 2026
 *      Author: Steven Dyer
 */

#include "drive_can.h"
#include <string.h>

DriveController_t drive;

static void ConfigureFDCANFilter();

bool Drive_Init(FDCAN_HandleTypeDef *hfdcan, uint8_t mcu_address) {

	if (hfdcan == NULL) {
		return false;
	}

	memset(&drive, 0, sizeof(DriveController_t));

	drive.hfdcan = hfdcan;
	drive.mcu_address = mcu_address;
	drive.can_id_command = CAN_ID_VCU_TO_MCU;

	ConfigureFDCANFilter();

	if (HAL_FDCAN_Start(hfdcan) != HAL_OK) {
		return false;
	}


}

static void ConfigureFDCANFilter() {

	if (drive.hfdcan == NULL) {
		return;
	}

	FDCAN_FilterTypeDef filter_config;

	filter_config.IdType = FDCAN_EXTENDED_ID;
	filter_config.FilterIndex = 0;
	filter_config.FilterType = FDCAN_FILTER_MASK;
	filter_config.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
	filter_config.FilterID1 = 0x00000000;
	filter_config.FilterID2 = 0x00000000;

	HAL_FDCAN_ConfigFilter(drive.hfdcan, &filter_config);

	filter_config.FilterIndex = 1;
    filter_config.FilterConfig = FDCAN_FILTER_TO_RXFIFO1;
    HAL_FDCAN_ConfigFilter(drive.hfdcan, &filter_config);
}
