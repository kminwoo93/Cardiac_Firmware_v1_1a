################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../STM32WB05N/Target/hci_tl_interface.c 

OBJS += \
./STM32WB05N/Target/hci_tl_interface.o 

C_DEPS += \
./STM32WB05N/Target/hci_tl_interface.d 


# Each subdirectory must supply rules for building sources it contributes
STM32WB05N/Target/%.o STM32WB05N/Target/%.su STM32WB05N/Target/%.cyclo: ../STM32WB05N/Target/%.c STM32WB05N/Target/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DTX_INCLUDE_USER_DEFINE_FILE -DTX_SINGLE_MODE_NON_SECURE=1 -DUX_INCLUDE_USER_DEFINE_FILE -DUSE_HAL_DRIVER -DSTM32U5A5xx -c -I../Core/Inc -I../AZURE_RTOS/App -I../USBX/App -I../USBX/Target -I../Drivers/STM32U5xx_HAL_Driver/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc/Legacy -I../Middlewares/ST/threadx/common/inc -I../Drivers/CMSIS/Device/ST/STM32U5xx/Include -I../Middlewares/ST/threadx/ports/cortex_m33/gnu/inc -I../Middlewares/ST/usbx/common/core/inc -I../Middlewares/ST/usbx/ports/generic/inc -I../Middlewares/ST/usbx/common/usbx_stm32_device_controllers -I../Middlewares/ST/usbx/common/usbx_device_classes/inc -I../Drivers/CMSIS/Include -I../STM32WB05N/Target -I../Middlewares/ST/STM32WB05N/includes -I../Middlewares/ST/STM32WB05N/utils -I../Middlewares/ST/STM32WB05N/hci/hci_tl_patterns/Basic -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-STM32WB05N-2f-Target

clean-STM32WB05N-2f-Target:
	-$(RM) ./STM32WB05N/Target/hci_tl_interface.cyclo ./STM32WB05N/Target/hci_tl_interface.d ./STM32WB05N/Target/hci_tl_interface.o ./STM32WB05N/Target/hci_tl_interface.su

.PHONY: clean-STM32WB05N-2f-Target

