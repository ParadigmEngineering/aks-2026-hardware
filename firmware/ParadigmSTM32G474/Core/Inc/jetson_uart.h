/*
 * jetson_uart.h
 *
 *  Created on: Apr 28, 2026
 *      Author: Steven Dyer
 */

#ifndef INC_JETSON_UART_H_
#define INC_JETSON_UART_H_

#include <stdint.h>
#include <stdbool.h>

#define JETSON_FLAG_AUTONOMOUS_ENABLE 	(1u << 0)
#define JETSON_FLAG_ESTOP_REQUEST 		(1u << 1)
#define JETSON_FLAG_VALID_PATH			(1u << 2)

typedef struct {
	uint16_t throttle;
	uint16_t steering;
	uint8_t flags;
	uint8_t sequence;
	uint32_t last_rx_ms;
	bool valid;
} JetsonCommand_t;

void Jetson_Init();
void Jetson_ProcessByte(uint8_t byte);
void Jetson_Tick();
bool Jetson_CommandIsFresh(uint32_t timeout_ms);
bool Jetson_CommandAllowsAutonomous();
bool Jetson_GetLatestCommand(JetsonCommand_t *cmd);

#endif /* INC_JETSON_UART_H_ */
