################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/User/debug/debug.c 

OBJS += \
./Core/User/debug/debug.o 

C_DEPS += \
./Core/User/debug/debug.d 


# Each subdirectory must supply rules for building sources it contributes
Core/User/debug/%.o Core/User/debug/%.su Core/User/debug/%.cyclo: ../Core/User/debug/%.c Core/User/debug/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Core/User -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -I"E:/STM 32 Project/智能家居项目源码/MySmartHome/SMATR/FreeRTOS" -I"E:/STM 32 Project/智能家居项目源码/MySmartHome/SMATR/FreeRTOS/Inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-User-2f-debug

clean-Core-2f-User-2f-debug:
	-$(RM) ./Core/User/debug/debug.cyclo ./Core/User/debug/debug.d ./Core/User/debug/debug.o ./Core/User/debug/debug.su

.PHONY: clean-Core-2f-User-2f-debug

