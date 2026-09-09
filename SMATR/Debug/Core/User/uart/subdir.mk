################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/User/uart/uart_config.c 

OBJS += \
./Core/User/uart/uart_config.o 

C_DEPS += \
./Core/User/uart/uart_config.d 


# Each subdirectory must supply rules for building sources it contributes
Core/User/uart/%.o Core/User/uart/%.su Core/User/uart/%.cyclo: ../Core/User/uart/%.c Core/User/uart/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Core/User -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -I"E:/STM 32 Project/智能家居项目源码/MySmartHome/SMATR/FreeRTOS" -I"E:/STM 32 Project/智能家居项目源码/MySmartHome/SMATR/FreeRTOS/Inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-User-2f-uart

clean-Core-2f-User-2f-uart:
	-$(RM) ./Core/User/uart/uart_config.cyclo ./Core/User/uart/uart_config.d ./Core/User/uart/uart_config.o ./Core/User/uart/uart_config.su

.PHONY: clean-Core-2f-User-2f-uart

