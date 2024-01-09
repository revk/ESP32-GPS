// GPS logger
// Copyright (c) 2019-2024 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

// TODO
// TEST DATA
// DOP
// START/STOP
// SPEED/COURSE
// PACK

static __attribute__((unused))
     const char TAG[] = "GPS";

#include "revk.h"
#include <aes/esp_aes.h>
#include <driver/i2c.h>
#include <driver/uart.h>
#include <math.h>
#include "esp_sntp.h"
#include "esp_vfs_fat.h"
#include <sys/dirent.h>
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"

#ifdef	CONFIG_FATFS_LFN_NONE
#error Need long file names
#endif

//#if   FF_FS_EXFAT == 0
//#error Need EXFAT (ffconf.h)
//#endif

#define	SYSTEMS	3
     static const char system_code[SYSTEMS] = { 'P', 'L', 'A' };
static const char system_colour[SYSTEMS] = { 'g', 'Y', 'C' };
static const char *const system_name[SYSTEMS] = { "NAVSTAR", "GLONASS", "GALILEO" };

#define IO_MASK         0x3F
#define IO_INV          0x40
#define settings	\
     	io(pwr,-15,		System PWR)	\
     	io(charger,-33,		Charger status)	\
     	io(rgb,34,		RGB LED Strip)	\
     	u8f(leds,15,		RGB LEDs)	\
     	io(accsda,13,		Accellerometer SDA) \
     	io(accscl,13,		Accellerometer SCL) \
	b(gpsdebug,N,		GPS debug logging)	\
	u8(gpsuart,1,		GPS UART ID)	\
	io(gpsrx,5,		GPS Rx - Tx from GPS GPIO)	\
	io(gpstx,7,		GPS Tx - Rx yo GPS GPIO)	\
	io(gpstick,3,		GPS Tick GPIO)	\
        u32(gpsbaud,115200,	GPS Baud)	\
	io(sdss,8,		MicroSD SS)    \
        io(sdmosi,9,		MicroSD MOSI)     \
        io(sdsck,10,		MicroSD SCK)      \
        io(sdcd,-11,		MicroSD CD)      \
        io(sdmiso,12,		MicroSD MISO)     \
	u16(fixms,1000,		Fix rate)	\
        b(navstar,Y,            GPS track NAVSTAR GPS)  \
        b(glonass,Y,            GPS track GLONASS GPS)  \
        b(galileo,Y,            GPS track GALILEO GPS)  \
        b(waas,Y,               GPS enable WAAS)        \
        b(sbas,Y,               GPS enable SBAS)        \
        b(qzss,N,               GPS enable QZSS)        \
        b(aic,Y,                GPS enable AIC) \
        b(easy,Y,               GPS enable Easy)        \
	b(walking,N,            GPS Walking mode)       \
        b(flight,N,             GPS Flight mode)        \
        b(balloon,N,            GPS Balloon mode)       \
	b(logecef,Y,		Log ECEF data)		\
	b(logsats,Y,		Log Sats data)		\
	u16(packmin,60,		Min samples for pack)	\
	u16(packmax,600,	Max samples for pack)	\
	u16(packm,1,	 	Pack delta m)	\
	u16(packs,10,		Pack delta s)	\
	s(url,,			URL to post data)	\

#define u32(n,d,t)	uint32_t n;
#define u16(n,d,t)	uint16_t n;
#define s8(n,d,t)	int8_t n;
#define u8(n,d,t)	uint8_t n;
#define u8f(n,d,t)	uint8_t n;
#define b(n,d,t) uint8_t n;
#define bl(n,d,t) uint8_t n;
#define h(n,t) uint8_t *n;
#define s(n,d,t) char * n;
#define io(n,d,t)         uint8_t n;
settings
#undef io
#undef u16
#undef u32
#undef s8
#undef u8
#undef u8f
#undef bl
#undef b
#undef h
#undef s
const char sd_mount[] = "/sd";
static led_strip_handle_t strip = NULL;
static SemaphoreHandle_t cmd_mutex = NULL;
static SemaphoreHandle_t ack_mutex = NULL;
static int pmtk = 0;            // Waiting ack
static SemaphoreHandle_t fix_mutex = NULL;
static int gpserrorcount = 0;   // running count
static time_t gpszda = 0;       // Last ZDA
static int gpserrors = 0;       // last count

static struct
{
   uint8_t gpsstarted:1;        // GPS started
   uint8_t gpsinit:1;           // Init GPS
   uint8_t doformat:1;          // Format SD
   uint8_t dodismount:1;        // Stop SD until card removed
} volatile b = { 0 };

static struct
{                               // Status for LED
   uint8_t sat[SYSTEMS];        // Count per system
   uint8_t fixmode;             // Fix mode
} status;

typedef struct fix_s fix_t;
struct fix_s
{
   fix_t *next;                 // Next in queue
   fix_t *pair;                 // Pair for packing
   struct
   {                            // Earth centred Earth fixed, used for packing, etc, and time stamp (us)
      int64_t x,
        y,
        z,
        t;
   } ecef;
   double lat,
     lon,
     alt;                       // Lat/lon/alt
   float course;
   float speed;
   float hepe;
   float vepe;
   uint8_t sats;                // Sats in use
   uint8_t fixmode;             // Fix mode 1-3
   uint8_t sat[SYSTEMS];        // Sat visible count per system
   uint8_t sett:1;
   uint8_t setsat:1;
   uint8_t setecef:1;
   uint8_t setlla:1;
   uint8_t setcs:1;
   uint8_t setepe:1;
};

typedef struct fixq_s fixq_t;
struct fixq_s
{
   fix_t *base;
   fix_t *last;
   uint32_t count;
};

fixq_t fixlog = { 0 };          // Queue to log
fixq_t fixpack = { 0 };         // Queue to pack
fixq_t fixsd = { 0 };           // Queue to record to SD
fixq_t fixfree = { 0 };         // Queue of free

fix_t *
fixadd (fixq_t * q, fix_t * f)
{
   if (!f)
      return NULL;
   xSemaphoreTake (fix_mutex, portMAX_DELAY);
   if (q->base)
      q->last->next = f;
   else
      q->base = f;
   q->last = f;
   q->count++;
   xSemaphoreGive (fix_mutex);
   return NULL;
}

fix_t *
fixget (fixq_t * q)
{
   fix_t *f = NULL;
   xSemaphoreTake (fix_mutex, portMAX_DELAY);
   if (q->base)
   {
      f = q->base;
      q->base = f->next;
      f->next = NULL;
      q->count--;
   }
   xSemaphoreGive (fix_mutex);
   return f;
}

fix_t *
fixnew (void)
{
   fix_t *f = fixget (&fixfree);
   if (!f)
   {
      f = mallocspi (sizeof (*f));
      if (f)
         memset (f, 0, sizeof (*f));
   }
   return f;
}

time_t
timegm (struct tm *tm)
{                               // Fucking linux time functions
   /* days before the month */
   static const unsigned short moff[12] = {
      0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
   };

   /* minimal sanity checking not to access outside of the array */
   if (!tm || (unsigned) tm->tm_mon >= 12)
      return (time_t) - 1;

   int y = tm->tm_year + 1900 - (tm->tm_mon < 2);
   int nleapdays = y / 4 - y / 100 + y / 400 - ((1970 - 1) / 4 - (1970 - 1) / 100 + (1970 - 1) / 400);
   time_t t =
      ((((time_t) (tm->tm_year - (1970 - 1900)) * 365 + moff[tm->tm_mon] + tm->tm_mday - 1 + nleapdays) * 24 + tm->tm_hour) * 60 +
       tm->tm_min) * 60 + tm->tm_sec;
   return (t < 0 ? (time_t) - 1 : t);
}

static uint32_t gpsbaudnow = 0;
void
gps_connect (unsigned int baud)
{                               // GPS connection
   esp_err_t err = 0;
   if (gpsbaudnow)
      uart_driver_delete (gpsuart);
   gpsbaudnow = baud;
   uart_config_t uart_config = {
      .baud_rate = baud,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
   };
   if (!err)
      err = uart_param_config (gpsuart, &uart_config);
   if (!err)
      err = uart_set_pin (gpsuart, gpstx & IO_MASK, gpsrx & IO_MASK, -1, -1);
   if (!err)
      err = uart_driver_install (gpsuart, 1024, 0, 0, NULL, 0);
   // TODO report error
   uart_write_bytes (gpsuart, "\r\n\r\n", 4);
}

void
gps_cmd (const char *fmt, ...)
{                               // Send command to UART
   if (pmtk)
      xSemaphoreTake (ack_mutex, 1000 * portTICK_PERIOD_MS);    // Wait for ACK from last command
   char s[100];
   va_list ap;
   va_start (ap, fmt);
   vsnprintf (s, sizeof (s) - 5, fmt, ap);
   va_end (ap);
   uint8_t c = 0;
   char *p;
   for (p = s + 1; *p; p++)
      c ^= *p;
   if (*s == '$')
      p += sprintf (p, "*%02X\r\n", c); // We allowed space
   xSemaphoreTake (cmd_mutex, portMAX_DELAY);
   uart_write_bytes (gpsuart, s, p - s);
   xSemaphoreGive (cmd_mutex);
   if (!strncmp (s, "$PMTK", 5))
   {
      xSemaphoreTake (ack_mutex, 0);
      pmtk = atoi (s + 5);
   } else
      pmtk = 0;
}

const char *
app_callback (int client, const char *prefix, const char *target, const char *suffix, jo_t j)
{
   if (client || !prefix || target || strcmp (prefix, prefixcommand) || !suffix)
      return NULL;              //Not for us or not a command from main MQTT
   char value[1000];
   int len = 0;
   if (j)
   {
      len = jo_strncpy (j, value, sizeof (value));
      if (len < 0)
         return "Expecting JSON string";
      if (len > sizeof (value))
         return "Too long";
   }
   if (!strcmp (suffix, "format"))
   {
      b.doformat = 1;
      return "";
   }
   if (!strcmp (suffix, "dismount") || !strcmp (suffix, "restart"))
   {
      b.dodismount = 1;
      return "";
   }
   if (!strcmp (suffix, "wifi"))
   {                            // WiFi connected, but not need for SNTP as we have GPS
      if (gpszda)
         esp_sntp_stop ();
      return "";
   }
   if (!strcmp (suffix, "disconnect"))
   {
      return "";
   }
   if (!strcmp (suffix, "connect"))
   {
      return "";
   }
   if (!strcmp (suffix, "gpstx") && len)
   {                            // Send arbitrary GPS command (do not include *XX or CR/LF)
      gps_cmd ("%s", value);
      return "";
   }
   if (!strcmp (suffix, "hot"))
   {
      gps_cmd ("$PMTK101");     // Hot start
      b.gpsstarted = 0;
      return "";
   }
   if (!strcmp (suffix, "warm"))
   {
      gps_cmd ("$PMTK102");     // Warm start
      b.gpsstarted = 0;
      return "";
   }
   if (!strcmp (suffix, "cold"))
   {
      gps_cmd ("$PMTK103");     // Cold start
      b.gpsstarted = 0;
      return "";
   }
   if (!strcmp (suffix, "reset"))
   {
      gps_cmd ("$PMTK104");     // Full cold start (resets to default settings including Baud rate)
      revk_restart ("GPS has been reset", 1);
      return "";
   }
   if (!strcmp (suffix, "sleep"))
   {
      gps_cmd ("$PMTK291,7,0,10000,1"); // Low power (maybe we need to drive EN pin?)
      return "";
   }
   if (!strcmp (suffix, "version"))
   {
      gps_cmd ("$PMTK605");     // Version
      return "";
   }
   return NULL;
}

static void
gps_init (void)
{                               // Set up GPS
   b.gpsinit = 0;
   if (gpsbaudnow != gpsbaud)
   {
      gps_cmd ("$PQBAUD,W,%d", gpsbaud);        // 
      gps_cmd ("$PMTK251,%d", gpsbaud); // Required Baud rate set
      sleep (1);
      gps_connect (gpsbaud);
   }
   gps_cmd ("$PMTK286,%d", aic ? 1 : 0);        // AIC
   gps_cmd ("$PMTK353,%d,%d,%d,0,0", navstar, glonass, galileo);
   gps_cmd ("$PMTK352,%d", qzss ? 0 : 1);       // QZSS (yes, 1 is disable)
   gps_cmd ("$PQTXT,W,0,1");    // Disable TXT
   gps_cmd ("$PQECEF,W,1,1");   // Enable ECEF
   //gps_cmd ("$PQODO,W,1");   // Enable ODO
   gps_cmd ("$PQEPE,W,1,1");    // Enable EPE
   gps_cmd ("$PMTK886,%d", balloon ? 3 : flight ? 2 : walking ? 1 : 0); // FR mode
   // Queries - responses prompt settings changes if needed
   gps_cmd ("$PMTK414");        // Q_NMEA_OUTPUT
   gps_cmd ("$PMTK400");        // Q_FIX
   gps_cmd ("$PMTK401");        // Q_DGPS
   gps_cmd ("$PMTK413");        // Q_SBAS
   gps_cmd ("$PMTK869,0");      // Query EASY
   //gps_cmd ("$PMTK605");     // Q_RELEASE
   b.gpsstarted = 1;
}

static void
nmea (char *s)
{
   ESP_LOGE (TAG, "GPS %s", s); // TODO
   if (!s || *s != '$' || !s[1] || !s[2] || !s[3])
      return;
   char *f[50];
   int n = 0;
   s++;
   while (n < sizeof (f) / sizeof (*f))
   {
      f[n++] = s;
      while (*s && *s != ',')
         s++;
      if (!*s || *s != ',')
         break;
      *s++ = 0;
   }
   if (!n)
      return;
   int64_t parse (const char *p, uint8_t places)
   {
      if (!p || !*p)
         return 0;
      const char *s = p;
      if (*p == '-')
         p++;
      int64_t v = 0;
      while (isdigit ((int) *p))
         v = v * 10 + *p++ - '0';
      if (*p == '.')
      {
         p++;
         while (places && isdigit ((int) *p))
         {
            v = v * 10 + *p++ - '0';
            places--;
         }
      }
      while (places)
      {
         v *= 10;
         places--;
      }
      if (*s == '-')
         v = 0 - v;
      return v;
   }
   static fix_t *fix = NULL;
   static uint32_t fixtod = -1;
   void startfix (const char *tod)
   {
      uint32_t newtod = (tod ? parse (tod, 3) : -1);
      if (fix && fixtod == newtod)
         return;                // Same fix
      fixtod = newtod;
      if (fix)
      {
         if (fix->setsat)
         {
            status.fixmode = fix->fixmode;
            for (int s = 0; s < SYSTEMS; s++)
               status.sat[s] = fix->sat[s];
         }
         fix = fixadd (&fixlog, fix);
      }
      if (tod)
         fix = fixnew ();
      // TODO should we copy sats and mode?
   }
   if (!b.gpsstarted && *f[0] == 'G' && !strcmp (f[0] + 2, "GGA") && (esp_timer_get_time () > 10000000 || !revk_link_down ()))
      b.gpsinit = 1;            // Time to send init
   if (!strcmp (f[0], "PMTK001") && n >= 3)
   {                            // ACK
      int tag = atoi (f[1]);
      if (pmtk && pmtk == tag)
      {                         // ACK received
         xSemaphoreGive (ack_mutex);
         pmtk = 0;
      }
      return;
   }
   if (!strcmp (f[0], "PQTXT"))
      return;                   // ignore
   if (!strcmp (f[0], "PQECEF"))
      return;                   // ignore
   if (*f[0] == 'G' && !strcmp (f[0] + 2, "GLL"))
      return;                   // ignore
   if (*f[0] == 'G' && !strcmp (f[0] + 2, "RMC") && n >= 13 && strlen (f[9]) == 6)
   {
      startfix (f[1]);
      struct tm t = { };
      t.tm_year = (f[9][4] - '0') * 10 + f[9][5] - '0' + 100;
      t.tm_mon = (f[9][2] - '0') * 10 + f[9][3] - '0' - 1;
      t.tm_mday = (f[9][0] - '0') + f[9][1] - '0';
      t.tm_hour = fixtod / 10000000 % 100;
      t.tm_min = fixtod / 100000 % 100;
      t.tm_sec = fixtod / 1000 % 100;
      fix->ecef.t = 1000000LL * timegm (&t) + (fixtod % 1000LL) * 1000LL;
      fix->sett = 1;
      return;                   // ignore
   }
   if (!strcmp (f[0], "PMTK010"))
      return;                   // Started, happens at end of init anyway
   if (!strcmp (f[0], "PMTK011"))
      return;                   // Message! Ignore
   if (!strcmp (f[0], "PQEPE") && n >= 3)
   {                            // Estimated position error
      if (fix)
      {
         fix->hepe = strtof (f[1], NULL);
         fix->vepe = strtof (f[2], NULL);
         fix->setepe = 1;
      }
      return;
   }
   if (!strcmp (f[0], "ECEFPOSVEL") && n >= 7)
   {
      startfix (f[1]);
      if (fix && strlen (f[2]) > 6 && strlen (f[3]) > 6 && strlen (f[4]) > 6)
      {
         fix->ecef.x = parse (f[2], 6);
         fix->ecef.y = parse (f[3], 6);
         fix->ecef.z = parse (f[4], 6);
         fix->setecef = 1;
      }
      return;
   }
   if (!strcmp (f[0], "PMTK869") && n >= 4)
   {                            // Set EASY
      if (atoi (f[1]) == 2 && atoi (f[2]) != easy)
         gps_cmd ("$PMTK869,1,%d", easy ? 1 : 0);
      return;
   }
   if (!strcmp (f[0], "PMTK513") && n >= 2)
   {                            // Set SBAS
      if (atoi (f[1]) != sbas)
         gps_cmd ("$PMTK313,%d", sbas ? 1 : 0);
      return;
   }
   if (!strcmp (f[0], "PMTK501") && n >= 2)
   {                            // Set DGPS
      if (atoi (f[1]) != ((sbas || waas) ? 2 : 0))
         gps_cmd ("$PMTK301,%d", (sbas || waas) ? 2 : 0);
      return;
   }
   if (!strcmp (f[0], "PMTK500") && n >= 2)
   {                            // Fix rate
      if (atoi (f[1]) != fixms)
         gps_cmd ("$PMTK220,%d", fixms);
      return;
   }
   if (!strcmp (f[0], "PMTK705") && n >= 2)
      return;                   // Ignore
   if (!strcmp (f[0], "PMTK514") && n >= 2)
   { // TODO review timing?
      unsigned int rates[19] = { 0 };
      rates[2] = (1000 / fixms ? : 1);  // VTG
      rates[3] = 1;             // GGA
      rates[4] = (10000 / fixms ? : 1); // GSA
      rates[5] = (10000 / fixms ? : 1); // GSV
      rates[17] = (10000 / fixms ? : 1);        // ZDA
      int q;
      for (q = 0; q < sizeof (rates) / sizeof (*rates) && rates[q] == (1 + q < n ? atoi (f[1 + q]) : 0); q++);
      if (q < sizeof (rates) / sizeof (*rates)) // Set message rates
         gps_cmd ("$PMTK314,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", rates[0], rates[1], rates[2], rates[3],
                  rates[4], rates[5], rates[6], rates[7], rates[8], rates[9], rates[10], rates[11], rates[12], rates[13], rates[14],
                  rates[15], rates[16], rates[17], rates[18], rates[19]);
      return;
   }
   if (*f[0] == 'G' && !strcmp (f[0] + 2, "GGA") && n >= 14)
   {                            // Fix: $GPGGA,093644.000,5125.1569,N,00046.9708,W,1,09,1.06,100.3,M,47.2,M,,
      startfix (f[1]);
      if (fix)
      {
         fix->sats = atoi (f[7]);
         fix->alt = strtod (f[9], NULL);
         if (strlen (f[2]) >= 9 && strlen (f[4]) >= 10)
         {
            fix->lat = ((f[2][0] - '0') * 10 + f[2][1] - '0' + strtod (f[2] + 2, NULL) / 60) * (f[3][0] == 'N' ? 1 : -1);
            fix->lon =
               ((f[4][0] - '0') * 100 + (f[4][1] - '0') * 10 + f[4][2] - '0' + strtod (f[4] + 3, NULL) / 60) * (f[5][0] ==
                                                                                                                'E' ? 1 : -1);
            fix->setlla = 1;
         }
      }
      return;
   }

   if (*f[0] == 'G' && !strcmp (f[0] + 2, "ZDA") && n >= 5)
   {                            // Time: $GPZDA,093624.000,02,11,2019,,
      startfix (f[1]);
      gpserrors = gpserrorcount;
      gpserrorcount = 0;
      if (strlen (f[1]) == 10 && atoi (f[4]) > 2000)
      {
         struct tm t = { };
         struct timeval v = { };
         t.tm_year = atoi (f[4]) - 1900;
         t.tm_mon = atoi (f[3]) - 1;
         t.tm_mday = atoi (f[2]);
         t.tm_hour = (f[1][0] - '0') * 10 + f[1][1] - '0';
         t.tm_min = (f[1][2] - '0') * 10 + f[1][3] - '0';
         t.tm_sec = (f[1][4] - '0') * 10 + f[1][5] - '0';
         v.tv_usec = atoi (f[1] + 7) * 1000;
         v.tv_sec = timegm (&t);
         if (!gpszda)
            esp_sntp_stop ();
         gpszda = v.tv_sec;
         settimeofday (&v, NULL);
      }
      return;
   }
   if (*f[0] == 'G' && !strcmp (f[0] + 2, "VTG") && n >= 10)
   {
      if (fix)
      {
         fix->course = strtof (f[1], NULL);
         fix->speed = strtof (f[7], NULL);
         fix->setcs = 1;
      }
      return;
   }
   if (*f[0] == 'G' && !strcmp (f[0] + 2, "GSA") && n >= 18)
   {
      if (fix)
      {
         fix->fixmode = atoi (f[2]);
         // TODO DOP?
      }
      return;
   }
   if (*f[0] == 'G' && !strcmp (f[0] + 2, "GSV") && n >= 4)
   {
      int n = atoi (f[3]);
      if (fix && n)
      {
         fix->setsat = 1;
         for (int s = 0; s < SYSTEMS; s++)
            if (f[0][1] == system_code[s])
               fix->sat[s] = n;
      }
      return;
   }
}

void
nmea_task (void *z)
{
   uint8_t buf[1000],
    *p = buf;
   uint64_t timeout = esp_timer_get_time () + 10000000;
   while (1)
   {
      if (b.gpsinit)
         gps_init ();
      // Get line(s), the timeout should mean we see one or more whole lines typically
      int l = uart_read_bytes (gpsuart, p, buf + sizeof (buf) - p, 10 / portTICK_PERIOD_MS);
      if (l <= 0)
      {
         if (timeout && timeout < esp_timer_get_time ())
         {
            b.gpsstarted = 0;
            static int rate = 0;
            const uint32_t rates[] = { 4800, 9600, 14400, 19200, 38400, 57600, 115200 };
            rate++;
            if (rate == sizeof (rates) / sizeof (*rates))
               rate = 0;
            gps_connect (rates[rate]);
#if 1
            ESP_LOGE (TAG, "GPS silent, trying %ld", gpsbaudnow);
#endif
            timeout = esp_timer_get_time () + 2000000 + fixms;
         }
         continue;
      }
      uint8_t *e = p + l;
      p = buf;
      while (p < e)
      {
         uint8_t *l = p;
         while (l < e && *l >= ' ')
            l++;
         if (l == e)
            break;
         if (*p == '$' && (l - p) >= 4 && l[-3] == '*' && isxdigit (l[-2]) && isxdigit (l[-1]))
         {
            // Checksum
            uint8_t c = 0,
               *x;
            for (x = p + 1; x < l - 3; x++)
               c ^= *x;
            if (((c >> 4) > 9 ? 7 : 0) + (c >> 4) + '0' != l[-2] || ((c & 0xF) > 9 ? 7 : 0) + (c & 0xF) + '0' != l[-1])
               gpserrorcount++;
            else
            {                   // Process line
               timeout = esp_timer_get_time () + 60000000 + fixms;
               l[-3] = 0;
               nmea ((char *) p);
            }
         }
         while (l < e && *l < ' ')
            l++;
         p = l;
      }
      if (p < e && (e - p) < sizeof (buf))
      {                         // Partial line
         memmove (buf, p, e - p);
         p = buf + (e - p);
         continue;
      }
      p = buf;                  // Start from scratch
   }
}

jo_t
log_line (fix_t * f)
{                               // generate log line
   jo_t j = jo_object_alloc ();
   if (f->sett)
   {
      struct tm t;
      time_t now = f->ecef.t / 1000000LL;
      gmtime_r (&now, &t);
      uint32_t ms = f->ecef.t / 1000LL % 1000LL;
      char temp[100],
       *p = temp;
      p += sprintf (p, "%04d-%02d-%02dT%02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
      if (ms)
         p += sprintf (p, ".%03ld", ms);
      *p++ = 'Z';
      *p = 0;
      jo_stringf (j, "ts", temp);
   }
   if (f->setsat && logsats)
   {
      jo_object (j, "sats");
      for (int s = 0; s < SYSTEMS; s++)
         if (f->sat[s])
            jo_int (j, system_name[s], f->sat[s]);
      jo_int (j, "fix", f->fixmode);
      jo_int (j, "used", f->sats);
      jo_close (j);
   }
   if (f->setlla)
   {
      jo_litf (j, "lat", "%lf", f->lat);
      jo_litf (j, "lon", "%lf", f->lon);
      if (f->fixmode >= 3)
         jo_litf (j, "alt", "%lf", f->alt);
   }
   if (f->setecef && logecef)
   {
      void o (const char *t, int64_t v)
      {
         char *s = "";
         if (v < 0)
         {
            v = 0 - v;
            s = "-";
         }
         jo_litf (j, t, "%s%lld.%06lld", s, v / 1000000LL, v % 1000000LL);
      }
      jo_object (j, "ecef");
      o ("x", f->ecef.x);
      o ("y", f->ecef.y);
      o ("z", f->ecef.z);
      if (f->sett)
         o ("t", f->ecef.t);
      jo_close (j);
   }
   if (gpserrors)
   {
      jo_int (j, "errors", gpserrors);
      gpserrors = 0;
   }
   return j;
}

void
log_task (void *z)
{                               // Log via MQTT
   while (1)
   {
      if (!fixlog.count || (revk_link_down () && fixlog.count < 100))
      {
         sleep (1);
         continue;
      }
      fix_t *f = fixget (&fixlog);
      if (!f)
      {
         sleep (1);             // TODO
         continue;
      }
      jo_t j = log_line (f);
      revk_info ("GPS", &j);
      fixadd (&fixpack, f);
   }
}

void
pack_task (void *z)
{
   while (1)
   {
      fix_t *f = fixget (&fixpack);
      if (!f)
      {
         sleep (1);
         continue;
      }
      // TODO packing
      // TODO start and stop moving
      fixadd (&fixsd, f);
   }
}

void
checkupload (void)
{
   if (revk_link_down () || !*url)
      return;
   DIR *dir = opendir (sd_mount);
   if (!dir)
      return;
   while (1)
   {
      char zap = 0;
      char *filename = NULL;
      struct dirent *entry;
      while (!filename && (entry = readdir (dir)))
         if (entry->d_type == DT_REG)
            asprintf (&filename, "%s/%s", sd_mount, entry->d_name);
      closedir (dir);
      struct stat s = { 0 };
      if (filename && *filename && !stat (filename, &s))
      {
         ESP_LOGE (TAG, "Send %s", filename);
         if (!s.st_size)
            zap = 1;            // Empty
         else
         {
            FILE *i = fopen (filename, "r");
            if (i)
            {
               esp_http_client_config_t config = {
                  .url = url,
                  .crt_bundle_attach = esp_crt_bundle_attach,
                  .method = HTTP_METHOD_POST,
                  .query = revk_id,
               };
               char *buf = mallocspi (1024);
               int response = 0;
               esp_http_client_handle_t client = esp_http_client_init (&config);
               if (client)
               {
                  esp_http_client_set_header (client, "Content-Type", "application/json");
                  if (!esp_http_client_open (client, s.st_size))
                  {             // Send
                     int len = 0;
                     while ((len = fread (buf, 1, 1024, i)) > 0)
                        esp_http_client_write (client, buf, len);
                     esp_http_client_fetch_headers (client);
                     esp_http_client_flush_response (client, &len);
                     response = esp_http_client_get_status_code (client);
                     esp_http_client_close (client);
                  }
                  esp_http_client_cleanup (client);
               }
               sleep (1);
               if (response / 100 == 2)
               {
                  jo_t j = jo_object_alloc ();
                  jo_string (j, "filename", filename);
                  jo_string (j, "url", url);
                  jo_int (j, "size", s.st_size);
                  revk_info ("Uploaded", &j);
                  zap = 1;
               } else
               {
                  jo_t j = jo_object_alloc ();
                  jo_string (j, "error", "Failed to upload");
                  jo_string (j, "filename", filename);
                  jo_string (j, "url", url);
                  jo_int (j, "size", s.st_size);
                  jo_int (j, "response", response);
                  revk_error ("Upload", &j);
               }
               fclose (i);
            }
         }
      }
      if (zap)
         unlink (filename);
      free (filename);
      if (!zap)
         return;
   }
}

void
sd_task (void *z)
{
   void wait (int s)
   {
      while (s--)
      {
         while (fixsd.count > packmax)
            fixadd (&fixfree, fixget (&fixsd)); // Discard as too many waiting
         sleep (1);
      }
   }
   esp_err_t ret;
   if (sdcd)
   {
      gpio_reset_pin (sdcd & IO_MASK);
      gpio_set_direction (sdcd & IO_MASK, GPIO_MODE_INPUT);
   }
#if 0                           // Should be done by system
   if (sdmosi)
      gpio_set_direction (sdmosi & IO_MASK, GPIO_MODE_OUTPUT);
   if (sdss)
      gpio_set_direction (sdss & IO_MASK, GPIO_MODE_OUTPUT);
   if (sdsck)
      gpio_set_direction (sdsck & IO_MASK, GPIO_MODE_OUTPUT);
   if (sdmiso)
      gpio_set_direction (sdmiso & IO_MASK, GPIO_MODE_INPUT);
#endif
   // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
   // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 20MHz for SDSPI)
   // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
   sdmmc_host_t host = SDSPI_HOST_DEFAULT ();
   host.max_freq_khz = SDMMC_FREQ_PROBING;

   spi_bus_config_t bus_cfg = {
      .mosi_io_num = sdmosi & IO_MASK,
      .miso_io_num = sdmiso & IO_MASK,
      .sclk_io_num = sdsck & IO_MASK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4000,
   };
   ret = spi_bus_initialize (host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
   if (ret != ESP_OK)
   {
      jo_t j = jo_object_alloc ();
      jo_string (j, "error", "SPI failed");
      jo_int (j, "code", ret);
      jo_int (j, "MOSI", sdmosi & IO_MASK);
      jo_int (j, "MISO", sdmiso & IO_MASK);
      jo_int (j, "CLK", sdsck & IO_MASK);
      revk_error ("SD", &j);
      vTaskDelete (NULL);
      return;
   }
   while (1)
   {
      if (sdcd)
      {
         if (b.dodismount)
         {
            jo_t j = jo_object_alloc ();
            jo_string (j, "action", "Remove card");
            revk_info ("SD", &j);
            b.dodismount = 0;
            while (gpio_get_level (sdcd & IO_MASK) == ((sdcd & IO_INV) ? 0 : 1))
               wait (1);
            continue;
         }
         if (gpio_get_level (sdcd & IO_MASK) != ((sdcd & IO_INV) ? 0 : 1))
         {
            jo_t j = jo_object_alloc ();
            jo_string (j, "error", "Card not present");
            revk_error ("SD", &j);
         }
         while (gpio_get_level (sdcd & IO_MASK) != ((sdcd & IO_INV) ? 0 : 1))
         {
            wait (1);
            // TODO flushing if too much logged
         }
      } else if (b.dodismount)
      {
         b.dodismount = 0;
         wait (60);
         continue;
      }

      wait (1);

      esp_vfs_fat_sdmmc_mount_config_t mount_config = {
         .format_if_mount_failed = 1,
         .max_files = 1,
         .allocation_unit_size = 16 * 1024
      };
      sdmmc_card_t *card;
      ESP_LOGI (TAG, "Initializing SD card");

      sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT ();
      slot_config.gpio_cs = sdss & IO_MASK;
      slot_config.host_id = host.slot;

      ESP_LOGI (TAG, "Mounting filesystem");
      ret = esp_vfs_fat_sdspi_mount (sd_mount, &host, &slot_config, &mount_config, &card);

      if (ret != ESP_OK)
      {
         jo_t j = jo_object_alloc ();
         if (ret == ESP_FAIL)
            jo_string (j, "error", "Failed to mount");
         else
            jo_string (j, "error", "Failed to iniitialise");
         jo_int (j, "code", ret);
         revk_error ("SD", &j);
         if (sdcd)
         {
            int try = 60;
            while (try-- && gpio_get_level (sdcd & IO_MASK) == ((sdcd & IO_INV) ? 0 : 1))
               wait (1);
         } else
            wait (60);
         continue;
      }
      ESP_LOGI (TAG, "Filesystem mounted");

      if (b.doformat && (ret = esp_vfs_fat_sdcard_format (sd_mount, card)))
      {
         jo_t j = jo_object_alloc ();
         jo_string (j, "error", "Failed to format");
         jo_int (j, "code", ret);
         revk_error ("SD", &j);
      }
      {
         jo_t j = jo_object_alloc ();
         jo_string (j, "action", b.doformat ? "formatted" : "mounted");
         // TODO size?
         revk_info ("SD", &j);
      }
      b.doformat = 0;

      checkupload ();

      FILE *o = NULL;
      int line = 0;
      char filename[100];
      while (!b.doformat && !b.dodismount)
      {
         if (sdcd && gpio_get_level (sdcd & IO_MASK) != ((sdcd & IO_INV) ? 0 : 1))
            break;
         fix_t *f = fixget (&fixsd);
         if (!f)
         {
            sleep (1);
            continue;
         }
         // TODO checking available space
         // TODO moving or not - close file when not moving
         // TODO uploading and deleting files.
         if (!o && f->sett && f->fixmode > 1)
         {
            struct tm t;
            time_t now = f->ecef.t / 1000000LL;
            gmtime_r (&now, &t);
            sprintf (filename, "%s/%04d-%02d-%02dT%02d-%02d-%02d.json", sd_mount, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                     t.tm_hour, t.tm_min, t.tm_sec);
            o = fopen (filename, "w");
            if (!o)
            {
               jo_t j = jo_object_alloc ();
               jo_string (j, "error", "Failed to create file");
               jo_string (j, "filename", filename);
               revk_error ("SD", &j);
            } else
            {
               jo_t j = jo_object_alloc ();
               jo_string (j, "action", "Log file created");
               jo_string (j, "filename", filename);
               revk_info ("SD", &j);
               fprintf (o, "[\n");
            }
            line = 0;
         }
         if (o)
         {
            jo_t j = log_line (f);
            char *l = jo_finisha (&j);
            if (line++)
               fprintf (o, ",\n");
            fprintf (o, " %s", l);
            free (l);
         } else
            checkupload ();
         fixadd (&fixfree, f);  // Discard
      }
      if (o)
      {
         fprintf (o, "\n]\n");
         fclose (o);
      }
      checkupload ();
      // All done, unmount partition and disable SPI peripheral
      esp_vfs_fat_sdcard_unmount (sd_mount, card);
      ESP_LOGI (TAG, "Card unmounted");
      {
         jo_t j = jo_object_alloc ();
         jo_string (j, "action", "dismounted");
         revk_info ("SD", &j);
      }
   }
}

void
rgb_task (void *z)
{
   uint8_t blink = 0;
   while (1)
   {
      if (++blink > 3)
         blink = 0;
      usleep (200000);
      int l = 0;
      if (status.fixmode >= blink)
      {
         for (int s = 0; s < SYSTEMS; s++)
         {                      // Two GPS per sat
            for (int n = 0; n < status.sat[s] / 2 && l < leds; n++)
               revk_led (strip, l++, 255, revk_rgb (system_colour[s]));
            if ((status.sat[s] & 1) && l < leds)
               revk_led (strip, l++, 127, revk_rgb (system_colour[s]));
         }
         if (!l)
            revk_led (strip, l++, 255, revk_rgb ('R')); // No sats
      }
      while (l < leds)
         revk_led (strip, l++, 255, revk_rgb ('K'));
      led_strip_refresh (strip);
   }
}

void
app_main ()
{
   revk_boot (&app_callback);
   cmd_mutex = xSemaphoreCreateBinary ();
   xSemaphoreGive (cmd_mutex);
   fix_mutex = xSemaphoreCreateBinary ();
   xSemaphoreGive (fix_mutex);
   ack_mutex = xSemaphoreCreateBinary ();
#define str(x) #x
   revk_register ("gps", 0, sizeof (gpsrx), &gpsrx, NULL, SETTING_SECRET);
   revk_register ("sd", 0, sizeof (sdmosi), &sdmosi, "- 9", SETTING_SET | SETTING_BITFIELD | SETTING_FIX);
#define b(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,SETTING_BOOLEAN);
#define bl(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,SETTING_BOOLEAN|SETTING_LIVE);
#define h(n,t) revk_register(#n,0,0,&n,NULL,SETTING_BINDATA|SETTING_HEX);
#define u32(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,0);
#define u16(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,0);
#define s8(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,SETTING_SIGNED);
#define u8(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,0);
#define u8f(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,SETTING_FIX);
#define s(n,d,t) revk_register(#n,0,0,&n,str(d),0);
#define io(n,d,t)         revk_register(#n,0,sizeof(n),&n,"- "str(d),SETTING_SET|SETTING_BITFIELD|SETTING_FIX);
   settings;
#undef ui
#undef u16
#undef u32
#undef s8
#undef u8
#undef u8
#undef bl
#undef b
#undef h
#undef s
#undef str
   revk_start ();
   if (leds && rgb)
   {
      led_strip_config_t strip_config = {
         .strip_gpio_num = (rgb & IO_MASK),
         .max_leds = leds,
         .led_pixel_format = LED_PIXEL_FORMAT_GRB,      // Pixel format of your LED strip
         .led_model = LED_MODEL_WS2812, // LED strip model
         .flags.invert_out = ((rgb & IO_INV) ? 1 : 0),  // whether to invert the output signal (useful when your hardware has a level inverter)
      };
      led_strip_rmt_config_t rmt_config = {
         .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
         .resolution_hz = 10 * 1000 * 1000,     // 10MHz
         .flags.with_dma = true,
      };
      REVK_ERR_CHECK (led_strip_new_rmt_device (&strip_config, &rmt_config, &strip));
      if (strip)
         revk_task ("RGB", rgb_task, NULL, 4);
   }
   if (charger)
   {
      gpio_reset_pin (charger & IO_MASK);
      gpio_set_direction (charger & IO_MASK, GPIO_MODE_INPUT);
   }
   if (pwr)
   {                            // System power
      gpio_set_level (pwr & IO_MASK, (pwr & IO_INV) ? 0 : 1);
      gpio_set_direction (pwr & IO_MASK, GPIO_MODE_OUTPUT);
   }
   // Main task...
   if (gpstick)
   {
      gpio_reset_pin (gpstick & IO_MASK);
      gpio_set_direction (gpstick & IO_MASK, GPIO_MODE_INPUT);
   }
   gps_connect (gpsbaud);
   revk_task ("NMEA", nmea_task, NULL, 4);
   revk_task ("Log", log_task, NULL, 4);
   revk_task ("Pack", pack_task, NULL, 4);
   revk_task ("SD", sd_task, NULL, 4);
}
