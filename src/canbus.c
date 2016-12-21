#include "stm32f0xx_hal.h"
#include "usbd_virtualcdc.h" 
#include "canconfig.h"

/*
    CANbus sniffer using STM32F042

    Copyright (C) 2015,2016 Peter Lawrence

    Permission is hereby granted, free of charge, to any person obtaining a 
    copy of this software and associated documentation files (the "Software"), 
    to deal in the Software without restriction, including without limitation 
    the rights to use, copy, modify, merge, publish, distribute, sublicense, 
    and/or sell copies of the Software, and to permit persons to whom the 
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in 
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
    DEALINGS IN THE SOFTWARE.
*/

/*
    Theory of operation:

    Routines are initialized with a call to CANbus_Init() and regular calls to CANbus_Service() whenever the CPU is idle.

    ST's CAN driver calls HAL_CAN_RxCpltCallback() and HAL_CAN_ErrorCallback().
    The former indicates a received packet; the latter indicates some sort of error condition.

    The HAL_CAN_RxCpltCallback() implementation writes the data into a queue (CANqueue[]) and re-enables reception ASAP.

    CANbus_Service() services the queue, converts it to LAWICEL protocol form, and outputs it to the virtual CDC routines.

    Data collection (outputting of CAN messages via virtual CDC serial port) is enabled only when DTR is active (CDC_SET_CONTROL_LINE_STATE).
*/

#define CANQUEUE_SIZE 128 /* how many entries in the CAN queue; chosen to use as much RAM as we can afford */
#define ERROR_CONDITION() __BKPT()

struct CANmessage
{
  uint32_t Id;
  uint8_t flags;
  uint8_t DLC;
  uint8_t Data[8];
};

static CAN_HandleTypeDef CanHandle;
static struct CANmessage CANqueue[CANQUEUE_SIZE];
static uint32_t CANqueue_write_index, CANqueue_read_index;
static uint32_t collection_active;

static void CAN_Receive(void);
static void CAN_Config(void);

void CANbus_Init(void)
{
  /* initialize the queue to empty */
  CANqueue_write_index = CANqueue_read_index = 0;

  collection_active = 0;

  CAN_Config();

  /* 'prime the pump' for CAN messages */
  CAN_Receive();

}

static void CAN_Config(void)
{
  CAN_FilterConfTypeDef  sFilterConfig;
  static CanRxMsgTypeDef RxMessage;

  CanHandle.Instance = CANx;
  CanHandle.pTxMsg = NULL;
  CanHandle.pRxMsg = &RxMessage;

  CanHandle.Init.TTCM = DISABLE;
  CanHandle.Init.ABOM = DISABLE;
  CanHandle.Init.AWUM = DISABLE;
  CanHandle.Init.NART = DISABLE;
  CanHandle.Init.RFLM = DISABLE;
  CanHandle.Init.TXFP = DISABLE;
  CanHandle.Init.Mode = CAN_MODE_SILENT;
  CanHandle.Init.SJW = CAN_SJW_1TQ;
  /* 500k */
  CanHandle.Init.BS1 = CAN_BS1_5TQ;
  CanHandle.Init.BS2 = CAN_BS2_6TQ;
  CanHandle.Init.Prescaler = 8;

  if (HAL_CAN_Init(&CanHandle) != HAL_OK)
    ERROR_CONDITION();

  /* set filter to receive all */
  sFilterConfig.FilterNumber = 0;
  sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
  sFilterConfig.FilterIdHigh = 0x0000;
  sFilterConfig.FilterIdLow = 0x0000;
  sFilterConfig.FilterMaskIdHigh = 0x0000;
  sFilterConfig.FilterMaskIdLow = 0x0000;
  sFilterConfig.FilterFIFOAssignment = 0;
  sFilterConfig.FilterActivation = ENABLE;
  sFilterConfig.BankNumber = 14;

  if (HAL_CAN_ConfigFilter(&CanHandle, &sFilterConfig) != HAL_OK)
    ERROR_CONDITION();
}

void HAL_CAN_RxCpltCallback(CAN_HandleTypeDef *CanHandle)
{
  uint32_t next_write_index, index;

  if (collection_active)
  {
    next_write_index = CANqueue_write_index + 1;
    if (CANQUEUE_SIZE == next_write_index)
      next_write_index = 0;
  
    if (next_write_index != CANqueue_read_index) /* only write if space left in queue */
    {
      CANqueue[CANqueue_write_index].Id = CanHandle->pRxMsg->StdId;
      CANqueue[CANqueue_write_index].flags = (CAN_ID_STD == CanHandle->pRxMsg->IDE) ? 0x01: 0x00;
      CANqueue[CANqueue_write_index].DLC = CanHandle->pRxMsg->DLC;
      for (index = 0; index < CANqueue[CANqueue_write_index].DLC; index++)
        CANqueue[CANqueue_write_index].Data[index] = CanHandle->pRxMsg->Data[index]; /* ST's CAN driver stores byte data in uint32_t for some unexplained reason */
      CANqueue_write_index = next_write_index;
    }
  }

  CAN_Receive();
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
  /* acknowledge the peripheral's error */
  hcan->Instance->MSR = CAN_MSR_ERRI;
}

static void CAN_Receive(void)
{
  /* request to receive another CAN message */
  if (HAL_CAN_Receive_IT(&CanHandle, CAN_FIFO0) != HAL_OK)
    ERROR_CONDITION();
}

void CANbus_Service(void)
{
  uint32_t read_index, write_index;
  static char scratchpad[1 /* start char */ + 8 /* extendedId */ + 1 /* DLC */ + 16 /* data */ + 1 /* CR */];
  unsigned length, index;
  static const char hexdigits[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
  struct CANmessage *pnt;

  if (!collection_active)
  {
    __disable_irq();
    CANqueue_read_index = CANqueue_write_index = 0;
    __enable_irq();
    return;
  }

  /* make snapshot of CANqueue state */
  __disable_irq();
  read_index = CANqueue_read_index;
  write_index = CANqueue_write_index;
  __enable_irq();

  while (read_index != write_index)
  {
    pnt = &CANqueue[read_index];
    length = 0;

    if (pnt->DLC <= 8)
    {
      if (pnt->flags)
      {
        scratchpad[length++] = 't';
        scratchpad[length++] = hexdigits[(pnt->Id >> 8) & 0xF];
        scratchpad[length++] = hexdigits[(pnt->Id >> 4) & 0xF];
        scratchpad[length++] = hexdigits[(pnt->Id >> 0) & 0xF];
      }
      else
      {
        scratchpad[length++] = 'T';
        scratchpad[length++] = hexdigits[(pnt->Id >> 28) & 0xF];
        scratchpad[length++] = hexdigits[(pnt->Id >> 24) & 0xF];
        scratchpad[length++] = hexdigits[(pnt->Id >> 20) & 0xF];
        scratchpad[length++] = hexdigits[(pnt->Id >> 16) & 0xF];
        scratchpad[length++] = hexdigits[(pnt->Id >> 12) & 0xF];
        scratchpad[length++] = hexdigits[(pnt->Id >> 8) & 0xF];
        scratchpad[length++] = hexdigits[(pnt->Id >> 4) & 0xF];
        scratchpad[length++] = hexdigits[(pnt->Id >> 0) & 0xF];
      }

      scratchpad[length++] = hexdigits[pnt->DLC & 0xF];

      for (index = 0; index < pnt->DLC; index++)
      {
        scratchpad[length++] = hexdigits[(pnt->Data[index] >> 4) & 0xF];
        scratchpad[length++] = hexdigits[(pnt->Data[index] >> 0) & 0xF];
      }

      scratchpad[length++] = 13; /* CR */

      /* bail loop if the buffer to the PC is too full */
      if (0 == USBD_VirtualCDC_DataOut_Append(scratchpad, length))
        break;
    }

    /* calculate next read index */
    read_index++;
    if (read_index == CANQUEUE_SIZE)
      read_index = 0;

    /* update read index as atomic operation */
    __disable_irq();
    CANqueue_read_index = read_index;
    __enable_irq();
  }
}

void CANx_RX_IRQHandler(void) /* using the macro defined in canconfig.h, provide a wrapper for the CAN IRQ routine */
{
  HAL_CAN_IRQHandler(&CanHandle);
}

/* this handler of CDC_SET_CONTROL_LINE_STATE enables/disables collection */

void USBD_VirtualCDC_LineState(uint16_t state)
{
  collection_active = (state & 1);
}
