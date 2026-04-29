/*
 * jetson_uart.c
 *
 *  Created on: Apr 28, 2026
 *      Author: Steven Dyer
 */

#include "jetson_uart.h"
#include "main.h"
#include <stddef.h>

/*
 * Packet format:
 *
 * Byte 0: 0xAA
 * Byte 1: 0x55
 * Byte 2: Message Id = 0x01
 * Byte 3: Payload length = 0x06
 * Byte 4: Sequence (increments from 0 to 255, check if jetson commands are actually changing)
 * Byte 5: Throttle low byte
 * Byte 6: Throttle high byte
 * Byte 7: Steering low byte
 * Byte 8: Steering high byte
 * Byte 9: Flags
 * Byte 10: Checksum
 */

#define JETSON_START_1 0xAA
#define JETSON_START_2 0x55

#define JETSON_MSG_DRIVE_CMD 0x01

#define JETSON_PAYLOAD_LENGTH 		6
#define JETSON_MAX_PAYLOAD_LENGTH 	16

#define JETSON_CMD_MIN 		1000
#define JETSON_CMD_NEUTRAL 	1500
#define JETSON_CMD_MAX 		2000

#define JETSON_TIMEOUT_US 100

typedef enum {
	PARSER_WAIT_START_1 = 0,
	PARSER_WAIT_START_2,
	PARSER_READ_MSG_ID,
	PARSER_READ_LENGTH,
	PARSER_READ_PAYLOAD,
	PARSER_READ_CHECKSUM
} JetsonParserState_t;

typedef struct {
	JetsonParserState_t state;
	uint8_t msg_id;
	uint8_t length;
	uint8_t payload[JETSON_MAX_PAYLOAD_LENGTH];
	uint8_t index;
	uint8_t checksum;
} JetsonParser_t;

static JetsonParser_t parser;
static volatile JetsonCommand_t latest_cmd;

static uint16_t read_u16_le(const uint8_t *data) {
	return (uint16_t)((uint16_t)data[0] | (uint16_t)data[1] << 8);
}

static bool command_in_range(uint16_t value) {
	return (value >=JETSON_CMD_MIN && value <=JETSON_CMD_MAX);
}

static void Jetson_HandlePacket(uint8_t msg_id, const uint8_t *payload, uint8_t length) {
	if (msg_id != JETSON_MSG_DRIVE_CMD) {
		return;
	}

	if (length != JETSON_PAYLOAD_LENGTH) {
		return;
	}

	uint8_t sequence = payload[0];

	uint16_t throttle = read_u16_le(&payload[1]);
	uint16_t steering = read_u16_le(&payload[3]);

	uint8_t flags = payload[5];

	/*
	 * Reject invalid commands instead of clamping
	 * If Jetson accidentally sends 9000, STM should not turn it into 2000
	 */
	if (!command_in_range(throttle)) {
		latest_cmd.valid = false;
		return;
	}

	if (!command_in_range(steering)) {
		latest_cmd.valid = false;
		return;
	}

	latest_cmd.sequence = sequence;
	latest_cmd.throttle = throttle;
	latest_cmd.steering = steering;
	latest_cmd.flags = flags;
	latest_cmd.last_rx_ms = HAL_GetTick();
	latest_cmd.valid = true;
}

void Jetson_Init() {
    parser.state = PARSER_WAIT_START_1;
    parser.msg_id = 0;
    parser.length = 0;
    parser.index = 0;
    parser.checksum = 0;

    latest_cmd.throttle = JETSON_CMD_NEUTRAL;
    latest_cmd.steering = JETSON_CMD_NEUTRAL;
    latest_cmd.flags = 0;
    latest_cmd.sequence = 0;
    latest_cmd.last_rx_ms = 0;
    latest_cmd.valid = false;
}

void Jetson_ProcessByte(uint8_t byte) {
	switch (parser.state) {
		case PARSER_WAIT_START_1:
			if (byte == JETSON_START_1) {
				parser.checksum = byte;
				parser.state = PARSER_WAIT_START_2;
			}

			break;

		case PARSER_WAIT_START_2:
			if (byte == JETSON_START_2) {
				parser.checksum ^= byte;
				parser.state = PARSER_READ_MSG_ID;
			} else if (byte == JETSON_START_1) {
                //Stay partially synchronized if another 0xAA arrives.
                parser.checksum = byte;
                parser.state = PARSER_WAIT_START_2;
            } else {
                parser.state = PARSER_WAIT_START_1;
            }

            break;

		case PARSER_READ_MSG_ID:
			parser.msg_id = byte;
			parser.checksum ^= byte;
			parser.state = PARSER_READ_LENGTH;

			break;

		case PARSER_READ_LENGTH:
			parser.length = byte;
			parser.checksum ^= byte;

			if (parser.length == 0 || parser.length > JETSON_MAX_PAYLOAD_LENGTH) {
				parser.state = PARSER_WAIT_START_1;
			} else {
				parser.index = 0;
				parser.state = PARSER_READ_PAYLOAD;
			}

			break;

		case PARSER_READ_PAYLOAD:
			parser.payload[parser.index] = byte;
			parser.index++;
			parser.checksum ^= byte;

			if (parser.index >= parser.length) {
				parser.state = PARSER_READ_CHECKSUM;
			}

			break;

		case PARSER_READ_CHECKSUM:
			if (byte == parser.checksum) {
				Jetson_HandlePacket(parser.msg_id, parser.payload, parser.length);
			}

			//Whether checksum passes or fails, restart parser
			parser.state = PARSER_WAIT_START_1;

			break;

		default:
			parser.state = PARSER_WAIT_START_1;

			break;
	}
}


bool Jetson_CommandIsFresh(uint32_t timeout_us) {
	if (!latest_cmd.valid) {
		return false;
	}

	return ((HAL_GetTick() - latest_cmd.last_rx_ms) <= timeout_us);
}

bool Jetson_CommandAllowAutonomous() {
	if (!Jetson_CommandIsFresh(JETSON_TIMEOUT_US)) {
		return false;
	}

	if (!(latest_cmd.flags & JETSON_FLAG_AUTONOMOUS_ENABLE)) {
		return false;
	}

	if (latest_cmd.flags & JETSON_FLAG_ESTOP_REQUEST) {
		return false;
	}

	if (!(latest_cmd.flags & JETSON_FLAG_VALID_PATH)) {
		return false;
	}

	return true;
}

bool Jetson_GetLatestCommand(JetsonCommand_t *cmd)
{
    if (cmd == NULL) {
        return false;
    }

    if (!latest_cmd.valid) {
        return false;
    }

    //__disable_irq();

    cmd->throttle = latest_cmd.throttle;
    cmd->steering = latest_cmd.steering;
    cmd->flags = latest_cmd.flags;
    cmd->sequence = latest_cmd.sequence;
    cmd->last_rx_ms = latest_cmd.last_rx_ms;
    cmd->valid = latest_cmd.valid;

    //__enable_irq();

    return true;
}



