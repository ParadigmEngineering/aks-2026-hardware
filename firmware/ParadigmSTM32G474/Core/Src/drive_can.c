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

//Private helpers
static bool SendFDCANFrame(uint32_t ext_id, const uint8_t *data, uint8_t len) {
	FDCAN_TxHeaderTypeDef = {0};
	hdr.Indentifier = ext_id;
}

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

	if (HAL_FDCAN_ActivateNotification(hfdcan,
			FDCAN_IT_RX_FIFO0_NEW_MESSAGE |
			FDCAN_IT_BUS_OFF |
			FDCAN_IT_ERROR_PASSIVE |
			FDCAN_IT_ERROR_WARNING,
			0) != HAL_OK) {
		return false;
	}

	drive.last_tx_tick = HAL_GetTick();
	return true;
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

    // Configure global filter to reject non-matching frames
    HAL_FDCAN_ConfigGlobalFilter(
    	drive.hfdcan,
		FDCAN_REJECT, // Non-matching STD -> Reject
		FDCAN_ACCEPT_IN_RX_FIFO0, // Non-matching EXT -> Accept to FIFO
		FDCAN_REJECT, // STD remote -> Reject
		FDCAN_REJECT); // STD remote -> Reject
}

bool Drive_SendHandshake() {
	uint8_t handshake_data[8] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
	SendFDCANFrame(CAN_ID_VCU_TO_MCU, handshake_data, 8);
}

bool Drive_Stop() {

}


