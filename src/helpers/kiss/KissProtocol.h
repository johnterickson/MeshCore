#pragma once

#include <stdint.h>

#define KISS_FEND  0xC0
#define KISS_FESC  0xDB
#define KISS_TFEND 0xDC
#define KISS_TFESC 0xDD

#define KISS_MAX_FRAME_SIZE  512
#define KISS_MAX_PACKET_SIZE 255
#define KISS_FRAME_BOUNDARY_BYTES 2
#define KISS_TYPE_BYTES 1
#define KISS_HW_SUBCMD_BYTES 1
#define KISS_MAX_ESCAPABLE_BYTES (KISS_MAX_FRAME_SIZE + KISS_TYPE_BYTES + KISS_HW_SUBCMD_BYTES)
#define KISS_MAX_ESCAPED_PAYLOAD_SIZE (2 * KISS_MAX_ESCAPABLE_BYTES)
#define KISS_MAX_ENCODED_FRAME_SIZE (KISS_FRAME_BOUNDARY_BYTES + KISS_MAX_ESCAPED_PAYLOAD_SIZE)
#define KISS_TX_FRAME_QUEUE_DEPTH 2
#define KISS_HW_MAX_PAYLOAD_SIZE (KISS_MAX_FRAME_SIZE + KISS_HW_SUBCMD_BYTES)

#define KISS_CMD_DATA        0x00
#define KISS_CMD_TXDELAY     0x01
#define KISS_CMD_PERSISTENCE 0x02
#define KISS_CMD_SLOTTIME    0x03
#define KISS_CMD_TXTAIL      0x04
#define KISS_CMD_FULLDUPLEX  0x05
#define KISS_CMD_SETHARDWARE 0x06
#define KISS_CMD_RETURN      0xFF

#define KISS_DEFAULT_TXDELAY     50
#define KISS_DEFAULT_PERSISTENCE 63
#define KISS_DEFAULT_SLOTTIME    10
#define KISS_TX_TIMEOUT_FACTOR   3/2

#define HW_CMD_GET_IDENTITY      0x01
#define HW_CMD_GET_RANDOM        0x02
#define HW_CMD_VERIFY_SIGNATURE  0x03
#define HW_CMD_SIGN_DATA         0x04
#define HW_CMD_ENCRYPT_DATA      0x05
#define HW_CMD_DECRYPT_DATA      0x06
#define HW_CMD_KEY_EXCHANGE      0x07
#define HW_CMD_HASH              0x08
#define HW_CMD_SET_RADIO         0x09
#define HW_CMD_SET_TX_POWER      0x0A
#define HW_CMD_GET_RADIO         0x0B
#define HW_CMD_GET_TX_POWER      0x0C
#define HW_CMD_GET_CURRENT_RSSI  0x0D
#define HW_CMD_IS_CHANNEL_BUSY   0x0E
#define HW_CMD_GET_AIRTIME       0x0F
#define HW_CMD_GET_NOISE_FLOOR   0x10
#define HW_CMD_GET_VERSION       0x11
#define HW_CMD_GET_STATS         0x12
#define HW_CMD_GET_BATTERY       0x13
#define HW_CMD_GET_MCU_TEMP      0x14
#define HW_CMD_GET_SENSORS       0x15
#define HW_CMD_GET_DEVICE_NAME   0x16
#define HW_CMD_PING              0x17
#define HW_CMD_REBOOT            0x18
#define HW_CMD_SET_SIGNAL_REPORT 0x19
#define HW_CMD_GET_SIGNAL_REPORT 0x1A

#define HW_RESP(cmd)             ((cmd) | 0x80)
#define HW_RESP_OK               0xF0
#define HW_RESP_ERROR            0xF1
#define HW_RESP_TX_DONE          0xF8
#define HW_RESP_RX_META          0xF9

#define HW_ERR_INVALID_LENGTH    0x01
#define HW_ERR_INVALID_PARAM     0x02
#define HW_ERR_NO_CALLBACK       0x03
#define HW_ERR_MAC_FAILED        0x04
#define HW_ERR_UNKNOWN_CMD       0x05
#define HW_ERR_ENCRYPT_FAILED    0x06
#define HW_ERR_TX_BUSY           0x07

#define KISS_FIRMWARE_VERSION 1

struct KissRadioConfig {
  uint32_t freq_hz;
  uint32_t bw_hz;
  uint8_t sf;
  uint8_t cr;
  uint8_t tx_power;
};