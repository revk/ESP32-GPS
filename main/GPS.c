// Simple GPS
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)
static const char TAG[] = "GPS";

#include "revk.h"
#include <driver/i2c.h>
#include <driver/uart.h>
#include <math.h>
#include "oled.h"

#define settings	\
	s8(oledsda,27)	\
	s8(oledscl,14)	\
	s8(oledaddress,0x3D)	\
	u8(oledcontrast,127)	\
	u8(gpspps,34)	\
	u8(gpsuart,1)	\
	u8(gpsrx,33)	\
	u8(gpstx,32)	\
	u8(gpsfix,25)	\
	u8(gpsen,26)	\
	b(oledflip)	\
	b(f)	\

#define u32(n,d)	uint32_t n;
#define s8(n,d)	int8_t n;
#define u8(n,d)	int8_t n;
#define b(n) uint8_t n;
#define s(n,d) char * n;
settings
#undef u32
#undef s8
#undef u8
#undef b
#undef s
const char *
app_command (const char *tag, unsigned int len, const unsigned char *value)
{
   if (!strcmp (tag, "contrast"))
   {
      oled_set_contrast (atoi ((char *) value));
      return "";                // OK
   }
   return NULL;
}

void
app_main ()
{
   revk_init (&app_command);
#define b(n) revk_register(#n,0,sizeof(n),&n,NULL,SETTING_BOOLEAN);
#define u32(n,d) revk_register(#n,0,sizeof(n),&n,#d,0);
#define s8(n,d) revk_register(#n,0,sizeof(n),&n,#d,SETTING_SIGNED);
#define u8(n,d) revk_register(#n,0,sizeof(n),&n,#d,0);
#define s(n,d) revk_register(#n,0,0,&n,d,0);
   settings
#undef u32
#undef s8
#undef u8
#undef b
#undef s
      if (oledsda >= 0 && oledscl >= 0)
      oled_start (1, oledaddress, oledscl, oledsda, oledflip);
   // Init UART
   uart_config_t uart_config = {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
   };
   esp_err_t err;
   if ((err = uart_param_config (gpsuart, &uart_config)))
      revk_error (TAG, "UART param fail %s", esp_err_to_name (err));
   else if ((err = uart_set_pin (gpsuart, gpstx, gpsrx, -1, -1)))
      revk_error (TAG, "UART pin fail %s", esp_err_to_name (err));
   else if ((err = uart_driver_install (gpsuart, 1000, 0, 0, NULL, 0)))
      revk_error (TAG, "UART install fail %s", esp_err_to_name (err));
   else
      revk_info (TAG, "PN532 UART %d Tx %d Rx %d", gpsuart, gpstx, gpsrx);
   if (gpsen >= 0)
   {                            // Enable
      gpio_set_level (gpsen, 1);
      gpio_set_direction (gpsen, GPIO_MODE_OUTPUT);
   }
   // Main task...
   while (1)
   {
      uint8_t buf[1000];
      int l = uart_read_bytes (gpsuart, buf, 1000, 10);
      if (l < 0)
         sleep (1);
      else if (l > 0)
         revk_info ("Rx", "%d [%.*s]", l, l, buf);
   }
}
