#ifndef FDCAN_TEST_H
#define FDCAN_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g474xx.h"

// FUNCATION PROTOTYPES
void initFDCANClocks();
void configFDCANPinMode();
void setFDCANAF();

#ifdef __cplusplus
}
#endif

#endif
