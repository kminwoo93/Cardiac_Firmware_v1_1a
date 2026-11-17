################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/klabc/Downloads/stm32cubewb0-v1-5-0/STM32Cube_FW_WB0_V1.5.0/Projects/NUCLEO-WB05KZ/Applications/BLE/BLE_TransparentMode/Core/Src/system_stm32wb0x.c 

OBJS += \
./Drivers/CMSIS/system_stm32wb0x.o 

C_DEPS += \
./Drivers/CMSIS/system_stm32wb0x.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/CMSIS/system_stm32wb0x.o: C:/Users/klabc/Downloads/stm32cubewb0-v1-5-0/STM32Cube_FW_WB0_V1.5.0/Projects/NUCLEO-WB05KZ/Applications/BLE/BLE_TransparentMode/Core/Src/system_stm32wb0x.c Drivers/CMSIS/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DUSE_FULL_LL_DRIVER -DUSE_HAL_DRIVER -DSTM32WB05 -DCONFIG_NO_HCI_COMMANDS -DNUCLEO_WB05KZ -c -I../../Core/Inc -I../../STM32_BLE/App -I../../STM32_BLE/Target -I../../System/Config/Debug_GPIO -I../../System/Interfaces -I../../System/Modules -I../../System/Modules/Flash -I../../System/Modules/NVMDB/Inc -I../../System/Modules/PKAMGR/Inc -I../../System/Modules/Profiles/Inc -I../../System/Modules/RADIO_utils/Inc -I../../System/Modules/RTDebug -I../../System/Startup -I../../../../../../../Utilities/trace/adv_trace -I../../../../../../../Drivers/STM32WB0x_HAL_Driver/Inc -I../../../../../../../Drivers/STM32WB0x_HAL_Driver/Inc/Legacy -I../../../../../../../Utilities/misc -I../../../../../../../Utilities/sequencer -I../../../../../../../Utilities/lpm/tiny_lpm -I../../../../../../../Middlewares/ST/STM32_BLE -I../../../../../../../Drivers/CMSIS/Device/ST/STM32WB0X/Include -I../../../../../../../Middlewares/ST/STM32_BLE/cryptolib/Inc -I../../../../../../../Middlewares/ST/STM32_BLE/cryptolib/Inc/Common -I../../../../../../../Middlewares/ST/STM32_BLE/cryptolib/Inc/AES -I../../../../../../../Middlewares/ST/STM32_BLE/cryptolib/Inc/AES/CBC -I../../../../../../../Middlewares/ST/STM32_BLE/cryptolib/Inc/AES/CMAC -I../../../../../../../Middlewares/ST/STM32_BLE/cryptolib/Inc/AES/Common -I../../../../../../../Middlewares/ST/STM32_BLE/cryptolib/Inc/AES/ECB -I../../../../../../../Middlewares/ST/STM32_BLE/evt_handler/inc -I../../../../../../../Middlewares/ST/STM32_BLE/queued_writes/inc -I../../../../../../../Middlewares/ST/STM32_BLE/stack/include -I../../../../../../../Drivers/CMSIS/Include -I../../../../../../../Drivers/BSP/STM32WB0x-nucleo -I../../Middlewares/ST/STM32_BLE/cryptolib/Inc -I../../../../../../../Projects/Common/BLE/Interfaces -I../../../../../../../Projects/Common/BLE/Modules -I../../../../../../../Projects/Common/BLE/Modules/RTDebug -I../../../../../../../Projects/Common/BLE/Modules/RADIO_utils/Inc -I../../../../../../../Projects/Common/BLE/Modules/Profiles/Inc -I../../../../../../../Projects/Common/BLE/Modules/PKAMGR/Inc -I../../../../../../../Projects/Common/BLE/Modules/NVMDB/Inc -I../../../../../../../Projects/Common/BLE/Modules/Flash -I../../../../../../../Projects/Common/BLE/Startup -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Drivers-2f-CMSIS

clean-Drivers-2f-CMSIS:
	-$(RM) ./Drivers/CMSIS/system_stm32wb0x.cyclo ./Drivers/CMSIS/system_stm32wb0x.d ./Drivers/CMSIS/system_stm32wb0x.o ./Drivers/CMSIS/system_stm32wb0x.su

.PHONY: clean-Drivers-2f-CMSIS

