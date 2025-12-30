#ifndef UART_TEST_H
#define UART_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g474xx.h"

//FUNCTION PROTOTYPES
void initClocks();
void configPinMode();
void setAF();
void configureUart1Pins();
void configureUart1();
void USART1_IRQHandler()
uint8_t receiveUart();
void transmitUart(uint8_t data);
void initUART();

#ifdef __cplusplus
}
#endif

#endif

