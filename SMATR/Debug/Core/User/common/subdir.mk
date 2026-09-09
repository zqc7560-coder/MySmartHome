################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/User/common/delay.c \
../Core/User/common/ringbuf.c 

OBJS += \
./Core/User/common/delay.o \
./Core/User/common/ringbuf.o 

C_DEPS += \
./Core/User/common/delay.d \
./Core/User/common/ringbuf.d 


# Each subdirectory must supply rules for building sources it contributes
Core/User/common/%.o Core/User/common/%.su Core/User/common/%.cyclo: ../Core/User/common/%.c Core/User/common/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Core/User -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -I"E:/STM 32 Project/智能家居项目源码/MySmartHome/SMATR/FreeRTOS" -I"E:/STM 32 Project/智能家居项目源码/MySmartHome/SMATR/FreeRTOS/Inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-User-2f-common

clean-Core-2f-User-2f-common:
	-$(RM) ./Core/User/common/delay.cyclo ./Core/User/common/delay.d ./Core/User/common/delay.o ./Core/User/common/delay.su ./Core/User/common/ringbuf.cyclo ./Core/User/common/ringbuf.d ./Core/User/common/ringbuf.o ./Core/User/common/ringbuf.su

.PHONY: clean-Core-2f-User-2f-common

