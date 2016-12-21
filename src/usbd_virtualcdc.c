/*
    virtual CDC for STM32F0xx

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

#include "usbd_virtualcdc.h"
#include "usbd_desc.h"

/* USB handle declared in main.c */
extern USBD_HandleTypeDef USBD_Device;

/* local function prototyping */

static uint8_t USBD_CDC_Init (USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_CDC_DeInit (USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_CDC_Setup (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_CDC_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_CDC_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_CDC_EP0_RxReady (USBD_HandleTypeDef *pdev);
static const uint8_t *USBD_CDC_GetFSCfgDesc (uint16_t *length);
static uint8_t USBD_CDC_SOF (USBD_HandleTypeDef *pdev);
static USBD_StatusTypeDef USBD_CDC_ReceivePacket (USBD_HandleTypeDef *pdev);
static USBD_StatusTypeDef USBD_CDC_TransmitPacket (USBD_HandleTypeDef *pdev, uint16_t offset, uint16_t length);
static int8_t CDC_Itf_Control (USBD_CDC_HandleTypeDef *hcdc, uint8_t cmd, uint8_t* pbuf, uint16_t length);

/* CDC interface class callbacks structure that is used by main.c */
const USBD_ClassTypeDef USBD_CDC = 
{
  .Init                  = USBD_CDC_Init,
  .DeInit                = USBD_CDC_DeInit,
  .Setup                 = USBD_CDC_Setup,
  .EP0_TxSent            = NULL,
  .EP0_RxReady           = USBD_CDC_EP0_RxReady,
  .DataIn                = USBD_CDC_DataIn,
  .DataOut               = USBD_CDC_DataOut,
  .SOF                   = USBD_CDC_SOF,
  .IsoINIncomplete       = NULL,
  .IsoOUTIncomplete      = NULL,     
  .GetFSConfigDescriptor = USBD_CDC_GetFSCfgDesc,    
};

/* context for each and every UART managed by this CDC implementation */
static USBD_CDC_HandleTypeDef context;

static uint8_t USBD_CDC_Init (USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  /* Open EP IN */
  USBD_LL_OpenEP(pdev, CDC_EP_DATAIN, USBD_EP_TYPE_BULK, USB_FS_MAX_PACKET_SIZE);
  
  /* Open EP OUT */
  USBD_LL_OpenEP(pdev, CDC_EP_DATAOUT, USBD_EP_TYPE_BULK, USB_FS_MAX_PACKET_SIZE);

  /* Open Command IN EP */
  USBD_LL_OpenEP(pdev, CDC_EP_COMMAND, USBD_EP_TYPE_INTR, CDC_CMD_PACKET_SIZE);
  
  /* initialize the context */
  context.InboundBufferReadIndex = context.InboundBufferWriteIndex = 0;
  context.InboundTransferInProgress = 0;
  context.OutboundTransferNeedsRenewal = 0;
   
  /* Prepare Out endpoint to receive next packet */
  USBD_CDC_ReceivePacket(pdev);

  return USBD_OK;
}

static uint8_t USBD_CDC_DeInit (USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  /* Close EP IN */
  USBD_LL_CloseEP(pdev, CDC_EP_DATAIN);

  /* Close EP OUT */
  USBD_LL_CloseEP(pdev, CDC_EP_DATAOUT);

  /* Close Command IN EP */
  USBD_LL_CloseEP(pdev, CDC_EP_COMMAND);

  return USBD_OK;
}

static uint8_t USBD_CDC_Setup (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  if (CDC_ITF_COMMAND == req->wIndex)
  {
    switch (req->bmRequest & USB_REQ_TYPE_MASK)
    {
    case USB_REQ_TYPE_CLASS :
      if (req->wLength)
      {
        if (req->bmRequest & 0x80)
        {
          CDC_Itf_Control(&context, req->bRequest, (uint8_t *)context.SetupBuffer, req->wLength);
          USBD_CtlSendData (pdev, (uint8_t *)context.SetupBuffer, req->wLength);
        }
        else
        {
          context.CmdOpCode = req->bRequest;
          context.CmdLength = req->wLength;
      
          USBD_CtlPrepareRx (pdev, (uint8_t *)context.SetupBuffer, req->wLength);
        }
      }
      else
      {
          CDC_Itf_Control(&context, req->bRequest, NULL, req->wValue); /* substitute wValue for wLength */
      }
      break;

    default: 
      break;
    }
  }

  return USBD_OK;
}

static uint8_t USBD_CDC_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  if (CDC_EP_DATAIN == (epnum | 0x80))
  {
    context.InboundTransferInProgress = 0;
  }

  return USBD_OK;
}

static uint8_t USBD_CDC_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  uint32_t RxLength;

  if (CDC_EP_DATAOUT == epnum)
  {
    /* Get the received data length */
    RxLength = USBD_LL_GetRxDataSize (pdev, epnum);

    /* transmit (device to PC Host) only: throw away data and set flag to allow new dataout */
    context.OutboundTransferNeedsRenewal = 1;
  }

  return USBD_OK;
}

static uint8_t USBD_CDC_SOF (USBD_HandleTypeDef *pdev)
{
  uint32_t buffsize;

  if(context.InboundBufferReadIndex != context.InboundBufferWriteIndex)
  {
    if(context.InboundBufferReadIndex > context.InboundBufferWriteIndex)
    {
      /* write index has looped around, so send partial data from the write index to the end of the buffer */
      buffsize = INBOUND_BUFFER_SIZE - context.InboundBufferReadIndex;
    }
    else 
    {
      /* send all data between read index and write index */
      buffsize = context.InboundBufferWriteIndex - context.InboundBufferReadIndex;
    }

    if(USBD_CDC_TransmitPacket(pdev, context.InboundBufferReadIndex, buffsize) == USBD_OK)
    {
      context.InboundBufferReadIndex += buffsize;
      /* if we've reached the end of the buffer, loop around to the beginning */
      if (context.InboundBufferReadIndex == INBOUND_BUFFER_SIZE)
      {
        context.InboundBufferReadIndex = 0;
      }
    }
  }

  if (context.OutboundTransferNeedsRenewal) /* if there is a lingering request needed due to a HAL_BUSY, retry it */
    USBD_CDC_ReceivePacket(pdev);

  return USBD_OK;
}

static uint8_t USBD_CDC_EP0_RxReady (USBD_HandleTypeDef *pdev)
{ 
  if (CDC_ITF_COMMAND != pdev->request.wIndex)
  {
    if (context.CmdOpCode != 0xFF)
    {
      CDC_Itf_Control(&context, context.CmdOpCode, (uint8_t *)context.SetupBuffer, context.CmdLength);
      context.CmdOpCode = 0xFF; 
    }
  }

  return USBD_OK;
}

static const uint8_t *USBD_CDC_GetFSCfgDesc (uint16_t *length)
{
  *length = USBD_CfgFSDesc_len;
  return USBD_CfgFSDesc_pnt;
}

uint8_t USBD_CDC_RegisterInterface  (USBD_HandleTypeDef   *pdev)
{
  return USBD_OK;
}

static uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev, uint16_t offset, uint16_t length)
{      
  USBD_StatusTypeDef outcome;

  if (context.InboundTransferInProgress)
    return USBD_BUSY;

  /* Transmit next packet */
  outcome = USBD_LL_Transmit(pdev, CDC_EP_DATAIN, (uint8_t *)(context.InboundBuffer) + offset, length);
  
  if (USBD_OK == outcome)
  {
    /* Tx Transfer in progress */
    context.InboundTransferInProgress = 1;
  }

  return outcome;
}

static uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *pdev)
{
  USBD_StatusTypeDef outcome;

  outcome = USBD_LL_PrepareReceive(pdev, CDC_EP_DATAOUT, (uint8_t *)context.OutboundBuffer, CDC_DATA_OUT_MAX_PACKET_SIZE);

  context.OutboundTransferNeedsRenewal = (USBD_OK != outcome); /* set if the HAL was busy so that we know to retry it */

  return outcome;
}

static int8_t CDC_Itf_Control (USBD_CDC_HandleTypeDef *hcdc, uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  /* when pbuf == NULL, length = wValue rather than length */

  switch (cmd)
  {
  case CDC_SET_CONTROL_LINE_STATE:
    USBD_VirtualCDC_LineState(length);
    break;
  }
  
  return USBD_OK;
}

void USBD_CDC_PMAConfig(PCD_HandleTypeDef *hpcd, uint32_t *pma_address)
{
  /* allocate PMA memory for all endpoints associated with CDC */
  HAL_PCDEx_PMAConfig(hpcd, CDC_EP_DATAIN,  PCD_SNG_BUF, *pma_address =+ CDC_DATA_IN_MAX_PACKET_SIZE);
  HAL_PCDEx_PMAConfig(hpcd, CDC_EP_DATAOUT, PCD_SNG_BUF, *pma_address =+ CDC_DATA_OUT_MAX_PACKET_SIZE);
  HAL_PCDEx_PMAConfig(hpcd, CDC_EP_COMMAND,  PCD_SNG_BUF, *pma_address =+ CDC_CMD_PACKET_SIZE);
}

uint32_t USBD_VirtualCDC_DataOut_Append(const uint8_t *data, uint32_t length)
{
  uint32_t write_index, next_write_index, read_index, countdown;
  uint8_t *wpnt;
  unsigned overrun = 0;

  /* sample current state of inbound buffer */
  __disable_irq();
  write_index = context.InboundBufferWriteIndex;
  read_index = context.InboundBufferReadIndex;
  __enable_irq();

  /* try to write data to inbound buffer, bailing early if we run out of space (overrun = 1) */
  countdown = length;
  while (countdown)
  {
    next_write_index = write_index + 1;
    if (next_write_index == INBOUND_BUFFER_SIZE)
        next_write_index = 0;
    if (next_write_index == read_index)
    {
      overrun = 1;
      break;
    }
    wpnt = (uint8_t *)(context.InboundBuffer) + write_index;
    *wpnt = *data;
    data++; write_index = next_write_index; countdown--;
  }

  /* if we overran, bail and let the caller know we failed */
  if (overrun)
    return 0;

  /* adding the data was successful, so we update the write index */
  __disable_irq();
  context.InboundBufferWriteIndex = write_index;
  __enable_irq();
  return length;
}

__weak void USBD_VirtualCDC_LineState(uint16_t state) {} /* optionally overridden by user code */
