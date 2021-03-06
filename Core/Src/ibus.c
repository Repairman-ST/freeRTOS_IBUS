#include "ibus.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "ibus.h"


uint16_t ibus_channel[IBUS_CHANNELS];
extern osThreadId_t rcHandlerTaskHandle;


static uint16_t calculateChecksum(const uint8_t *ibusPacket)
{
  uint16_t checksum = 0xFFFF;
  uint8_t dataSize = ibusPacket[0] - IBUS_CHECKSUM_SIZE;
  for (unsigned i = 0; i < dataSize; i++) {
    checksum -= ibusPacket[i];
  }

  return checksum;
}


// Receive ISR callback
static void ibusDataReceive(uint8_t c, uint8_t* buf)
{
  static TickType_t ibusTimeLast;
  static uint8_t ibusFramePosition;

  TickType_t now = xTaskGetTickCountFromISR();

  if (now - ibusTimeLast > IBUS_FRAME_GAP) {
    ibusFramePosition = 0;
  }

  ibusTimeLast = now;
  if (ibusFramePosition == 0 && c != IBUS_SERIAL_RX_PACKET_LENGTH) {
    return;
  }

  buf[ibusFramePosition] = (uint8_t)c;

  // Receive a complete frame of data
  if (ibusFramePosition == IBUS_SERIAL_RX_PACKET_LENGTH - 1) {
    ibusFramePosition = 0;
    vTaskNotifyGiveFromISR(rcHandlerTaskHandle, NULL);
  } else {
    ibusFramePosition++;
  }
}


HAL_StatusTypeDef IBUS_Receive_IT(UART_HandleTypeDef *huart)
{
  uint8_t data;
  /* Check that a Rx process is ongoing */
  if (huart->RxState == HAL_UART_STATE_BUSY_RX)
  {
    if ((huart->Init.WordLength == UART_WORDLENGTH_9B) || ((huart->Init.WordLength == UART_WORDLENGTH_8B) && (huart->Init.Parity == UART_PARITY_NONE)))
    {
      data = (uint8_t)(huart->Instance->DR & (uint8_t)0x00FF);
    }
    else
    {
      data = (uint8_t)(huart->Instance->DR & (uint8_t)0x007F);
    }
   
    // Never disable RX interrupt
    ibusDataReceive(data, huart->pRxBuffPtr);
    return HAL_OK;
  }
  else
  {
    return HAL_BUSY;
  }
}


static uint8_t isChecksumOkIa6b(const uint8_t *ibusPacket)
{
  uint16_t calculatedChecksum = calculateChecksum(ibusPacket);

  // Note that there's a byte order swap to little endian here
  return (calculatedChecksum >> 8) == ibusPacket[IBUS_SERIAL_RX_PACKET_LENGTH - 1]
        && (calculatedChecksum & 0xFF) == ibusPacket[IBUS_SERIAL_RX_PACKET_LENGTH - 2];
}


void ibusUpdateChannel(const uint8_t *ibusPacket)
{
  uint16_t i;
  uint16_t *channelPos = (uint16_t*)(ibusPacket+2);

  if (!isChecksumOkIa6b(ibusPacket)) {
    return;
  }

  for (i = 0; i < IBUS_CHANNELS; i++) {
    ibus_channel[i] = channelPos[i];
  }
}


uint16_t ibusGetChannel(uint8_t idx)
{
  return ibus_channel[idx];
}


uint16_t ibusGetChannelHundred(uint8_t idx)
{
  uint32_t tmp = ibus_channel[idx];

  if (tmp < IBUS_DATA_BEGIN+160) {
    return 0;
  }

  tmp = 100 * (tmp - IBUS_DATA_BEGIN) / IBUS_DATA_INTERVAL;
  if (tmp >= 94) {
    return 100;
  }

  return tmp;
}



