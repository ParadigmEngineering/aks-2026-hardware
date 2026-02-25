/*
 * drive_can.h
 *
 *  Created on: Jan 27, 2026
 *      Author: Steven Dyer
 */

#ifndef INC_DRIVE_CAN_H_
#define INC_DRIVE_CAN_H_

#ifdef _cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"
#include <stdbool.h>

#define VCU_ADDRESS 		0xD0
#define MCU_ADDRESS_DEFAULT 0xEF

#define CAN_ID_VCU_TO_MCU 	0x0C01EFD0U
#define CAN_ID_MCU_STATUS1 	0x1801D0EFU
#define CAN_ID_MCU_STATUS2 	0x1802D0EFU

#define MCU_HANDSHAKE_BYTE 	0x55U
#define VCU_HANDSHAKE_BYTE 	0xAAU

#define DRIVE_POL_MS 			50U
#define DRIVE_TIMEOUT_COUNT 	10U
#define DRIVE_LIFESIG_FAIL_MAX 	5U

#define CURRENT_OFFSET_RAW 	32000
#define SPEED_OFFSET_RAW 	32000

typedef enum {
	CMD_HALTED 				= 0x00,
	CMD_RUNNING 			= 0x01,
	CMD_TORQUE_MODE 		= 0x02,
	CMD_SPEED_MODE_BASE 	= 0x04,
} DriveCommand_t;

typedef enum {
	DRIVE_STATE_UNINIT = 0,
	DRIVE_STATE_HANDSHAKING,
	DRIVE_STATE_OPERATIONAL,
	DRIVE_STATE_COMM_FAILED,
} DriveState_t;

typedef union {
	struct {
		uint8_t overcurrent 		: 1;
		uint8_t overload 			: 1;
		uint8_t overvoltage 		: 1;
		uint8_t undervoltage 		: 1;
		uint8_t ctrl_overheat 		: 1;
		uint8_t motor_overheat 		: 1;
		uint8_t motor_stalled 		: 1;
		uint8_t motor_out_of_phase 	: 1;
	} bits;
	uint8_t all;
} DriveErrorFlags_t;

typedef struct {
	float bus_voltage;
	float bus_current;
	float phase_current;
	int16_t speed;

	int8_t ctrl_temp;
	int8_t motor_temp;
	bool is_running;
	bool is_torque_mode;

	DriveErrorFlags_t errors;
	uint8_t life_signal;
} DriveStatus_t;

typedef struct {
	int16_t target_current;
	int16_t target_speed;
	DriveCommand_t command;
	uint8_t life_counter;
} VCU_Command_t;

typedef struct {
	FDCAN_HandleTypeDef *hfdcan;
	DriveStatus_t status;

	uint8_t mcu_address;
	uint32_t can_id_command;
} DriveController_t;

bool Drive_Init(FDCAN_HandleTypeDef *hfdcan, uint8_t mcu_address);
bool Drive_SendHandshake();
bool Drive_SendCommand(int16_t target_current, int16_t target_speed, DriveCommand_t command);
bool Drive_Stop();
void Drive_Tick(int16_t target_current, int16_t target_speed, DriveCommand_t command);
void Drive_ProcessRx(const FDCAN_RxHeaderTypeDef *hdr, const uint8_t *data);
bool Drive_HasError();
DriveState_t Drive_GetState();


#ifdef __cplusplus
}
#endif

#endif
