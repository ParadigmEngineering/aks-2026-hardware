################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/CustomDrivers/Src/fdcan_test.c \
../Drivers/CustomDrivers/Src/uart_test.c 

OBJS += \
./Drivers/CustomDrivers/Src/fdcan_test.o \
./Drivers/CustomDrivers/Src/uart_test.o 

C_DEPS += \
./Drivers/CustomDrivers/Src/fdcan_test.d \
./Drivers/CustomDrivers/Src/uart_test.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CustomDrivers/Src/%.o Drivers/CustomDrivers/Src/%.su Drivers/CustomDrivers/Src/%.cyclo: ../Drivers/CustomDrivers/Src/%.c Drivers/CustomDrivers/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G474xx -c -I../Core/Inc -I../Drivers/CustomDrivers/Inc -I../Drivers/STM32G4xx_HAL_Driver/Inc -I../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-CustomDrivers-2f-Src

clean-Drivers-2f-CustomDrivers-2f-Src:
	-$(RM) ./Drivers/CustomDrivers/Src/fdcan_test.cyclo ./Drivers/CustomDrivers/Src/fdcan_test.d ./Drivers/CustomDrivers/Src/fdcan_test.o ./Drivers/CustomDrivers/Src/fdcan_test.su ./Drivers/CustomDrivers/Src/uart_test.cyclo ./Drivers/CustomDrivers/Src/uart_test.d ./Drivers/CustomDrivers/Src/uart_test.o ./Drivers/CustomDrivers/Src/uart_test.su

.PHONY: clean-Drivers-2f-CustomDrivers-2f-Src

