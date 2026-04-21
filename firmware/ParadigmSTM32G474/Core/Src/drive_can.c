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
	FDCAN_TxHeaderTypeDef hdr = {0};
	hdr.Identifier = ext_id;
	hdr.IdType = FDCAN_EXTENDED_ID;
	hdr.TxFrameType = FDCAN_DATA_FRAME;
	hdr.DataLength = FDCAN_DLC_BYTES_8;
	hdr.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	hdr.BitRateSwitch = FDCAN_BRS_OFF;
	hdr.FDFormat = FDCAN_CLASSIC_CAN;
	hdr.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	hdr.MessageMarker = 0;

	return HAL_FDCAN_AddMessageToTxFifoQ(drive.hfdcan, &hdr, (uint8_t *)data) == HAL_OK;
}

static uint16_t getU16LE(const uint8_t *buf, uint8_t offset) {
	return (uint16_t)buf[offset] | ((uint16_t)buf[offset + 1U] << 8U);
}

static void ParseStatus1(const uint8_t*d) {
	// Bus voltage: 0.1 V/bit, offset 0
	drive.status.bus_voltage = getU16LE(d, 0) * 0.1f;

	// Bus current: 0.1A/bit, offset -3200 A
	drive.status.bus_current = (getU16LE(d, 2) * 0.1f) - 3200.0f;

	//Phase current: 0.1A/bit, offset -3200 A
	drive.status.phase_current = (getU16LE(d, 4) * 0.1f) - 3200.0f;

	// Speed: 1rpm/bit, offset -32000 rpm
	drive.status.speed = (float)(getU16LE(d, 6)) - 32000.0f;

	// MCU is alive - reset missed-frame counter
	drive.missed_rx_count = 0;
}

static void ParseStatus2(const uint8_t *d) {
	// Byte 0: Controller Temperature, 1 degree C/bit, offset -40 degree C
	drive.status.ctrl_temp = (float)d[0] - 40.0f;

	// Byte 1: Motor Temperature, 1 degree C/bit, offset -40 degree C
	drive.status.motor_temp = (float)d[1] - 40.0f;

	// Byte 2: status bits
	drive.status.is_running =(d[2] & 0x01) != 0;
	drive.status.is_torque_mode = (d[2] & 0x02) != 0;

	// Bytes 3/4/5: error flags
	drive.status.errors.b3.all = d[3];
	drive.status.errors.b4.all = d[4];
	drive.status.errors.b5.all = d[5];

	// Byte 6: reserved

	// Byte 7: life signal - must change every poll or we declare failure;
	uint8_t new_life = d[7];
	if (new_life == drive.last_mcu_life_signal) {
		drive.life_fail_count++;
		if (drive.life_fail_count >= DRIVE_LIFESIG_FAIL_MAX) {
			drive.state = DRIVE_STATE_COMM_FAILED;
		}
	} else {
		drive.life_fail_count = 0;
		drive.last_mcu_life_signal = new_life;
	}
	drive.status.life_signal = new_life;
	drive.status.last_status2_tick = HAL_GetTick();
}

bool Drive_Init(FDCAN_HandleTypeDef *hfdcan, uint8_t mcu_address) {

	if (hfdcan == NULL) {
		return false;
	}

	memset(&drive, 0, sizeof(DriveController_t));

	drive.hfdcan = hfdcan;
	drive.mcu_address = mcu_address;
	drive.state = DRIVE_STATE_HANDSHAKING;

	ConfigureFDCANFilter();

	if (HAL_FDCAN_Start(hfdcan) != HAL_OK) {
		return false;
	}

	if (HAL_FDCAN_ActivateNotification(hfdcan,
			FDCAN_IT_RX_FIFO0_NEW_MESSAGE |
			FDCAN_IT_RX_FIFO1_NEW_MESSAGE |
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
	filter_config.FilterType = FDCAN_FILTER_MASK;
	// Filter 0: MCU Status1 -> FIFO0
	filter_config.FilterIndex = 0;
	filter_config.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
	filter_config.FilterID1 = CAN_ID_MCU_STATUS1;
	filter_config.FilterID2 = 0x1FFFFFFF;

	HAL_FDCAN_ConfigFilter(drive.hfdcan, &filter_config);

	// Filter 1: MCU Status2 -> FIFO1
	filter_config.FilterIndex = 1;
    filter_config.FilterConfig = FDCAN_FILTER_TO_RXFIFO1;
    filter_config.FilterID1 = CAN_ID_MCU_STATUS2;
    filter_config.FilterID2 = 0x1FFFFFFF;
    HAL_FDCAN_ConfigFilter(drive.hfdcan, &filter_config);

    // Configure global filter to reject non-matching frames
    HAL_FDCAN_ConfigGlobalFilter(
    	drive.hfdcan,
		FDCAN_REJECT, // Non-matching STD -> Reject
		FDCAN_REJECT, // Non-matching EXT -> Reject
		FDCAN_REJECT, // STD remote -> Reject
		FDCAN_REJECT); // STD remote -> Reject
}

bool Drive_SendHandshake() {
	uint8_t data[8];
	memset(data, VCU_HANDSHAKE_BYTE, sizeof(data));
	return SendFDCANFrame(CAN_ID_VCU_TO_MCU, data, 8);
}

bool Drive_SendCommand(int16_t target_current, int16_t target_speed, DriveCommand_t command){
	uint8_t buf[8] = {0};

	// Bytes 0-1: target phase current, little-endian
	buf[0] = (uint8_t)(target_current);
	buf[1] = (uint8_t)(target_current >> 8);

	// Bytes 2-3: target speed, little-endian
	buf[2] = (uint8_t)(target_speed);
	buf[3] = (uint8_t)(target_speed >> 8);

	// Byte 4: command control
	buf[4] = (uint8_t)command;

	// Bytes 5-6: reserved
	buf[5] = 0;
	buf[6] = 0;

	// Byte 7: life signal, must increment every frame
	buf[7] = drive.life_counter++;

	bool ok = SendFDCANFrame(CAN_ID_VCU_TO_MCU, buf, 8);

	// Track whether MCU has been responding
	if (ok) {
		drive.missed_rx_count++;
		if (drive.missed_rx_count >= DRIVE_TIMEOUT_COUNT) {
			drive.state = DRIVE_STATE_COMM_FAILED;
		}
	}

	return ok;
}

bool Drive_Stop() {
	return Drive_SendCommand(DRIVE_ENCODE_CURRENT(0.0f),
							DRIVE_ENCODE_SPEED(0.0f),
							CMD_HALTED);
}

void Drive_Tick(int16_t target_current, int16_t target_speed, DriveCommand_t command) {
	uint32_t now = HAL_GetTick();

	if ((now - drive.last_tx_tick) < DRIVE_POLL_MS) {
		return;
	}

	drive.last_tx_tick = now;
	switch(drive.state) {
		case DRIVE_STATE_HANDSHAKING:
			Drive_SendHandshake();
			break;

		case DRIVE_STATE_OPERATIONAL:
			Drive_SendCommand(target_current, target_speed, command);
			break;

		case DRIVE_STATE_COMM_FAILED:
			// Drop back to handshaking - MCU will restart its own sequence
			drive.state = DRIVE_STATE_HANDSHAKING;
			drive.missed_rx_count = 0;
			drive.life_fail_count = 0;
			break;

		default:
			break;
	}
}

void Drive_ProcessRx(const FDCAN_RxHeaderTypeDef *hdr, const uint8_t *data) {
	if (hdr == NULL || data == NULL) {
		return;
	}
	if (hdr->IdType != FDCAN_EXTENDED_ID) {
		return;
	}

	uint32_t id = hdr->Identifier;

	// Handshake: MCU sends 8x0x55 on the Status1 ID
	/*if (id == CAN_ID_MCU_STATUS1 && drive.state == DRIVE_STATE_HANDSHAKING) {
		bool all_match = true;

		for (uint8_t i = 0; i < 8; i++) {
			if (data[i] != MCU_HANDSHAKE_BYTE) {
				all_match = false;
				break;
			}
		}
		if (all_match) {
			drive.state = DRIVE_STATE_OPERATIONAL;
			drive.missed_rx_count = 0;
		}
		return;
	}*/

	if (id == CAN_ID_MCU_STATUS1 && drive.state == DRIVE_STATE_HANDSHAKING) {
	    // If we get the 0x55 beacon, we already sent 0xAA — handshake done
	    // If we get real telemetry, MCU already accepted our 0xAA — also done
	    drive.state = DRIVE_STATE_OPERATIONAL;
	    drive.missed_rx_count = 0;
	    // Also parse if it's real telemetry, not the 0x55 beacon
	    bool is_beacon = true;
	    for (uint8_t i = 0; i < 8; i++) {
	        if (data[i] != MCU_HANDSHAKE_BYTE) { is_beacon = false; break; }
	    }
	    if (!is_beacon) {
	        ParseStatus1(data);
	    }
	    return;
	}


	// Operational telemetry
	if (drive.state == DRIVE_STATE_OPERATIONAL) {
		if (id == CAN_ID_MCU_STATUS1) {
			ParseStatus1(data);
		} else if (id == CAN_ID_MCU_STATUS2) {
			ParseStatus2(data);
		}
	}

	// Recovery: if failed, listen for MCU re-handshake
	if (id == CAN_ID_MCU_STATUS1 && drive.state == DRIVE_STATE_COMM_FAILED) {
		bool all_match = true;
		for (uint8_t i = 0; i < 8; i++) {
			if (data[i] != MCU_HANDSHAKE_BYTE) {
				all_match = false;
				break;
			}
		}
		if (all_match) {
			drive.state = DRIVE_STATE_HANDSHAKING;
			drive.missed_rx_count = 0;
			drive.life_fail_count = 0;
		}
	}
}

DriveState_t Drive_GetState() {
	return drive.state;
}

bool Drive_HasError() {
	return (drive.status.errors.b3.all != 0 ||
			drive.status.errors.b4.all != 0 ||
			drive.status.errors.b5.all != 0);
}

void Drive_GetStatus(DriveStatus_t *out) {
	if (out != NULL) {
		memcpy(out, &drive.status, sizeof(DriveStatus_t));
	}
}
