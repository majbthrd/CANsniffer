#ifndef __USB_VIRTUALCDC_H_
#define __USB_VIRTUALCDC_H_

#include  "usbd_def.h"
#include  "usbd_ioreq.h"

#define CDC_ITF_COMMAND 0x00
#define CDC_ITF_DATA    0x01
#define CDC_EP_COMMAND  0x82
#define CDC_EP_DATAOUT  0x01
#define CDC_EP_DATAIN   0x81

#define CDC_DATA_OUT_MAX_PACKET_SIZE        USB_FS_MAX_PACKET_SIZE /* don't exceed USB_FS_MAX_PACKET_SIZE; Linux data loss happens otherwise */
#define CDC_DATA_IN_MAX_PACKET_SIZE         256
#define CDC_CMD_PACKET_SIZE                 8 /* this may need to be enlarged for advanced CDC commands */

/*
INBOUND_BUFFER_SIZE should be 2x or more (bigger is better) of CDC_DATA_IN_MAX_PACKET_SIZE to ensure 
adequate time for the service routine to copy the data to the relevant USB IN endpoint PMA memory
*/
#define INBOUND_BUFFER_SIZE                 (4*CDC_DATA_IN_MAX_PACKET_SIZE)

/* listing CDC commands handled by switch statement in usbd_cdc.c */
#define CDC_SEND_ENCAPSULATED_COMMAND       0x00
#define CDC_GET_ENCAPSULATED_RESPONSE       0x01
#define CDC_SET_COMM_FEATURE                0x02
#define CDC_GET_COMM_FEATURE                0x03
#define CDC_CLEAR_COMM_FEATURE              0x04
#define CDC_SET_LINE_CODING                 0x20
#define CDC_GET_LINE_CODING                 0x21
#define CDC_SET_CONTROL_LINE_STATE          0x22
#define CDC_SEND_BREAK                      0x23

/* struct type used for each instance of a CDC UART */
typedef struct
{
  /*
  ST's example code partially used 32-bit alignment of individual buffers with no explanation as to why
  word alignment is relevant for DMA, so this practice was used (albeit in a more consistent manner) in this struct
  */
  uint32_t                   SetupBuffer[(CDC_CMD_PACKET_SIZE)/sizeof(uint32_t)];
  uint32_t                   OutboundBuffer[(CDC_DATA_OUT_MAX_PACKET_SIZE)/sizeof(uint32_t)];
  uint32_t                   InboundBuffer[(INBOUND_BUFFER_SIZE)/sizeof(uint32_t)];
  uint8_t                    CmdOpCode;
  uint8_t                    CmdLength;
  uint32_t                   InboundBufferReadIndex, InboundBufferWriteIndex;
  volatile uint32_t          InboundTransferInProgress;
  volatile uint32_t          OutboundTransferNeedsRenewal;
  volatile uint32_t          OutboundTransferOutstanding;
} USBD_CDC_HandleTypeDef;

/* array of callback functions invoked by USBD_RegisterClass() in main.c */
extern const USBD_ClassTypeDef USBD_CDC;

uint8_t USBD_CDC_RegisterInterface(USBD_HandleTypeDef *pdev);
void USBD_CDC_PMAConfig(PCD_HandleTypeDef *hpcd, uint32_t *pma_address);

/* user code calls this to add data to queue to host; a return value of zero indicates the action was not possible */
extern uint32_t USBD_VirtualCDC_ToHost_Append(const uint8_t *data, uint32_t length);

/* user code optionally implements this to act upon CDC LineState events */
extern void USBD_VirtualCDC_LineState(uint16_t state);

/* user code optionally implements this to process data from the host; return value must be 0 to length and indicates number of bytes handled */
extern uint32_t USBD_VirtualCDC_FromHost_Append(const uint8_t *data, uint32_t length);

#endif  // __USB_VIRTUALCDC_H_
