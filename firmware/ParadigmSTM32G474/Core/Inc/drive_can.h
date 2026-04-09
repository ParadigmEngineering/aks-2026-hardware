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

#define CAN_ID_VCU_TO_MCU 	0x0C01EFD0
#define CAN_ID_MCU_STATUS1 	0x1801D0EF
#define CAN_ID_MCU_STATUS2 	0x1802D0EF

#define MCU_HANDSHAKE_BYTE 	0x55
#define VCU_HANDSHAKE_BYTE 	0xAA

#define DRIVE_POLL_MS 			50
#define DRIVE_TIMEOUT_COUNT 	10
#define DRIVE_LIFESIG_FAIL_MAX 	5

#define CURRENT_OFFSET_RAW 	32000
#define SPEED_OFFSET_RAW 	32000

typedef enum {
	CMD_HALTED 			= 0x00,
	CMD_RUNNING 		= 0x01,
	CMD_SPEED_MODE		= 0x02,
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
} DriveErrorByte3_t;

typedef union {
	struct {
		uint8_t motor_sensor 		: 1;
		uint8_t motor_aux_sensor 	: 1;
		uint8_t encoder_misaligned 	: 1;
		uint8_t anti_runaway 		: 1;
		uint8_t main_accelerator 	: 1;
		uint8_t aux_accelerator 	: 1;
		uint8_t pre_charge 			: 1;
		uint8_t dc_contactor 		: 1;
	} bits;
	uint8_t all;
} DriveErrorByte4_t;

typedef union {
	struct {
		uint8_t power_valve : 1;
		uint8_t current_sensor : 1;
		uint8_t auto_tune : 1;
		uint8_t rs485 : 1;
		uint8_t can : 1;
		uint8_t software : 1;
		uint8_t reserved : 2;
	} bits;
	uint8_t all;
} DriveErrorByte5_t;

typedef struct {
	DriveErrorByte3_t b3;
	DriveErrorByte4_t b4;
	DriveErrorByte5_t b5;
} DriveErrors_t;

typedef struct {
	float bus_voltage;
	float bus_current;
	float phase_current;
	float speed;

	float ctrl_temp;
	float motor_temp;
	bool is_running;
	bool is_torque_mode;

	DriveErrors_t errors;
	uint8_t life_signal;

	uint32_t last_status1_tick;
	uint32_t last_status2_tick;
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
	DriveState_t state;

	uint8_t mcu_address;
	uint8_t life_counter; // Increment on every TX command
	uint8_t missed_rx_count; // Consecutive missed Status1 frames
	uint8_t life_fail_count; // Consecutive life signal failures
	uint8_t last_mcu_life_signal; // HAL_GetTick() of last 50 ms send

	uint32_t last_tx_tick;
} DriveController_t;

bool Drive_Init(FDCAN_HandleTypeDef *hfdcan, uint8_t mcu_address);
bool Drive_SendHandshake();
bool Drive_SendCommand(int16_t target_current, int16_t target_speed, DriveCommand_t command);
bool Drive_Stop();
void Drive_Tick(int16_t target_current, int16_t target_speed, DriveCommand_t command);
void Drive_ProcessRx(const FDCAN_RxHeaderTypeDef *hdr, const uint8_t *data);
bool Drive_HasError();
DriveState_t Drive_GetState();

#define DRIVE_ENCODE_CURRENT(amps) 	((uint16_t)(((amps) / 0.1) + CURRENT_OFFSET_RAW))
#define DRIVE_ENCODE_SPEED(rpm) 	((uint16_t)((rpm) + SPEED_OFFSET_RAW))


#ifdef __cplusplus
}
#endif

#endif
