/*
 * drive_can.h
 *
 *  Created on: Jan 27, 2026
 *      Author: Steven Dyer
 */

#ifndef INC_DRIVE_CAN_H_
#define INC_DRIVE_CAN_H_

#include "stm32g4xx_hal.h"

typedef enum {
	ADDR_VCU = 		0xD0;
	ADDR_MCU_Base = 0xEF;
	ADDR_BMS_BASE = 0xF3;
	ADDR_GLOBAL = 	0xFF;
} CAN_Address_t;

typedef enum {
	ID_VCU_HANDSHAKE = 	0x0C01EFD0;
	ID_VCU_CONTROL = 	0x0C01EFD0;

	ID_MCU_STATUS_1 = 	0x1801D0EF;
	ID_MCU_STATUS_2 = 	0x1802D0EF;
} CAN_MessageID_t;

typedef enum {
	CMD_HALTED = 			0x00;
	CMD_RUNNING = 			0x01;
	CMD_TORQUE_MODE = 		0x02;
	CMD_SPEED_MODE_BASE = 	0x04;
} ControlCommand_t;



#endif
