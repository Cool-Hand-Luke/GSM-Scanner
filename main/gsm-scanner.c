/* GSM Scanner

 This example code is in the Public Domain (or CC0 licensed, at your option.)

 Unless required by applicable law or agreed to in writing, this
 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 CONDITIONS OF ANY KIND, either express or implied.
*/

#include <time.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "tftspi.h"
#include "tft.h"
#include "spiffs_vfs.h"
#include "definitions.h"
#include "esp_log.h"

// Define which spi bus to use TFT_VSPI_HOST or TFT_HSPI_HOST
#define SPI_BUS TFT_HSPI_HOST
//#define SPI_BUS TFT_VSPI_HOST
// ==========================================================

static bool gsmPoweredOn = false;
static bool gsmConnected = false;
static bool sim_inserted = false;
static void gsm_send( const char * );

static struct tm *tm_info;
static char   tmp_buff[256];
static time_t time_now, time_last = 0;

//----------------------
static void _checkTime()
{
  time( &time_now );
  if ( time_now > time_last )
  {
    color_t last_fg, last_bg;
    time_last = time_now;
    tm_info = localtime( &time_now );
    sprintf( tmp_buff, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec );

    TFT_saveClipWin();
    TFT_resetclipwin();

    Font curr_font = cfont;
    last_bg = _bg;
    last_fg = _fg;
    _fg = TFT_YELLOW;
    _bg = ( color_t )
    {
    64, 64, 64};
    TFT_setFont( DEFAULT_FONT, NULL );

    TFT_fillRect( 1, _height - TFT_getfontheight() - 8, _width - 3, TFT_getfontheight() + 6, _bg );
    TFT_print( tmp_buff, CENTER, _height - TFT_getfontheight() - 5 );

    cfont = curr_font;
    _fg = last_fg;
    _bg = last_bg;

    TFT_restoreClipWin();
  }
}

//---------------------
static int Wait( int ms )
{
  uint8_t tm = 1;
  if ( ms < 0 )
  {
    tm = 0;
    ms *= -1;
  }
  if ( ms <= 50 )
  {
    vTaskDelay( pdMS_TO_TICKS( ms ) );
//if (_checkTouch()) return 0;
  }
  else
  {
    for ( int n = 0; n < ms; n += 50 )
    {
      vTaskDelay( pdMS_TO_TICKS( 50 ) );
      if ( tm )
        _checkTime();
//if (_checkTouch()) return 0;
    }
  }
  return 1;
}

//---------------------
static void _dispTime()
{
  Font curr_font = cfont;
  if ( _width < 240 )
    TFT_setFont( DEF_SMALL_FONT, NULL );
  else
    TFT_setFont( DEFAULT_FONT, NULL );

  time( &time_now );
  time_last = time_now;
  tm_info = localtime( &time_now );
  sprintf( tmp_buff, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec );
  TFT_print( tmp_buff, CENTER, _height - TFT_getfontheight() - 5 );

  cfont = curr_font;
}

//---------------------------------
static void disp_header( const char *info )
{
  TFT_fillScreen( TFT_BLACK );
  TFT_resetclipwin();

  _fg = TFT_YELLOW;
  _bg = ( color_t )
  {
  64, 64, 64};

  if ( _width < 240 )
    TFT_setFont( DEF_SMALL_FONT, NULL );
  else
    TFT_setFont( DEFAULT_FONT, NULL );
  TFT_fillRect( 0, 0, _width - 1, TFT_getfontheight() + 8, _bg );
  TFT_drawRect( 0, 0, _width - 1, TFT_getfontheight() + 8, TFT_CYAN );

  TFT_fillRect( 0, _height - TFT_getfontheight() - 9, _width - 1, TFT_getfontheight() + 8, _bg );
  TFT_drawRect( 0, _height - TFT_getfontheight() - 9, _width - 1, TFT_getfontheight() + 8, TFT_CYAN );

  TFT_print( ( char * ) info, CENTER, 4 );
  _dispTime();

  _bg = TFT_BLACK;
  TFT_setclipwin( 0, TFT_getfontheight() + 9, _width - 1, _height - TFT_getfontheight() - 10 );
}

//---------------------------------------------
static void update_header( const char *hdr, const char *ftr )
{
  color_t last_fg, last_bg;

  TFT_saveClipWin();
  TFT_resetclipwin();

  Font curr_font = cfont;
  last_bg = _bg;
  last_fg = _fg;
  _fg = TFT_YELLOW;
  _bg = ( color_t )
  {
  64, 64, 64};
  if ( _width < 240 )
    TFT_setFont( DEF_SMALL_FONT, NULL );
  else
    TFT_setFont( DEFAULT_FONT, NULL );

  if ( hdr )
  {
    TFT_fillRect( 1, 1, _width - 3, TFT_getfontheight() + 6, _bg );
    TFT_print( ( char * ) hdr, CENTER, 4 );
  }

  if ( ftr )
  {
    TFT_fillRect( 1, _height - TFT_getfontheight() - 8, _width - 3, TFT_getfontheight() + 6, _bg );
    if ( strlen( ftr ) == 0 )
      _dispTime();
    else
      TFT_print( ( char * ) ftr, CENTER, _height - TFT_getfontheight() - 5 );
  }

  cfont = curr_font;
  _fg = last_fg;
  _bg = last_bg;

  TFT_restoreClipWin();
}

#undef isspace
static int isspace( int c )
{
  return ( ( c >= 0x09 && c <= 0x0D ) || ( c == 0x20 ) );
}

static unsigned int trim( char *str )
{
  char *retStr = str;
  int i, j = 0;
  int lastChar = 0;
  bool start = false;

  for ( i = 0; str[i] != 0x0; i++ )
  {
    if ( !isspace( ( unsigned char ) str[i] ) )
    {
      retStr[j++] = str[i];

      if ( !start )
      {
        start = true;
      }

      // Reset last space
      lastChar = 0;
    }
    else if ( start )
    {
      if ( !lastChar )
      {
        lastChar = j;                                                                              // Save this point as a none white space
        retStr[j++] = str[i];                                                                      // Copy whitespace
        //retStr[j++] = 0x20;         // Set whitespaces to a 'space'
      }
    }
  }

  j = ( lastChar ? lastChar : j );
  retStr[j] = 0x0;

  return j;
}

//===============
static void gsm_pwr()
{
  gpio_set_level( GSM_PWR, 0 );
  vTaskDelay( pdMS_TO_TICKS( 1500 ) );
  gpio_set_level( GSM_PWR, 1 );
  vTaskDelay( pdMS_TO_TICKS( 500 ) );
}

/* Scan while arg1 is greater than ' ' and arg1 == arg2 */
int shrt_cmp( const char *arg1, const char *arg2 )
{
  int chk = 0;
  while ( *arg1 > ' ' )
  {
    if ( ( chk = ( *arg1++ | 32 ) - ( *arg2++ | 32 ) ) )
    {
      return ( chk );
    }
  }
  return ( 0 );
}

static int _check_command( char *compStr )
{
  int top = ATCTOP;
  int mid, found, low = 0;

  // Null or useless string
  if ( strlen( compStr ) < 2 )
  {
    return CMD_NONE;
  }

  // Loop through available commands
  while ( low <= top )
  {
    mid = ( low + top ) / 2;
    if ( ( found = shrt_cmp( compStr, cmdSet[mid].name ) ) > 0 )
    {
      low = mid + 1;
    }
    else if ( found < 0 )
    {
      top = mid - 1;
    }
    else
    {
      ESP_LOGI( GSM_TAG, "_check_command: (%d) [%s] [%s]", cmdSet[mid].id, compStr, cmdSet[mid].name );
      return ( cmdSet[mid].id );
    }
  }

  ESP_LOGI( GSM_TAG, "_check_command: (%d) [%s] <fail> ", CMD_NONE, compStr );
  return CMD_NONE;
}

static bool dcs1800_gsm = true;
static void decode_arfcn( uint16_t arfcn, const char **band, uint * uplink, uint * downlink )
{
  /*
   * Decode ARFCN to frequency using GSM 05.05 
   */

  if ( arfcn >= 1 && arfcn <= 124 )
  {
    *band = "P-GSM 900";
    *uplink = 890000 + 200 * arfcn;
    *downlink = *uplink + 45000;
  }
  else if ( arfcn == 0 )
  {
    *band = "E-GSM 900";
    *uplink = 890000 + 200 * arfcn;
    *downlink = *uplink + 45000;
  }
  else if ( arfcn >= 975 && arfcn <= 1023 )
  {
    *band = "E-GSM 900";
    *uplink = 890000 + 200 * ( arfcn - 1024 );
    *downlink = *uplink + 45000;
  }
  else if ( arfcn >= 955 && arfcn <= 1023 )
  {
    *band = "R-GSM 900";
    *uplink = 890000 + 200 * ( arfcn - 1024 );
    *downlink = *uplink + 45000;
  }
  else if ( arfcn >= 512 && arfcn <= 885 && dcs1800_gsm )
  {
    *band = "DCS 1800";
    *uplink = 1710200 + 200 * ( arfcn - 512 );
    *downlink = *uplink + 95000;
  }
  else if ( arfcn >= 512 && arfcn <= 810 && !dcs1800_gsm )
  {
    *band = "PCS 1900";
    *uplink = 1850200 + 200 * ( arfcn - 512 );
    *downlink = *uplink + 80000;
  }
  else if ( arfcn >= 259 && arfcn <= 293 )
  {
    *band = "GSM 450";
    *uplink = 450600 + 200 * ( arfcn - 259 );
    *downlink = *uplink + 10000;
  }
  else if ( arfcn >= 306 && arfcn <= 340 )
  {
    *band = "GSM 480";
    *uplink = 479000 + 200 * ( arfcn - 306 );
    *downlink = *uplink + 10000;
  }
  else if ( arfcn >= 128 && arfcn <= 251 )
  {
    *band = "GSM 850";
    *uplink = 824200 + 200 * ( arfcn - 128 );
    *downlink = *uplink + 45000;
  }
  else
  {
    *band = "Unknown";
    *uplink = *downlink = 0;
  }
}

static const char hextable[] = {
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/** 
 * @brief convert a hexidecimal string to a signed long
 * will not produce or process negative numbers except 
 * to signal error.
 * 
 * @param hex without decoration, case insensitive. 
 * 
 * @return -1 on error, or result (max (sizeof(long)*8)-1 bits)
 */
static long hexdec( unsigned const char *hex )
{
  static long ret = 0;

  while ( *hex && ret >= 0 )
  {
    ret = ( ret << 4 ) | hextable[*hex++];
  }

  return ret;
}

static int sortRxlev( const void *s1, const void *s2 )
{
  cnetscan_info *e1 = ( cnetscan_info * ) s1;
  cnetscan_info *e2 = ( cnetscan_info * ) s2;

  int rxCompare = e2->rxlev - e1->rxlev;

  if ( rxCompare == 0 )
  {
    rxCompare = strcmp( e1->operator, e2->operator  );
  }

  return rxCompare;
}

static int sortOperator( const void *s1, const void *s2 )
{
  cnetscan_info *e1 = ( cnetscan_info * ) s1;
  cnetscan_info *e2 = ( cnetscan_info * ) s2;

  int opCompare = strcmp( e1->operator, e2->operator  );

  if ( opCompare == 0 )
  {
    opCompare = e2->rxlev - e1->rxlev;
  }

  return opCompare;
}

static void gsm_scanner()
{

  font_rotate = 0;
  text_wrap = 0;
  font_transparent = 0;
  font_forceFixed = 0;
  TFT_resetclipwin();

  image_debug = 0;

  char dtype[16];

  switch ( tft_disp_type )
  {
    case DISP_TYPE_ILI9341:
      sprintf( dtype, "ILI9341" );
      break;
    case DISP_TYPE_ILI9488:
      sprintf( dtype, "ILI9488" );
      break;
    case DISP_TYPE_ST7789V:
      sprintf( dtype, "ST7789V" );
      break;
    case DISP_TYPE_ST7735:
      sprintf( dtype, "ST7735" );
      break;
    case DISP_TYPE_ST7735R:
      sprintf( dtype, "ST7735R" );
      break;
    case DISP_TYPE_ST7735B:
      sprintf( dtype, "ST7735B" );
      break;
    default:
      sprintf( dtype, "Unknown" );
  }

  gray_scale = 0;

  // Set Rotation
  TFT_setRotation( LANDSCAPE );

  disp_header( "GSM Scanner" );
  update_header( "GSM Scanner", "" );

  //gsm_pwr();
  uint8_t currentCommand = CMD_NONE;
  uart_data data;
  static char workBuf[MAX_LINE_LEN];
  uint16_t bufPtr = 0;
  bool newLine = false;
  uint16_t x, y;
  cnetscan_info networks[20];
  uint8_t netCount;

  for ( ;; )
  {
    /*
     * Only run when we have no connection 
     */
    while ( gsmConnected == false )
    {
      gsm_send( "AT\n" );

      for ( uint8_t wait = 5; gsmConnected == false && wait > 0; wait-- )
      {
        if ( xQueueReceive( gsm_rx_queue, &data, 10 ) )
        {
          trim( ( char * ) data.bytes );
          ESP_LOGD( GSM_TAG, "gsm_rx_queue:\n%s<<", ( char * ) data.bytes );
          if ( !strncmp( ( char * ) data.bytes, "AT\rOK", 5 ) )
          {
            ESP_LOGI( GSM_TAG, "gsm_rx_queue: gsmConnected = true" );
            gsmConnected = true;
            gsmPoweredOn = true;
          }
        }

        ESP_LOGD( GSM_TAG, "wait()" );
        vTaskDelay( pdMS_TO_TICKS( 200 ) );
      }

      if ( gsmConnected )
      {
        ESP_LOGI( GSM_TAG, "Connected" );
        gsm_send( "AT+CNETSCAN\n" );
      }
      else
      {
        ESP_LOGI( GSM_TAG, "Toggling GSM Power" );
        gsm_pwr();
        vTaskDelay( pdMS_TO_TICKS( 5000 ) );
      }

      vTaskDelay( pdMS_TO_TICKS( 500 ) );
      taskYIELD();
    }

    /*
     * Normal conditions 
     */
    if ( xQueueReceive( gsm_rx_queue, &data, 10 ) )
    {
      switch ( currentCommand )
      {
        case CMD_AT:
          break;
        case CMD_ATI:
          break;
        case CMD_CNETSCAN:
          if ( newLine )
          {
            netCount = 0;
            newLine = false;
            x = 10, y = 10;

            _fg = TFT_WHITE;
            _bg = TFT_BLACK;

            // Clear the screen
            //TFT_fillRect( 10, 5, _width - 20, 185, TFT_BLACK );

            sprintf( tmp_buff, "%-12s %-9s %-10s %-6s", "Operator", "CellID", "Freq MHz", "RxLev" );
            TFT_print( tmp_buff, x, y );
          }
          uint16_t cc = 0;
          while ( cc < data.len )
          {
            if ( data.bytes[cc] != '\r' )
            {
              // Copy the current character to the working buffer 
              // and move onto the next character
              workBuf[bufPtr++] = data.bytes[cc++];
            }
            else
            {
              // Terminate the string in the working buffer
              workBuf[bufPtr] = 0x0;

              // Back to the start of the working buffer
              bufPtr = 0;

              // Plain "OK" means we are done processing
              if ( !strncmp( ( char * ) workBuf, "OK", 2 ) )
              {
                ESP_LOGI( GSM_TAG, "All Done!" );
                currentCommand = CMD_NONE;

                // Clear the screen
                TFT_fillRect( 10, 22, _width - 20, 170, TFT_BLACK );

                // Sort the list of networks
                qsort( networks, netCount, sizeof( cnetscan_info ), sortRxlev );

                // DEJAVU18_FONT / DEJAVU24_FONT / UBUNTU16_FONT
                // COMIC24_FONT / MINYA24_FONT / TOONEY32_FONT
                // SMALL_FONT / DEF_SMALL_FONT / DEFAULT_FONT
                _fg = TFT_CYAN;
                y += 2;
                TFT_setFont( DEFAULT_FONT, NULL );
                const char *band;
                uint downlink, uplink;
                for ( uint8_t c = 0; c < netCount; c++ )
                {
                  y += 12;
                  x = 10;
                  TFT_print( networks[c].operator, x, y );
                  TFT_print( networks[c].cellid, x + 90, y );

                  decode_arfcn(networks[c].arfcn, &band, &uplink, &downlink);
                  sprintf(tmp_buff, "%0.1f", ((float)downlink / 1000));
                  TFT_print( tmp_buff, x + 150, y );

                  sprintf( tmp_buff, "%d", networks[c].rxlev );
                  TFT_print( tmp_buff, x + 226, y );

                  //TFT_jpg_image (x + 226, y+23, 1, "/spiffs/images/1.jpeg", NULL, 0);
                  //Wait(10);
                }

                // Start over
                gsm_send( "AT+CNETSCAN\n" );
              }
              // Else, chop the string into segments
              // Operator:"O2",MCC:234,MNC:10,Rxlev:12,Cellid:3284,Arfcn:102
              else if ( !strncmp( ( char * ) workBuf, "Operator", 8 ) )
              {
                uint8_t c = 0;  // Char count
                uint8_t b = 0;  // Break count
                uint16_t bp = 0;  // bufPtr

                // Log our current string
                ESP_LOGI( GSM_TAG, "Network %d: %s\n", netCount, ( char * ) workBuf );

                do
                {
                  if ( workBuf[bp] == ',' || workBuf[bp] == 0x0 )
                  {
                    switch ( b )
                    {
                      case 0:
                        tmp_buff[c - 1] = 0x0;
                        strcpy( networks[netCount].operator, ( char * ) &tmp_buff[10] );
                        break;
                      case 1:
                        tmp_buff[c] = 0x0;
                        networks[netCount].mcc = atoi( ( char * ) &tmp_buff[4] );
                        break;
                      case 2:
                        tmp_buff[c] = 0x0;
                        networks[netCount].mnc = atoi( ( char * ) &tmp_buff[4] );
                        break;
                      case 3:
                        tmp_buff[c] = 0x0;
                        networks[netCount].rxlev = atoi( ( char * ) &tmp_buff[6] );
                        break;
                      case 4:
                        tmp_buff[c] = 0x0;
                        strcpy( networks[netCount].cellid, ( char * ) &tmp_buff[7] );
                        break;
                      case 5:
                        tmp_buff[c] = 0x0;
                        networks[netCount].arfcn = atoi( ( char * ) &tmp_buff[6] );
                        break;
                      default:
                        break;
                    }
                    c = 0;
                    b++;
                  }
                  else
                  {
                    tmp_buff[c++] = workBuf[bp];
                  }

                  bp++;
                }
                while ( bp <= strlen(workBuf) );

                /*
                printf("op: %s, mcc: %d, mnc: %d, rxlev: %d, cellid: %s, arfcn: %d\n",
                    networks[netCount].operator, networks[netCount].mcc, networks[netCount].mnc,
                    networks[netCount].rxlev, networks[netCount].cellid, networks[netCount].arfcn);
                    */

                netCount++;
              }
              else
              {
                ESP_LOGI( GSM_TAG, "Invalid Line: %s", ( char * ) workBuf );
              }

              // Increment the current character comparison and
              // skip any white spaces for the start of the next string
              while ( ( cc < data.len ) && isspace( data.bytes[cc] ) )
              {
                cc++;
              }
            }
          }
          break;
        case CMD_NONE:
          if ( ( currentCommand = _check_command( ( char * ) data.bytes ) ) )
          {
            newLine = true;
            bufPtr = 0;
          }
          break;
        default:
          ESP_LOGI( GSM_TAG, "gsm_rx_queue: (%d) [%s]", currentCommand, ( char * ) data.bytes );
          break;
      }
    }

    vTaskDelay( pdMS_TO_TICKS( 100 ) );
  }

  ESP_LOGI( GSM_TAG, "gsm_scanner: vTaskDelete()" );
  vTaskDelete( NULL );
}

/*
// ================== TEST SD CARD ==========================================

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

// This example can use SDMMC and SPI peripherals to communicate with SD card.
// SPI mode IS USED

// When testing SD and SPI modes, keep in mind that once the card has been
// initialized in SPI mode, it can not be reinitialized in SD mode without
// toggling power to the card.

// Pin mapping when using SPI mode.
// With this mapping, SD card can be used both in SPI and 1-line SD mode.
// Note that a pull-up on CS line is required in SD mode.
#define sdPIN_NUM_MISO 19
#define sdPIN_NUM_MOSI 23
#define sdPIN_NUM_CLK18
#define sdPIN_NUM_CS 4

static const char *TAG = "SDCard test";

void test_sd_card( void )
{
  ESP_LOGI( SD_TAG, "\n=======================================================\n" );
  ESP_LOGI( SD_TAG, "===== Test using SD Card in SPI mode=====\n" );
  ESP_LOGI( SD_TAG, "===== SD Card uses the same gpio's as TFT display =====\n" );
  ESP_LOGI( SD_TAG, "=======================================================\n\n" );
  ESP_LOGI( SD_TAG, TAG, "Initializing SD card" );
  ESP_LOGI( SD_TAG, TAG, "Using SPI peripheral" );

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
  slot_config.gpio_miso = sdPIN_NUM_MISO;
  slot_config.gpio_mosi = sdPIN_NUM_MOSI;
  slot_config.gpio_sck = sdPIN_NUM_CLK;
  slot_config.gpio_cs = sdPIN_NUM_CS;
// This initializes the slot without card detect (CD) and write protect (WP) signals.
// Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.

// Options for mounting the filesystem.
// If format_if_mount_failed is set to true, SD card will be partitioned and
// formatted in case when mounting fails.
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 5
  };

// Use settings defined above to initialize SD card and mount FAT filesystem.
// Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
// Please check its source code and implement error recovery when developing
// production applications.
  sdmmc_card_t *card;
  esp_err_t ret = esp_vfs_fat_sdmmc_mount( "/sdcard", &host, &slot_config, &mount_config, &card );

  if ( ret != ESP_OK )
  {
    if ( ret == ESP_FAIL )
    {
      ESP_LOGE( TAG, "Failed to mount filesystem. " "If you want the card to be formatted, set format_if_mount_failed = true." );
    }
    else
    {
      ESP_LOGE( TAG, "Failed to initialize the card (%d). " "Make sure SD card lines have pull-up resistors in place.", ret );
    }
    return;
  }

// Card has been initialized, print its properties
  sdmmc_card_print_info( stdout, card );

// Use POSIX and C standard library functions to work with files.
// First create a file.
  ESP_LOGI( SD_TAG, "Opening file" );
  FILE *f = fopen( "/sdcard/hello.txt", "w" );
  if ( f == NULL )
  {
    ESP_LOGE( TAG, "Failed to open file for writing" );
    return;
  }
  fprintf( f, "Hello %s!\n", card->cid.name );
  fclose( f );
  ESP_LOGI( SD_TAG, "File written" );

// Check if destination file exists before renaming
  struct stat st;
  if ( stat( "/sdcard/foo.txt", &st ) == 0 )
  {
// Delete it if it exists
    unlink( "/sdcard/foo.txt" );
  }

// Rename original file
  ESP_LOGI( SD_TAG, "Renaming file" );
  if ( rename( "/sdcard/hello.txt", "/sdcard/foo.txt" ) != 0 )
  {
    ESP_LOGE( TAG, "Rename failed" );
    return;
  }

// Open renamed file for reading
  ESP_LOGI( SD_TAG, "Reading file" );
  f = fopen( "/sdcard/foo.txt", "r" );
  if ( f == NULL )
  {
    ESP_LOGE( TAG, "Failed to open file for reading" );
    return;
  }
  char line[64];
  fgets( line, sizeof( line ), f );
  fclose( f );
// strip newline
  char *pos = strchr( line, '\n' );
  if ( pos )
  {
    *pos = 0x0;
  }
  ESP_LOGI( SD_TAG, "Read from file: '%s'", line );

// All done, unmount partition and disable SDMMC or SPI peripheral
  esp_vfs_fat_sdmmc_unmount();
  ESP_LOGI( SD_TAG, "Card unmounted" );

  ESP_LOGI( SD_TAG, "===== SD Card test end ================================\n\n" );
}
*/

// ================== TEST SD CARD ==========================================

static void gsm_send( const char *msg )
{
  uart_command pkt;

  pkt.len = strlen( msg );
  strncpy( pkt.cmd, msg, pkt.len );
  pkt.cmd[pkt.len] = 0x0;

  if ( gsm_tx_queue != 0 )
  {
    xQueueSend( gsm_tx_queue, &pkt, ( portTickType ) 0 );
  }
}

static void gsm_tx_main( void *pvParameters )
{
  uart_command pkt;

  for ( ;; )
  {
    if ( xQueueReceive( gsm_tx_queue, &pkt, 10 ) )
    {
      ESP_LOGI( GSM_TX_TAG, "gsm_tx_main(port:%d): %s", GSM_TXD, pkt.cmd );
      uart_write_bytes( GSM_PORT, pkt.cmd, pkt.len );
    }

    ESP_LOGD( GSM_TX_TAG, "tick()" );
    vTaskDelay( pdMS_TO_TICKS( 500 ) );
    taskYIELD();
  }

  ESP_LOGI( GSM_TX_TAG, "gsm_tx_main: vTaskDelete()" );
  vTaskDelete( NULL );
}

static void gsm_rx_main( void *pvParameters )
{
  uart_event_t event;
  uart_data data;
  size_t buffered_size;
  //uint8_t *dtmp = ( uint8_t * ) pvPortMalloc( BUF_SIZE );

  for ( ;; )
  {
    //Waiting for UART event.
    if ( xQueueReceive( gsm_uart_queue, ( void * ) &event, 10 ) )
    {
      //ESP_LOGI( GSM_RX_TAG, "uart[%d] event:", GSM_PORT );
      switch ( event.type )
      {
          //Event of UART receving data
          /*
           * We'd better handler data event fast, there would be much more data events than
           * other types of events. If we take too much time on data event, the queue might
           * be full.
           * in this example, we don't process data in event, but read data outside.
           */
        case UART_DATA:
          uart_get_buffered_data_len( GSM_PORT, &buffered_size );
          ESP_LOGI( GSM_RX_TAG, "data, len: %d; buffered len: %d", event.size, buffered_size );

          data.len = uart_read_bytes( GSM_PORT, ( uint8_t * ) data.bytes, MAX_LINE_LEN, 100 / portTICK_RATE_MS );
          data.bytes[data.len] = 0x0;
          ESP_LOGD( GSM_RX_TAG, "\n%s", data.bytes );

          if ( gsm_rx_queue != 0 )
          {
            xQueueSend( gsm_rx_queue, &data, ( portTickType ) 0 );
          }

          // Local Echo
          //gsm_send( data.bytes);

          break;
          //Event of HW FIFO overflow detected
        case UART_FIFO_OVF:
          ESP_LOGI( GSM_RX_TAG, "hw fifo overflow\n" );
          //If fifo overflow happened, you should consider adding flow control for your application.
          //We can read data out out the buffer, or directly flush the rx buffer.
          uart_flush( GSM_PORT );
          break;
          //Event of UART ring buffer full
        case UART_BUFFER_FULL:
          ESP_LOGI( GSM_RX_TAG, "ring buffer full\n" );
          //If buffer full happened, you should consider encreasing your buffer size
          //We can read data out out the buffer, or directly flush the rx buffer.
          uart_flush( GSM_PORT );
          break;
          //Event of UART RX break detected
        case UART_BREAK:
          ESP_LOGI( GSM_RX_TAG, "uart rx break\n" );
          break;
          //Event of UART parity check error
        case UART_PARITY_ERR:
          ESP_LOGI( GSM_RX_TAG, "uart parity error\n" );
          break;
          //Event of UART frame error
        case UART_FRAME_ERR:
          ESP_LOGI( GSM_RX_TAG, "uart frame error\n" );
          break;
          //UART_PATTERN_DET
        case UART_PATTERN_DET:
          ESP_LOGI( GSM_RX_TAG, "uart pattern detected\n" );
          break;
          //Others
        default:
          ESP_LOGI( GSM_RX_TAG, "uart event type: %d\n", event.type );
          break;
      }
    }

    vTaskDelay( pdMS_TO_TICKS( 10 ) );
    taskYIELD();
  }

  ESP_LOGI( GSM_RX_TAG, "gsm_rx_main: vTaskDelete()" );
  vTaskDelete( NULL );
}

//=============
void app_main()
{
  //test_sd_card();
  // ========PREPARE DISPLAY INITIALIZATION=========

  esp_err_t ret;

  // ====================================================================
  // === Pins MUST be initialized before SPI interface initialization ===
  // ====================================================================
  TFT_PinsInit();

  // ====CONFIGURE SPI DEVICES(s)====================================================================================
  spi_lobo_device_handle_t spi;

  spi_lobo_bus_config_t buscfg = {
    .miso_io_num = PIN_NUM_MISO,                                                                   // set SPI MISO pin
    .mosi_io_num = PIN_NUM_MOSI,                                                                   // set SPI MOSI pin
    .sclk_io_num = PIN_NUM_CLK,                                                                    // set SPI CLK pin
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 6 * 1024,
  };
  spi_lobo_device_interface_config_t devcfg = {
    .clock_speed_hz = 8000000,                                                                     // Initial clock out at 8 MHz
    .mode = 0,                                                                                     // SPI mode 0
    .spics_io_num = -1,                                                                            // we will use external CS pin
    .spics_ext_io_num = PIN_NUM_CS,                                                                // external CS pin
    .flags = LB_SPI_DEVICE_HALFDUPLEX,                                                             // ALWAYS SETto HALF DUPLEX MODE!! for display spi
  };

  /*****************************************************/
  /*
   * Configure UART driver
   */
  uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
  };

  /*
   * uart_intr_config_t lidar_intr = {
   * .intr_enable_mask         = UART_RXFIFO_FULL_INT_ENA_M,
   * .rxfifo_full_thresh       = LIDAR_THRESHOLD,
   * .rx_timeout_thresh        = UART_TOUT_THRESH_DEFAULT,
   * .txfifo_empty_intr_thresh = UART_EMPTY_THRESH_DEFAULT,
   * };
   */

  uart_param_config( GSM_PORT, &uart_config );
  uart_set_pin( GSM_PORT, GSM_TXD, GSM_RXD, SERIAL_RTS, SERIAL_CTS );
  uart_driver_install( GSM_PORT, UART_BUF_SIZE, UART_BUF_SIZE, 10, &gsm_uart_queue, 0 );
  //uart_enable_pattern_det_intr( GSM_PORT, ( char ) 0x81, PATTERN_NUM, 10000, 10, 10 );
  /*****************************************************/

  /*****************************************************/
  /*
   * Output pin(s)
   */
  gpio_config_t out_pin_config;
  out_pin_config.intr_type = GPIO_INTR_DISABLE;
  out_pin_config.mode = GPIO_MODE_OUTPUT;
  out_pin_config.pin_bit_mask = ( 1ULL << GSM_PWR );
  out_pin_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out_pin_config.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config( &out_pin_config );

  // ====================================================================================================================
  vTaskDelay( pdMS_TO_TICKS( 500 ) );
  ESP_LOGI( INIT_TAG, "\r\n==============================\r\n" );
  ESP_LOGI( INIT_TAG, "GSM Scanner\r\n" );
  ESP_LOGI( INIT_TAG, "==============================\r\n" );
  ESP_LOGI( INIT_TAG, "Pins used: miso=%d, mosi=%d, sck=%d, cs=%d\r\n", PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS );
  ESP_LOGI( INIT_TAG, "==============================\r\n\r\n" );

  // ==================================================================
  // ==== Initialize the SPI bus and attach the LCD to the SPI bus ====

  ret = spi_lobo_bus_add_device( SPI_BUS, &buscfg, &devcfg, &spi );
  assert( ret == ESP_OK );
  ESP_LOGI( INIT_TAG, "SPI: display device added to spi bus (%d)\r\n", SPI_BUS );
  disp_spi = spi;

  // ==== Test select/deselect ====
  ret = spi_lobo_device_select( spi, 1 );
  assert( ret == ESP_OK );
  ret = spi_lobo_device_deselect( spi );
  assert( ret == ESP_OK );

  ESP_LOGI( INIT_TAG, "SPI: attached display device, speed=%u\r\n", spi_lobo_get_speed( spi ) );
  ESP_LOGI( INIT_TAG, "SPI: bus uses native pins: %s\r\n", spi_lobo_uses_native_pins( spi ) ? "true" : "false" );

  // ================================
  // ==== Initialize the Display ====

  ESP_LOGI( INIT_TAG, "SPI: display init...\r\n" );
  TFT_display_init();
  ESP_LOGI( INIT_TAG, "OK\r\n" );

  // ---- Detect maximum read speed ----
  max_rdclock = find_rd_speed();
  ESP_LOGI( INIT_TAG, "SPI: Max rd speed = %u\r\n", max_rdclock );

  // ==== Set SPI clock used for display operations ====
  spi_lobo_set_speed( spi, DEFAULT_SPI_CLOCK );
  //spi_lobo_set_speed (spi, 32000000);
  ESP_LOGI( INIT_TAG, "SPI: Changed speed to %u\r\n", spi_lobo_get_speed( spi ) );

  font_rotate = 0;
  text_wrap = 0;
  font_transparent = 0;
  font_forceFixed = 0;
  gray_scale = 0;
  TFT_setGammaCurve( DEFAULT_GAMMA_CURVE );
  TFT_setRotation( LANDSCAPE );
  TFT_setFont( DEFAULT_FONT, NULL );
  TFT_resetclipwin();

  disp_header( ( char * ) "File system INIT" );
  _fg = TFT_CYAN;
  TFT_print( ( char * ) "Initializing SPIFFS...", CENTER, CENTER );
  // ==== Initialize the file system ====
  vfs_spiffs_register();
  if ( !spiffs_is_mounted )
  {
    _fg = TFT_RED;
    TFT_print( ( char * ) "SPIFFS not mounted !", CENTER, LASTY + TFT_getfontheight() + 2 );
  }
  else
  {
    _fg = TFT_GREEN;
    TFT_print( ( char * ) "SPIFFS Mounted.", CENTER, LASTY + TFT_getfontheight() + 2 );
  }

  gsm_tx_queue = xQueueCreate( 10, sizeof( uart_command ) );
  gsm_rx_queue = xQueueCreate( 10, sizeof( uart_data ) );

  // Tasks can be created before or after starting the RTOS
  xTaskCreate( gsm_scanner, "GSM_Scanner", 4096, NULL, tskIDLE_PRIORITY + 1, &tid_scanner );
  xTaskCreate( gsm_rx_main, "GSM_RX", 4096, NULL, tskIDLE_PRIORITY + 1, &tid_gsm_rx );
  xTaskCreate( gsm_tx_main, "GSM_TX", 4096, NULL, tskIDLE_PRIORITY + 1, &tid_gsm_tx );

  // Start the real time kernel with preemption.
  vTaskStartScheduler();
}
