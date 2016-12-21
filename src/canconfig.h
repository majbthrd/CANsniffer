#ifndef CANCONFIG_H_
#define CANCONFIG_H_

#include "stm32f0xx_hal.h"

/* configuration chosen to utilize PB9 and PB8 on STM32F072DISCOVERY */

#define CANx                            CAN
#define CANx_CLK_ENABLE()               __CAN_CLK_ENABLE()
#define CANx_GPIO_CLK_ENABLE()          __GPIOB_CLK_ENABLE()

#define CANx_FORCE_RESET()              __CAN_FORCE_RESET()
#define CANx_RELEASE_RESET()            __CAN_RELEASE_RESET()

#define CANx_TX_PIN                    GPIO_PIN_9
#define CANx_TX_GPIO_PORT              GPIOB
#define CANx_TX_AF                     GPIO_AF4_CAN
#define CANx_RX_PIN                    GPIO_PIN_8
#define CANx_RX_GPIO_PORT              GPIOB
#define CANx_RX_AF                     GPIO_AF4_CAN

#define CANx_RX_IRQn                   CEC_CAN_IRQn
#define CANx_RX_IRQHandler             CEC_CAN_IRQHandler

#endif
