################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/User/mqtt/mqtt_client.c 

OBJS += \
./Core/User/mqtt/mqtt_client.o 

C_DEPS += \
./Core/User/mqtt/mqtt_client.d 


# Each subdirectory must supply rules for building sources it contributes
Core/User/mqtt/%.o Core/User/mqtt/%.su Core/User/mqtt/%.cyclo: ../Core/User/mqtt/%.c Core/User/mqtt/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Core/User -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -I"E:/STM 32 Project/智能家居项目源码/MySmartHome/SMATR/FreeRTOS" -I"E:/STM 32 Project/智能家居项目源码/MySmartHome/SMATR/FreeRTOS/Inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-User-2f-mqtt

clean-Core-2f-User-2f-mqtt:
	-$(RM) ./Core/User/mqtt/mqtt_client.cyclo ./Core/User/mqtt/mqtt_client.d ./Core/User/mqtt/mqtt_client.o ./Core/User/mqtt/mqtt_client.su

.PHONY: clean-Core-2f-User-2f-mqtt

