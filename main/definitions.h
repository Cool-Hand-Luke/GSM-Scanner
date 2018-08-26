/*
 * @file definitions.h
 * @brief definitions for esp drivers
 *
 * @author Naveen
 * @author Vikram Shanker (vshanker@andrew.cmu.edu)
 * @bug None
 */

#ifndef __DEFINITIONS_H
#define __DEFINITIONS_H

#define BUF_SIZE          (256)
#define MAX_CMD_LEN       (30)
#define MAX_LINE_LEN      BUF_SIZE

#define GSM_PORT          UART_NUM_1
#define GSM_RXD           (GPIO_NUM_16)
#define GSM_TXD           (GPIO_NUM_17)
#define GSM_PWR           (GPIO_NUM_21)

#define GPS_PORT          UART_NUM_2
#define GPS_RXD           (GPIO_NUM_18)
#define GPS_TXD           (GPIO_NUM_19)

#define SERIAL_RTS        (UART_PIN_NO_CHANGE)
#define SERIAL_CTS        (UART_PIN_NO_CHANGE)

#define UART_BUF_SIZE     BUF_SIZE * 2

#define SET_BIT(n)        (1ULL << n)

#define INIT_TAG    "Init"
#define WIFI_TAG    "WiFi"
#define SNTP_TAG    "SNTP"
#define GPS_TAG     "GPS"
#define GSM_TAG     "GSM"
#define GSM_RX_TAG  "GSM RX"
#define GSM_TX_TAG  "GSM TX"
#define STRCMP_TAG  "STRCMP"

enum {
  CMD_NONE,
  CMD_AT,
  CMD_ATI,
  CMD_CFUN,
  CMD_CNETSCAN,
};

typedef struct _at_command
{
  const char *name;
  uint8_t    id;
} at_command;

const at_command cmdSet[] = {
  {"", CMD_NONE},
  {"AT", CMD_AT},
  {"AT+CFUN", CMD_CFUN},
  {"AT+CNETSCAN", CMD_CNETSCAN},
  {"ATI", CMD_ATI},
};
#define ATCTOP ((sizeof(cmdSet) / sizeof(at_command)) -1)

TaskHandle_t  tid_scanner;
TaskHandle_t  tid_gsm_rx;
TaskHandle_t  tid_gsm_tx;

QueueHandle_t gps_tx_queue;
QueueHandle_t gps_uart_queue;
QueueHandle_t gps_rx_queue;

QueueHandle_t gsm_tx_queue;
QueueHandle_t gsm_uart_queue;
QueueHandle_t gsm_rx_queue;

typedef struct _uart_command
{
  char     cmd[MAX_CMD_LEN];
  uint8_t  len;
} uart_command;

typedef struct _uart_data
{
  uint8_t  bytes[MAX_LINE_LEN];
  uint16_t len;
} uart_data;

typedef struct _cnetscan_info_
{
  char      operator[12];
  char      cellid[6];
  uint16_t  mcc;
  uint16_t  mnc;
  uint16_t  rxlev;
  uint16_t  arfcn;
} cnetscan_info;
 
#endif
