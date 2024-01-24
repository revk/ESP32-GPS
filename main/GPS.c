// GPS logger
// Copyright (c) 2019-2024 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

// TODO email upload option

__attribute__((unused))
     const char TAG[] = "GPS";

#include "revk.h"
#include "esp_sleep.h"
#include <aes/esp_aes.h>
#include <driver/i2c.h>
#include <driver/uart.h>
#include <driver/rtc_io.h>
#include <math.h>
#include "esp_sntp.h"
#include "esp_vfs_fat.h"
#include <sys/dirent.h>
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include <driver/i2c.h>
#include "email.h"

#ifdef	CONFIG_FATFS_LFN_NONE
#error Need long file names
#endif

//#define       POSTCODEDEBUG   // Debug for postcode lookup
#define	ODOBASE	100000000

#define	SYSTEMS	3

     const char system_code[SYSTEMS] = { 'P', 'L', 'A' };
const char system_colour[SYSTEMS] = { 'G', 'Y', 'C' };
const char *const system_name[SYSTEMS] = { "NAVSTAR", "GLONASS", "GALILEO" };

#define	I2CPORT	0
#define	BATSCALE	3       // Pot divide on battery voltage (ADC1)

#define IO_MASK         0x3F
#define IO_INV          0x40
#define settings	\
     	io(pwr,-15,		System PWR)	\
     	io(charging,-4,		Charging status)	\
     	io(usb,,		USB power status)	\
	bl(powerman,N,		Power management)	\
	bl(powerstop,N,		Power off end journey)	\
     	io(rgb,2,		RGB LED Strip)	\
     	u8f(leds,17,		RGB LEDs)	\
	s32al(home,3,		Home location) \
	u8(homem,50,		Home proximity)	\
	bf(ledsd,1,		First RGB is for SD)	\
	u8f(accaddress,0x19,	Accelerometer I2C ID)	\
     	io(accsda,13,		Accelerometer SDA) \
     	io(accscl,14,		Accelerometer SCL) \
	bl(gpsdebug,N,		GPS debug logging)	\
	u8(gpsuart,1,		GPS UART ID)	\
	io(gpsrx,5,		GPS Rx - Tx from GPS GPIO)	\
	io(gpstx,7,		GPS Tx - Rx yo GPS GPIO)	\
	io(gpstick,3,		GPS Tick GPIO)	\
        u32(gpsbaud,115200,	GPS Baud)	\
	u16l(move,30,		Seconds moving to start if slow) \
	u16l(stop,120,		Seconds not moving to stop if not home) \
	bf(sdled,N,		First LED is SD)	\
	io(sdss,8,		MicroSD SS)    \
        io(sdmosi,9,		MicroSD MOSI)     \
        io(sdsck,10,		MicroSD SCK)      \
        io(sdcd,-11,		MicroSD CD)      \
        io(sdmiso,12,		MicroSD MISO)     \
	u16(fixms,1000,		Fix rate)	\
        b(gpsnavstar,Y,         GPS track NAVSTAR GPS)  \
        b(gpsglonass,Y,         GPS track GLONASS GPS)  \
        b(gpsgalileo,Y,         GPS track GALILEO GPS)  \
        b(gpswaas,Y,            GPS enable WAAS)        \
        b(gpssbas,Y,            GPS enable SBAS)        \
        b(gpsqzss,N,            GPS enable QZSS)        \
        b(gpsaic,Y,             GPS enable AIC) \
        b(gpseasy,Y,            GPS enable Easy)        \
	b(gpswalking,N,         GPS Walking mode)       \
        b(gpsflight,N,          GPS Flight mode)        \
        b(gpsballoon,N,         GPS Balloon mode)       \
	b(logpos,Y,		Log position data)	\
	b(loggpx,N,		Log in GPX format)	\
	bl(logcsv,Y,		Log in CSV summary)	\
	bl(loglla,Y,		Log lat/lon/alt)	\
	bl(logund,Y,		Log undulation)		\
	bl(logdop,Y,		Lod dop)		\
	bl(logodo,Y,		Log odometer)		\
	bl(logseq,Y,		Log seq)		\
	bl(logecef,Y,		Log ECEF data)		\
	bl(logepe,Y,		Log EPE data)		\
	bl(logsats,Y,		Log Sats data)		\
	bl(logcs,Y,		Log Course/speed)		\
	bl(logmph,Y,		Log mph)		\
	bl(logacc,Y,		Log Accelerometer)		\
	bl(logdsq,N,		Log pack deviation)	\
	u16(packmin,60,		Min samples for pack)	\
	u16(packmax,600,	Max samples for pack)	\
	u16(packdist,0,	 	Pack delta m)	\
	u16(packtime,0,		Pack delta s)	\
	s(url,,			URL to post data)	\
	s(emailhost,,		Email server)	\
	s(emailport,587,	Email portname)	\
	s(emailuser,,		Email username)	\
	s(emailpass,,		Email password)	\
	s(emailfrom,,		Email from)	\

#define s32al(n,a,t)	int32_t n[a];
#define u32(n,d,t)	uint32_t n;
#define u16(n,d,t)	uint16_t n;
#define s8(n,d,t)	int8_t n;
#define u8(n,d,t)	uint8_t n;
#define u8f(n,d,t)	uint8_t n;
#define u8l(n,d,t)	uint8_t n;
#define u16l(n,d,t)	uint16_t n;
#define b(n,d,t) uint8_t n;
#define bl(n,d,t) uint8_t n;
#define bf(n,d,t) uint8_t n;
#define h(n,t) uint8_t *n;
#define s(n,d,t) char * n;
#define io(n,d,t)         uint8_t n;
settings
#undef io
#undef u16
#undef u32
#undef s32al
#undef s8
#undef u8
#undef u8f
#undef u8l
#undef u16l
#undef bl
#undef bf
#undef b
#undef h
#undef s
#define	ZDARATE	10
#define	GSARATE	10
#define	GSVRATE	10
#define VTGRATE 5
   uint32_t zdadue = 0;         // Uptime when due
uint32_t gsadue = 0;
uint32_t gsvdue = 0;
uint32_t vtgdue = 0;
uint32_t busy = 0;              // Uptime last busy
httpd_handle_t webserver = NULL;
const char sd_mount[] = "/sd";
const char postcodefile[] = "/sd/POSTCODE.DAT";
led_strip_handle_t strip = NULL;
SemaphoreHandle_t cmd_mutex = NULL;
SemaphoreHandle_t ack_semaphore = NULL;
uint16_t pmtk = 0;              // Waiting ack
SemaphoreHandle_t fix_mutex = NULL;
uint8_t gpserrorcount = 0;      // running count
uint8_t gpserrors = 0;          // last count
uint8_t vtgcount = 0;           // Count of stopped/moving
uint8_t upload = 0;             // File upload progress
char rgbsd = 'K';
const char *cardstatus = NULL;
int32_t pos[3] = { 0 };         // last x/y/z

float adc[3];                   // ADCs

uint64_t sdsize = 0,            // SD card data
   sdfree = 0;

struct
{
   uint8_t die:1;               // End tasks
   uint8_t gpsstarted:1;        // GPS started
   uint8_t gpsinit:1;           // Init GPS
   uint8_t doformat:1;          // Format SD
   uint8_t dodismount:1;        // Stop SD until card removed
   uint8_t vtglast:1;           // Was last VTG moving
   uint8_t moving:1;            // We seem to be moving
   uint8_t sdwaiting:1;         // SD has data
   uint8_t sdpresent:1;         // SD is present
   uint8_t sdempty:1;           // SD has no data
   uint8_t accok:1;             // ACC OK
   uint8_t sbas:1;              // Current fix is SBAS
   uint8_t home:1;              // At home
   uint8_t charging:1;          // Charging
   uint8_t usb:1;               // USB power
   uint8_t postcode:1;          // We have postcode
} volatile b = { 0 };

typedef struct slow_s slow_t;
struct slow_s
{                               // Slow updated data
   uint8_t gsv[SYSTEMS];        // Sats in view
   uint8_t gsa[SYSTEMS];        // Sats active
   uint8_t fixmode;             // Fix mode from slow update
   float course;
   float speed;
   float hdop;                  // Slow hdop
   float pdop;
   float vdop;
} status = { 0 };

typedef struct fix_s fix_t;
struct fix_s
{                               // each fix
   fix_t *next;                 // Next in queue
   uint32_t seq;                // Simple sequence number
   struct
   {                            // Earth centred Earth fixed, used for packing, etc, and time stamp (us)
      int64_t x,
        y,
        z,
        t;
   } ecef;
   slow_t slow;
   uint64_t odo;                // Odometer
   double lat,
     lon;
   float alt;
   float und;
   float hdop;
   float hepe;                  // Estimated position error
   float vepe;
   float dsq;                   // Square of deviation from line from packing
   struct acc
   {                            // Acc data
      float x,
        y,
        z;
   } acc;
   uint8_t quality;             // Fix quality (0=none, 1=GPS, 2=SBAS)
   uint8_t sats;                // Sats used for fix
   uint8_t home:1;              // This pos is at home
   uint8_t corner:1;            // Corner point for packing
   uint8_t deleted:1;           // Deleted by packing
   uint8_t sett:1;              // Fields set
   uint8_t setsat:1;
   uint8_t setecef:1;
   uint8_t setlla:1;
   uint8_t setepe:1;
   uint8_t setodo:1;
   uint8_t setacc:1;
};

typedef struct fixq_s fixq_t;
struct fixq_s
{                               // A queue of fixes
   fix_t *base;
   fix_t *last;
   uint32_t count;
};

fixq_t fixlog = { 0 };          // Queue to log
fixq_t fixpack = { 0 };         // Queue to pack
fixq_t fixsd = { 0 };           // Queue to record to SD
fixq_t fixfree = { 0 };         // Queue of free

void power_shutdown (void);

char *
getts (uint64_t when, char fn)
{
   struct tm t;
   time_t now = when / 1000000LL;
   gmtime_r (&now, &t);
   if (t.tm_year < 100)
      return NULL;
   uint32_t ms = when / 1000LL % 1000LL;
   char temp[50],
    *p = temp;
   p += sprintf (p, "%04d-%02d-%02dT%02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
   if (ms)
      p += sprintf (p, ".%03ld", ms);
   *p++ = 'Z';
   *p = 0;
   if (fn)
   {
      for (char *p = temp; *p; p++)
         if (*p == ':' || *p == '.')
            *p = fn;
   }
   return strdup (temp);
}

#define	POSTCODEDATMAGIX	20240122
struct
{
   int32_t magic;
   int32_t scale;
   int32_t grid;
   int32_t e;
   int32_t n;
   int32_t w;
   int32_t h;
} postcodedat = { 0 };

void
checkpostcode (void)
{
   b.postcode = 0;
   int i = open (postcodefile, O_RDONLY);
   if (i >= 0)
   {
      if (read (i, &postcodedat, sizeof (postcodedat)) == sizeof (postcodedat) && postcodedat.magic == POSTCODEDATMAGIX)
      {
         ESP_LOGI (TAG, "Postcode file present");
         b.postcode = 1;
      }
      close (i);
   }
}

#define pi      3.1415926535897932384626433832795028L
#define secl(x) (1.0L/cosl(x))

#ifdef	POSTCODEDEBUG
#define	pe(x) e=x
#else
#define	pe(x) e=1
#endif
char *
getpostcode (double lat, double lon)
{
#ifdef	POSTCODEDEBUG
   const char *e = NULL;
#else
   uint8_t e = 0;
#endif
   size_t grid = sizeof (postcodedat);
   size_t data = grid + 4 * (postcodedat.w * postcodedat.h + 1);
   char postcode[8] = { 0 };
   uint32_t pos[2] = { 0 };
   int E = 0,
      N = 0;
   if (!b.postcode)
      pe ("No postcode file");
   if (!e)
   {
      E = lon * postcodedat.scale;
      N = lat * postcodedat.scale;
      int adjust = round (lat);
      if (adjust < 0)
         adjust = -adjust;
      if (adjust > 90)
         adjust = 90;           // Uh?
      static const uint16_t cossq[] =   // Square of cosine scaled by 1000
      { 1000, 999, 998, 997, 995, 992, 989, 985, 980, 975, 969, 963, 956, 949, 941, 933, 924, 914, 904, 894, 883, 871, 859, 847,
         834, 821, 807, 793, 779, 764, 749, 734, 719, 703, 687, 671, 654, 637, 620, 603, 586, 569, 552, 534, 517, 499, 482, 465,
         447, 430, 413, 396, 379, 362,
         345, 328, 312, 296, 280, 265, 250, 235, 220, 206, 192, 178, 165, 152, 140, 128, 116, 105, 95, 85, 75, 66, 58, 50, 43, 36,
         30, 24, 19, 14, 10, 7, 4,
         2, 1, 0, 0
      };
      adjust = cossq[adjust];

      // Check valid
      if (E < postcodedat.e || E >= postcodedat.e + postcodedat.w * postcodedat.grid || //
          N < postcodedat.n || N >= postcodedat.n + postcodedat.h * postcodedat.grid)
         pe ("Out of range");
      else
      {                         // Look up postcode
         grid += 4 * ((N - postcodedat.n) / postcodedat.grid * postcodedat.w + (E - postcodedat.e) / postcodedat.grid);
         struct
         {
            int32_t e,
              n;
            char postcode[8];
         } p;
         int i = open (postcodefile, O_RDONLY);
         if (i < 0)
            pe ("Cannot open postcode file");
         else
         {
            if (lseek (i, grid, SEEK_SET) < 0)
               pe ("Cannot seek grid");
            else if (read (i, pos, sizeof (pos)) != sizeof (pos))
               pe ("Cannot read pos");
            else
            {
               if (lseek (i, data + pos[0] * sizeof (p), SEEK_SET) < 0)
                  pe ("Cannot seek record");
               else
               {
                  long best = 0;
                  int c = pos[1] - pos[0];
                  if (c > 0)
                     while (c--)
                     {
                        if (read (i, &p, sizeof (p)) != sizeof (p))
                        {
                           pe ("End of file");
                           break;
                        }
                        long d = (long) (p.e - E) * (long) (p.e - E) * (long) adjust / 1000LL + (long) (p.n - N) * (long) (p.n - N);
                        if (!*postcode || d < best)
                        {
                           best = d;
                           memcpy (postcode, p.postcode, sizeof (postcode));
                        }
                     }
               }
            }
         }
         close (i);
      }
   }
#ifdef	POSTCODEDEBUG
   jo_t j = jo_object_alloc ();
   if (e)
      jo_string (j, "error", e);
   jo_litf (j, "lat", "%.9lf", lat);
   jo_litf (j, "lon", "%.9lf", lon);
   if (E)
      jo_int (j, "e", E);
   if (N)
      jo_int (j, "n", N);
   if (b.postcode)
   {
      jo_int (j, "scale", postcodedat.scale);
      jo_int (j, "grid", postcodedat.grid);
      jo_int (j, "basee", postcodedat.e);
      jo_int (j, "basen", postcodedat.n);
      jo_int (j, "width", postcodedat.w);
      jo_int (j, "heightr", postcodedat.h);
      jo_int (j, "grid", grid);
      jo_int (j, "data", data);
      jo_int (j, "pos0", pos[0]);
      jo_int (j, "pos1", pos[1]);
   }
   if (*postcode && !postcode[7])
      jo_string (j, "postcode", postcode);
   revk_error ("postcode", &j);
#endif
   if (!e && !postcode[7])
   {
      int l = strlen (postcode);
      if (l > 5)
      {
         char *temp = malloc (9);
         sprintf (temp, "%.*s %s", l - 3, postcode, postcode + l - 3);
         return temp;
      }
   }
   return NULL;
}

#undef pe

uint8_t
io (uint8_t gpio)
{                               // Get GPIO
   if (gpio && gpio_get_level (gpio & IO_MASK) == ((gpio & IO_INV) ? 0 : 1))
      return 1;
   return 0;
}

fix_t *
fixadd (fixq_t * q, fix_t * f)
{
   if (!f)
      return NULL;
   xSemaphoreTake (fix_mutex, portMAX_DELAY);
   f->next = NULL;              // Just in case
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
      f->next = NULL;           // Tidy
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
      f = mallocspi (sizeof (*f));
   if (f)
   {
      memset (f, 0, sizeof (*f));
      static uint32_t seq = 0;
      f->seq = ++seq;
      f->hepe = NAN;
      f->vepe = NAN;
      f->dsq = NAN;
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

uint32_t gpsbaudnow = 0;
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
   if (err)
      ESP_LOGE (TAG, "UART init error");
   sleep (1);
   uart_write_bytes (gpsuart, "\r\n\r\n$PMTK000*32\r\n", 4);
}

void
gps_cmd (const char *fmt, ...)
{                               // Send command to UART
   if (pmtk)
      xSemaphoreTake (ack_semaphore, 1000 * portTICK_PERIOD_MS);        // Wait for ACK from last command
   char s[100];
   va_list ap;
   va_start (ap, fmt);
   vsnprintf (s, sizeof (s) - 5, fmt, ap);
   va_end (ap);
   if (gpsdebug)
   {                            // Log (without checksum)
      jo_t j = jo_create_alloc ();
      jo_string (j, NULL, s);
      revk_info ("tx", &j);
   }
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
      xSemaphoreTake (ack_semaphore, 0);
      pmtk = atoi (s + 5);
   } else
      pmtk = 0;
}

const char *
app_callback (int client, const char *prefix, const char *target, const char *suffix, jo_t j)
{
   if (client || !prefix || target || strcmp (prefix, prefixcommand) || !suffix)
      return NULL;              // Not for us or not a command from main MQTT
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
   if (!strcmp (suffix, "stop"))
   {
      b.moving = 0;
      return "";
   }
   if (!strcmp (suffix, "move"))
   {
      b.moving = 1;
      return "";
   }
   if (!strcmp (suffix, "upgrade"))
   {
      busy = uptime ();
      return "";
   }
   if (!strcmp (suffix, "shutdown"))
   {
      power_shutdown ();
      return "";
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
      if (zdadue)
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
      b.gpsstarted = 0;
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

esp_err_t
acc_write (uint8_t reg, uint8_t val)
{
   esp_err_t e;
   i2c_cmd_handle_t txn = i2c_cmd_link_create ();
   i2c_master_start (txn);
   i2c_master_write_byte (txn, (accaddress << 1) | I2C_MASTER_WRITE, true);
   i2c_master_write_byte (txn, reg, true);
   i2c_master_write_byte (txn, val, true);
   i2c_master_stop (txn);
   if ((e = i2c_master_cmd_begin (I2CPORT, txn, 10 / portTICK_PERIOD_MS)))
      ESP_LOGE (TAG, "Write fail %02X %02X %d", reg, val, e);
   i2c_cmd_link_delete (txn);
   return e;
}

esp_err_t
acc_read (uint8_t reg, uint8_t len, void *data)
{
   esp_err_t e;
   i2c_cmd_handle_t txn = i2c_cmd_link_create ();
   i2c_master_start (txn);
   i2c_master_write_byte (txn, (accaddress << 1) | I2C_MASTER_WRITE, true);
   i2c_master_write_byte (txn, reg, true);
   i2c_master_start (txn);
   i2c_master_write_byte (txn, (accaddress << 1) | I2C_MASTER_READ, true);
   i2c_master_read (txn, data, len, I2C_MASTER_LAST_NACK);
   i2c_master_stop (txn);
   if ((e = i2c_master_cmd_begin (I2CPORT, txn, 10 / portTICK_PERIOD_MS)))
      ESP_LOGE (TAG, "Read fail %d", e);
   i2c_cmd_link_delete (txn);
   return e;
}

void
acc_init (void)
{
   if (!accsda || !accscl || !accaddress)
   {
      ESP_LOGE (TAG, "No ACC");
      return;
   }
   i2c_config_t config = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = accsda & IO_MASK,
      .scl_io_num = accscl & IO_MASK,
      .sda_pullup_en = true,
      .scl_pullup_en = true,
      .master.clk_speed = 100000,
   };
   esp_err_t e = i2c_driver_install (I2CPORT, I2C_MODE_MASTER, 0, 0, 0);
   if (!e)
      e = i2c_param_config (I2CPORT, &config);
   jo_t makeerr (const char *e)
   {
      ESP_LOGE (TAG, "ACC fail %s", e);
      jo_t j = jo_object_alloc ();
      jo_string (j, "error", e);
      jo_int (j, "sda", accsda & IO_MASK);
      jo_int (j, "scl", accscl & IO_MASK);
      jo_int (j, "address", accaddress);
      return j;
   }
   if (e)
   {
      jo_t j = makeerr ("I2C failed");
      jo_int (j, "code", e);
      revk_error ("ACC", &j);
      vTaskDelete (NULL);
      return;
   }
   uint8_t id = 0;
   acc_read (0x0F, 1, &id);
   if (id != 0x33)
   {
      jo_t j = makeerr ("Wrong ID");
      jo_int (j, "id", id);
      revk_error ("ACC", &j);
      vTaskDelete (NULL);
      return;
   }
   acc_write (0x1F, 0xC0);      // Temp and ADC
   acc_write (0x20, 0x17);      // 1Hz high resolution
   acc_write (0x23, 0xB8);      // High resolution ±16g
   acc_write (0x2E, 0x00);      // Bypass
   acc_write (0x24, 0x00);      // FIFO disable
   b.accok = 1;
}

void
acc_stop (void)
{
   acc_write (0x20, 0x00);      // Power-down mode
}

void
acc_get (fix_t * f)
{                               // Get acc data for a fix
   if (!f || !b.accok)
      return;
   uint8_t data[7] = { 0 };
   acc_read (0x80 + 0x27, sizeof (data), data);
   if (data[0] & 0x08)
   {
      f->acc.x = ((float) ((int16_t) (data[1] + (data[2] << 8)))) / 16.0 * 12.0 / 1000.0;       // 12 bits and 12mG/unit
      f->acc.y = ((float) ((int16_t) (data[3] + (data[4] << 8)))) / 16.0 * 12.0 / 1000.0;       // 12 bits and 12mG/unit
      f->acc.z = ((float) ((int16_t) (data[5] + (data[6] << 8)))) / 16.0 * 12.0 / 1000.0;       // 12 bits and 12mG/unit
      f->setacc = 1;
   }
   acc_read (0x80 + 0x08, 6, data);
   adc[0] = (1.2 + 0.4 * (float) ((int16_t) (data[0] + (data[1] << 8)) >> 6) / 1024) * BATSCALE;
   adc[1] = (1.2 + 0.4 * (float) ((int16_t) (data[2] + (data[3] << 8)) >> 6) / 1024);   // Spare
   adc[2] = (1.2 + 0.4 * (float) ((int16_t) (data[4] + (data[5] << 8)) >> 6) / 1024);   // Temp
}

void
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
   gps_cmd ("$PMTK101");        // Hot start
   gps_cmd ("$PMTK286,%d", gpsaic ? 1 : 0);     // AIC
   gps_cmd ("$PMTK353,%d,%d,%d,0,0", gpsnavstar, gpsglonass, gpsgalileo);
   gps_cmd ("$PMTK352,%d", gpsqzss ? 0 : 1);    // QZSS (yes, 1 is disable)
   gps_cmd ("$PQTXT,W,0,1");    // Disable TXT
   gps_cmd ("$PQECEF,W,1,1");   // Enable ECEF
   gps_cmd ("$PQEPE,W,1,1");    // Enable EPE
   gps_cmd ("$PMTK886,%d", gpsballoon ? 3 : gpsflight ? 2 : gpswalking ? 1 : 0);        // FR mode
   // Queries - responses prompt settings changes if needed
   gps_cmd ("$PMTK414");        // Q_NMEA_OUTPUT
   gps_cmd ("$PMTK400");        // Q_FIX
   gps_cmd ("$PMTK401");        // Q_DGPS
   gps_cmd ("$PMTK413");        // Q_SBAS
   gps_cmd ("$PMTK869,0");      // Query EASY
   gps_cmd ("$PQODO,R");        // Read ODO status
   //gps_cmd ("$PMTK605");     // Q_RELEASE
   b.gpsstarted = 1;
}

void
gps_stop (void)
{
   gps_cmd ("$PMTK161,0");      // Standby
}

void
nmea_timeout (uint32_t up)
{
   if (zdadue && zdadue < up)
   {
      zdadue = 0;
      esp_sntp_restart ();
   }
   if (gsadue && gsadue < up)
   {
      gsadue = 0;
      status.fixmode = 0;
      memset (status.gsa, 0, sizeof (status.gsa));
      status.hdop = NAN;
      status.pdop = NAN;
      status.vdop = NAN;
   }
   if (gsvdue && gsvdue < up)
   {
      gsvdue = 0;
      memset (status.gsv, 0, sizeof (status.gsv));
   }
   if (vtgdue && vtgdue < up)
   {
      vtgdue = 0;
      status.course = NAN;
      status.speed = NAN;
   }
}

void
nmea (char *s)
{
   if (gpsdebug)
   {
      jo_t j = jo_create_alloc ();
      jo_string (j, NULL, s);
      revk_info ("rx", &j);
   }
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
      while (*p && isdigit ((int) *p))
         v = v * 10 + *p++ - '0';
      if (*p == '.')
      {
         p++;
         while (places && *p && isdigit ((int) *p))
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
   uint32_t up = uptime ();
   nmea_timeout (up);
   static uint8_t century = 19;
   static fix_t *fix = NULL;
   static uint32_t fixtod = -1;
   static uint32_t sod = 0;
   void startfix (const char *tod)
   {
      uint32_t newtod = (tod ? parse (tod, 3) : -1);
      if (fix && fixtod == newtod)
         return;                // Same fix
      if (sod && fixtod > newtod)
         sod++;
      fixtod = newtod;
      if (fix)
      {
         fix->slow = status;
         fix = fixadd (&fixlog, fix);
      }
      if (tod)
         fix = fixnew ();
      if (sod && tod && fix)
      {
         fix->ecef.t =
            1000000LL * (sod * 86400 + (fixtod / 10000000LL) * 3600 + (fixtod / 100000LL % 100LL) * 60 +
                         (fixtod / 1000LL % 100LL)) + (fixtod % 1000LL) * 1000LL;
         fix->sett = 1;
      }
      if (fix)
         acc_get (fix);
   }
   if (!b.gpsstarted && *f[0] == 'G' && !strcmp (f[0] + 2, "GGA") && (esp_timer_get_time () > 10000000 || !revk_link_down ()))
      b.gpsinit = 1;            // Time to send init
   if (!strcmp (f[0], "PMTK001") && n >= 3)
   {                            // ACK
      int tag = atoi (f[1]);
      if (pmtk && pmtk == tag)
      {                         // ACK received
         xSemaphoreGive (ack_semaphore);
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
      return;                   // ignore
   if (!strcmp (f[0], "PMTK010"))
      return;                   // Started, happens at end of init anyway
   if (!strcmp (f[0], "PMTK011"))
      return;                   // Ignore
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
         pos[0] = (fix->ecef.x = parse (f[2], 6)) / 1000000LL;
         pos[1] = (fix->ecef.y = parse (f[3], 6)) / 1000000LL;
         pos[2] = (fix->ecef.z = parse (f[4], 6)) / 1000000LL;
         fix->setecef = 1;
         if (home[0] || home[1] || home[2])
         {
            int64_t dx = pos[0] - home[0];
            int64_t dy = pos[1] - home[1];
            int64_t dz = pos[2] - home[2];
            fix->home = b.home = ((dx * dx + dy * dy + dz * dz < (int64_t) homem * (int64_t) homem) ? 1 : 0);
         }
      }
      if (logodo)
         gps_cmd ("$PQODO,Q");  // Read ODO every sample
      return;
   }
   if (!strcmp (f[0], "PMTK869") && n >= 4)
   {                            // Set EASY
      if (atoi (f[1]) == 2 && atoi (f[2]) != gpseasy)
         gps_cmd ("$PMTK869,1,%d", gpseasy ? 1 : 0);
      return;
   }
   if (!strcmp (f[0], "PMTK513") && n >= 2)
   {                            // Set SBAS
      if (atoi (f[1]) != gpssbas)
         gps_cmd ("$PMTK313,%d", gpssbas ? 1 : 0);
      return;
   }
   if (!strcmp (f[0], "PMTK501") && n >= 2)
   {                            // Set DGPS
      if (atoi (f[1]) != ((gpssbas || gpswaas) ? 2 : 0))
         gps_cmd ("$PMTK301,%d", (gpssbas || gpswaas) ? 2 : 0);
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
   {
      unsigned int rates[19] = { 0 };
      //rates[0]=0;     // GLL
      //rates[1]=0;     // RMC
      rates[2] = (VTGRATE * 1000 / fixms ? : 1);        // VTG
      rates[3] = 1;             // GGA every sample
      rates[4] = (GSARATE * 1000 / fixms ? : 1);        // GSA
      rates[5] = (GSVRATE * 1000 / fixms ? : 1);        // GSV
      //rates[6]=0; // GRS
      //rates[7]=0; // GST
      //rates[13]=0; // MALM
      //rates[14]=0; // MEPH
      //rates[15]=0; // MDGP
      //rates[16]=0; // MDBG
      rates[17] = (ZDARATE * 1000 / fixms ? : 1);       // ZDA
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
         fix->quality = atoi (f[6]);    // Fast fix mode
         b.sbas = ((fix->quality == 2) ? 1 : 0);
         fix->sats = atoi (f[7]);
         if (*f[8])
            fix->hdop = strtof (f[8], NULL);
         if (*f[9])
            fix->alt = strtof (f[9], NULL);
         if (*f[11])
            fix->und = strtof (f[11], NULL);
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
      if (strlen (f[1]) == 10)
      {
         zdadue = up + ZDARATE + 2;
         century = atoi (f[4]) / 100;
         if (century >= 20)
         {
            struct timeval v = { 0 };
            struct tm t = { 0 };
            t.tm_year = atoi (f[4]) - 1900;
            t.tm_mon = atoi (f[3]) - 1;
            t.tm_mday = atoi (f[2]);
            sod = timegm (&t) / 86400;
            t.tm_hour = (f[1][0] - '0') * 10 + f[1][1] - '0';
            t.tm_min = (f[1][2] - '0') * 10 + f[1][3] - '0';
            t.tm_sec = (f[1][4] - '0') * 10 + f[1][5] - '0';
            v.tv_usec = atoi (f[1] + 7) * 1000;
            v.tv_sec = timegm (&t);
            if (!zdadue)
               esp_sntp_stop ();
            settimeofday (&v, NULL);
         }
      }
      if (!logodo)
         gps_cmd ("$PQODO,Q");  // Read ODO periodically
      return;
   }
   if (*f[0] == 'G' && !strcmp (f[0] + 2, "VTG") && n >= 10)
   {
      vtgdue = up + VTGRATE + 2;
      status.course = (*f[1] ? strtof (f[1], NULL) : NAN);
      status.speed = (*f[7] ? strtof (f[7], NULL) : NAN);
      // Start/stop
      if (b.vtglast == (status.fixmode <= 1 || status.speed == 0 ? 0 : 1))
      {                         // No change
         if (vtgcount < 255)
            vtgcount++;
         if (b.vtglast && !b.moving && (vtgcount * VTGRATE >= move || (fix && status.speed > fix->hepe)))
         {                      // speed (kp/h) compared to EPE is just a rough idea that we are moving faster than random
            b.moving = 1;
            jo_t j = jo_object_alloc ();
            jo_string (j, "action", "Started moving");
            revk_info ("GPS", &j);
         } else if (!b.vtglast && b.moving && (vtgcount * VTGRATE >= stop || b.home || (powerstop && !b.usb)))
         {
            b.moving = 0;
            jo_t j = jo_object_alloc ();
            jo_string (j, "action", "Stopped moving");
            revk_info ("GPS", &j);
         }
      } else
      {
         // Changed
         b.vtglast ^= 1;
         vtgcount = 1;
      }
      return;
   }
   if (*f[0] == 'G' && !strcmp (f[0] + 2, "GSA") && n >= 18)
   {                            // $GNGSA,A,3,18,05,15,23,20,,,,,,,,1.33,1.07,0.80,1
      gsadue = up + GSARATE + 2;
      status.fixmode = atoi (f[2]);     // Slow
      uint8_t s = atoi (f[18]);
      if (s && s <= SYSTEMS)
      {
         uint8_t c = 0;
         for (int i = 3; i <= 14; i++)
            if (atoi (f[i]))
               c++;
         status.gsa[s - 1] = c;
      }
      if (*f[15])
         status.pdop = strtof (f[15], NULL);
      if (*f[16])
         status.hdop = strtof (f[16], NULL);
      if (*f[17])
         status.vdop = strtof (f[17], NULL);
      return;
   }
   if (*f[0] == 'G' && !strcmp (f[0] + 2, "GSV") && n >= 4)
   {
      gsvdue = up + GSVRATE + 2;
      int n = atoi (f[3]);
      for (int s = 0; s < SYSTEMS; s++)
         if (f[0][1] == system_code[s])
            status.gsv[s] = n;
      return;
   }
   if (!strcmp (f[0], "PQODO") && n >= 2)
   {
      if ((*f[1] == 'R' && !atoi (f[2])) || (*f[2] == 'Q' && atoi (f[2]) < ODOBASE))
         gps_cmd ("$PQODO,W,1,%d", ODOBASE);    // Start ODO
      else if (*f[1] == 'Q' && fix)
      {
         fix->odo = parse (f[2], 2);    // Read ODO
         fix->setodo = 1;
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
   while (!b.die)
   {
      // Get line(s), the timeout should mean we see one or more whole lines typically
      int l = uart_read_bytes (gpsuart, p, buf + sizeof (buf) - p, 10 / portTICK_PERIOD_MS);
      if (l <= 0)
      {
         if (timeout && timeout < esp_timer_get_time ())
         {
            b.gpsstarted = 0;
            static int rate = 0;
            const uint32_t rates[] = { 4800, 9600, 14400, 19200, 38400, 57600, 115200 };
            jo_t j = jo_object_alloc ();
            jo_string (j, "error", "No reply");
            jo_int (j, "Baud", rates[rate]);
            jo_int (j, "tx", gpstx & IO_MASK);
            jo_int (j, "rx", gpsrx & IO_MASK);
            revk_error ("GPS", &j);
            if (++rate >= sizeof (rates) / sizeof (*rates))
               rate = 0;
            gps_connect (rates[rate]);
            timeout = esp_timer_get_time () + 2000000 + fixms;
            nmea_timeout (uptime ());
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
   gps_stop ();
   acc_stop ();
   vTaskDelete (NULL);
}

jo_t
log_line (fix_t * f)
{                               // generate log line
   jo_t j = jo_object_alloc ();
   if (f->sett)
   {
      char *ts = getts (f->ecef.t, 0);
      if (ts)
         jo_stringf (j, "ts", ts);
      free (ts);
   }
   if (logseq)
      jo_int (j, "seq", f->seq);
   if (logsats && f->sats + f->slow.gsa[0] + f->slow.gsa[1] + f->slow.gsa[2])
   {
      jo_object (j, "sats");
      for (int s = 0; s < SYSTEMS; s++)
         if (f->slow.gsa[s])
            jo_int (j, system_name[s], f->slow.gsa[s]);
      if (f->sats)
         jo_int (j, "used", f->sats);
      jo_close (j);
   }
   if (f->slow.fixmode)
      jo_int (j, "fixmode", f->slow.fixmode);
   if (loglla && f->setlla && f->quality)
   {
      jo_litf (j, "lat", "%.8lf", f->lat);
      jo_litf (j, "lon", "%.8lf", f->lon);
      if (f->slow.fixmode >= 3 && !isnan (f->alt))
         jo_litf (j, "alt", "%.2f", f->alt);
      jo_int (j, "quality", f->quality);
   }
   if (logund && f->slow.fixmode >= 3 && !isnan (f->und))
      jo_litf (j, "und", "%.2f", f->und);
   if (logepe && f->setepe && f->slow.fixmode >= 1)
   {
      if (f->hepe > 0)
         jo_litf (j, "hepe", "%.2f", f->hepe);
      if (f->vepe > 0 && f->slow.fixmode >= 3)
         jo_litf (j, "vepe", "%.2f", f->vepe);
   }
   if (logdop)
   {
      if (!isnan (f->hdop) && f->hdop)
         jo_litf (j, "hdop", "%.1f", f->hdop);
      if (!isnan (f->slow.pdop) && f->slow.pdop)
         jo_litf (j, "pdop", "%.1f", f->slow.pdop);
      if (!isnan (f->slow.vdop) && f->slow.vdop && f->slow.fixmode >= 3)
         jo_litf (j, "vdop", "%.1f", f->slow.vdop);
   }
   if (logcs && f->quality && !isnan (f->slow.speed) && f->slow.speed != 0)
   {
      jo_litf (j, "speed", "%.2f", f->slow.speed);
      if (!isnan (f->slow.course))
         jo_litf (j, "course", "%.2f", f->slow.course);
   }
   if (logmph && f->quality && !isnan (f->slow.speed) && f->slow.speed != 0)
      jo_litf (j, "mph", "%.2f", f->slow.speed / 1.609344);
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
         uint32_t us = v % 1000000LL;
         if (us)
            jo_litf (j, t, "%s%lld.%06lld", s, v / 1000000LL, v % 1000000LL);
         else
            jo_litf (j, t, "%s%lld", s, v / 1000000LL);
      }
      jo_object (j, "ecef");
      o ("x", f->ecef.x);
      o ("y", f->ecef.y);
      o ("z", f->ecef.z);
      if (f->sett)
         o ("t", f->ecef.t);
      jo_close (j);
   }
   if (f->setacc && logacc)
   {
      jo_object (j, "acc");
      jo_litf (j, "x", "%.3f", f->acc.x);
      jo_litf (j, "y", "%.3f", f->acc.y);
      jo_litf (j, "z", "%.3f", f->acc.z);
      jo_close (j);
   }
   if (logdsq && !isnan (f->dsq))
      jo_litf (j, "dsq", "%f", f->dsq);
   if (logodo && f->setodo && f->odo)
      jo_litf (j, "odo", "%lld.%02lld", f->odo / 100, f->odo % 100);
   if (gpserrors)
   {
      jo_int (j, "errors", gpserrors);
      gpserrors = 0;
   }
   if (f->home && f->setecef)
      jo_bool (j, "home", 1);
   return j;
}

void
log_task (void *z)
{                               // Log via MQTT
   while (!b.die)
   {
      if (!fixlog.count || (!b.moving && fixlog.base && fixlog.base->quality && fixlog.count < move))
      {                         // Waiting - holds pre-moving data
         usleep (100000);
         continue;
      }
      fix_t *f = fixget (&fixlog);
      if (!f)
         continue;
      jo_t j = log_line (f);
      revk_info ("GPS", &j);
      // Pass on - if packing, and TS and ECEF, to packing, else if packing and no TS or ECEF then drop as packing does not do those. Else direct to SD
      fixadd (packdist && packmin ? !f->sett || !f->setecef ? &fixfree : &fixpack : &fixsd, f);
   }
   vTaskDelete (NULL);
}

float
dist2 (fix_t * A, fix_t * B)
{                               // Distance between two fixes
   float X = ((float) (A->ecef.x - B->ecef.x)) / 1000000.0;
   float Y = ((float) (A->ecef.y - B->ecef.y)) / 1000000.0;
   float Z = ((float) (A->ecef.z - B->ecef.z)) / 1000000.0;
   float T = 0;
   if (packtime)
      T = ((float) ((A->ecef.t - B->ecef.t) * packdist / packtime)) / 1000000.0;
   return X * X + Y * Y + Z * Z + T * T;
}

fix_t *
findmax (fix_t * A, fix_t * B, float *dsqp)
{
   if (dsqp)
      *dsqp = 0;
   if (!A || !B || A == B || A->next == B)
      return NULL;
   float b2 = dist2 (B, A);
   fix_t *m = NULL;
   float best = 0;
   for (fix_t * C = A->next; C && C != B; C = C->next)
   {
      float h2 = 0;
      float a2 = dist2 (A, C),
         c2 = 0;
      if (b2 == 0.0)
         h2 = a2;               // A/B same, so distance from A
      else
      {
         c2 = dist2 (C, B);
         if (c2 - b2 >= a2)
            h2 = a2;            // Off end of A
         else if (a2 - b2 >= c2)
            h2 = c2;            // Off end of B
         else
            h2 = (4 * a2 * b2 - (a2 + b2 - c2) * (a2 + b2 - c2)) / (b2 * 4);    // see https://www.revk.uk/2024/01/distance-of-point-to-lie-in-four.html
      }
      C->dsq = h2;              // Before EPE adjust
      if (m && h2 <= best)
         continue;              // Not bigger
      best = h2;
      m = C;
   }
   if (dsqp)
      *dsqp = best;
   return m;
}

void
pack_task (void *z)
{                               // Packing - only gets data with ECEF and time set
   uint32_t packtry = packmin;
   while (!b.die)
   {
      if (fixpack.count < 2 || (b.moving && fixpack.count < packtry))
      {                         // Wait
         usleep (100000);
         continue;
      }
      fix_t *A = fixpack.base;
      fix_t *B = fixpack.last;
      float dsq = 0;
      float cutoff = (float) packdist * (float) packdist;
      fix_t *M = findmax (A, B, &dsq);
      fix_t *E = M ? : B;
      if (dsq < cutoff && b.moving && fixpack.count < packmax)
      {                         // wait for more
         packtry += packmin;
         continue;
      }
      A->corner = 1;
      packtry = packmin;
      if (dsq < cutoff)
         for (fix_t * X = A->next; X && X != B; X = X->next)
            X->deleted = 1;     // All within cutoff
      else
         while (A && M)
         {                      // Check A to M - recursively
            M->corner = 1;
            B = M;
            M = findmax (A, B, &dsq);
            if (dsq < cutoff)
            {                   // Drop all in between as all within margin - otherwise process A to M again.
               if (A != B)
                  for (fix_t * X = A->next; X && X != B; X = X->next)
                     X->deleted = 1;
               // Find next half
               A = B;
               M = A->next;
               while (M && !M->corner)
                  M = M->next;
            }
         }
      while (fixpack.base && fixpack.base != E)
      {
         fix_t *X = fixget (&fixpack);
         fixadd (X->deleted ? &fixfree : &fixsd, X);
      }
   }
   vTaskDelete (NULL);
}

void
checkupload (void)
{
   if ((!home[0] && !home[1] && !home[2]) || b.home)
      revk_enable_wifi ();
   uint32_t up = uptime ();
   static uint32_t delay = 0;
   if (delay > up)
      return;
   if (b.sdempty)
   {
      ESP_LOGI (TAG, "No upload as empty");
      delay = up + 60;
      return;
   }
   if (b.sdwaiting && revk_link_down ())
   {
      ESP_LOGI (TAG, "No upload as off line");
      delay = up + 5;
      return;
   }
   if (!(b.sdpresent = io (sdcd)))
   {
      ESP_LOGI (TAG, "No upload as no card");
      delay = up + 10;
      return;
   }
   while (1)
   {
      DIR *dir = opendir (sd_mount);
      if (!dir)
      {                         // Error
         delay = up + 60;       // Don't try for a bit
         jo_t j = jo_object_alloc ();
         jo_stringf (j, "error", "Cannot open %s", sd_mount);
         revk_error ("SD", &j);
         ESP_LOGE (TAG, "Cannot open dir");
         return;
      }
      char zap = 0;
      char *filename = NULL;
      const char *ct = NULL;
      struct dirent *entry;
      while (!filename && (entry = readdir (dir)))
         if (entry->d_type == DT_REG)
         {
            const char *e = strrchr (entry->d_name, '.');
            if (!e)
               continue;
            if (!strcasecmp (e, ".json"))
               ct = "application/json";
            else if (!strcasecmp (e, ".gpx"))
               ct = "application/gpx+xml";
            else if (!strcasecmp (e, ".csv"))
               ct = "text/csv";
            if (ct)
               asprintf (&filename, "%s/%s", sd_mount, entry->d_name);
         }
      closedir (dir);
      if (filename)
      {
         ESP_LOGI (TAG, "Waiting %s", filename);
         b.sdwaiting = 1;
         b.sdempty = 0;
      } else
      {
         b.sdwaiting = 0;
         b.sdempty = 1;
         ESP_LOGI (TAG, "No upload as empty");
         delay = up + 60;
         return;
      }
      if (revk_link_down ())
      {                         // Don't send or nothing to send
         ESP_LOGI (TAG, "Not sending as off line (%s)", filename);
         free (filename);
         break;
      }
      if (revk_link_down () || !*url)
      {                         // Don't send or nothing to send
         ESP_LOGI (TAG, "Not sending as no URL (%s)", filename);
         delay = up + 3600;
         free (filename);
         break;
      }
      busy = up;
      struct stat s = { 0 };
      jo_t makeerr (const char *e)
      {
         jo_t j = jo_object_alloc ();
         if (e)
         {
            jo_string (j, "error", e);
            if (filename)
               ESP_LOGE (TAG, "%s: %s", e, filename);
         }
         if (filename)
            jo_string (j, "filename", filename);
         jo_string (j, "url", url);
         if (s.st_size)
            jo_int (j, "size", s.st_size);
         return j;
      }
      if (filename && *filename && !stat (filename, &s))
      {                         // Send
         rgbsd = 'C';
         if (!s.st_size)
         {
            ESP_LOGI (TAG, "Empty file %s", filename);
            zap = 1;            // Empty
            jo_t j = makeerr ("Empty file");
            revk_error ("Upload", &j);
         } else
         {
            ESP_LOGI (TAG, "Send %s", filename);
            int response = 0;
            FILE *i = fopen (filename, "r");
            if (i)
            {
               if (strchr (url, '@'))
               {
                  ESP_LOGI (TAG, "Email %s", url);
                  response = email_send (url, ct, filename + sizeof (sd_mount), filename + sizeof (sd_mount), i, s.st_size);
               } else
               {
                  char *u;
                  asprintf (&u, "%s?%s-%s", url, hostname, filename + sizeof (sd_mount));
                  for (char *p = u + strlen (url) + 1; *p; p++)
                     if (!isalnum ((int) *p) && *p != '.')
                        *p = '-';
                  esp_http_client_config_t config = {
                     .url = u,
                     .crt_bundle_attach = esp_crt_bundle_attach,
                     .method = HTTP_METHOD_POST,
                  };
#define	BLOCK	2048
                  char *buf = mallocspi (BLOCK);
                  if (buf)
                  {
                     esp_http_client_handle_t client = esp_http_client_init (&config);
                     if (client)
                     {
                        esp_http_client_set_header (client, "Content-Type", ct);
                        ESP_LOGI (TAG, "Sending %s %ld", filename, s.st_size);
                        if (!esp_http_client_open (client, s.st_size))
                        {       // Send
                           int total = 0;
                           int len = 0;
                           while ((len = fread (buf, 1, BLOCK, i)) > 0)
                           {
                              total += len;
                              upload = total * 100 / s.st_size;
                              ESP_LOGI (TAG, "%d bytes (%d%%)", total, upload);
                              esp_http_client_write (client, buf, len);
                              busy = uptime ();
                           }
                           esp_http_client_fetch_headers (client);
                           esp_http_client_flush_response (client, &len);
                           response = esp_http_client_get_status_code (client);
                           esp_http_client_close (client);
                        }
                        esp_http_client_cleanup (client);
                     }
                  }
                  upload = 0;
                  free (u);
                  free (buf);
#undef	BLOCK
               }
               ESP_LOGI (TAG, "Sent, Response %d", response);
               if (response / 100 == 2)
               {
                  jo_t j = makeerr (NULL);
                  revk_info ("Uploaded", &j);
                  zap = 1;
               } else
               {
                  jo_t j = makeerr ("Failed to upload");
                  jo_int (j, "response", response);
                  revk_error ("Upload", &j);
               }
               fclose (i);
            } else
            {
               jo_t j = makeerr ("Failed to open");
               revk_error ("Upload", &j);
            }
         }
      }
      if (zap)
      {
         ESP_LOGI (TAG, "Delete %s", filename);
         if (unlink (filename))
         {
            jo_t j = makeerr ("Failed to delete");
            revk_error ("Upload", &j);
         }
      }
      free (filename);
      if (!zap)
      {                         // Don't retry for a bit
         delay = up + 60;
         ESP_LOGI (TAG, "No upload for 60 seconds");
         return;
      }
   }
}

void
sd_task (void *z)
{
   revk_disable_ap ();
   revk_disable_settings ();
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
      rgbsd = 'R';
      jo_t j = jo_object_alloc ();
      jo_string (j, "error", cardstatus = "SPI failed");
      jo_int (j, "code", ret);
      jo_int (j, "MOSI", sdmosi & IO_MASK);
      jo_int (j, "MISO", sdmiso & IO_MASK);
      jo_int (j, "CLK", sdsck & IO_MASK);
      revk_error ("SD", &j);
      vTaskDelete (NULL);
      return;
   }
   esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = 1,
      .max_files = 2,
      .allocation_unit_size = 16 * 1024,
      .disk_status_check_enable = 1,
   };
   sdmmc_card_t *card = NULL;
   sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT ();
   slot_config.gpio_cs = sdss & IO_MASK;
   slot_config.host_id = host.slot;
   uint64_t csvtime = 0;
   while (!b.die)
   {
      if (sdcd)
      {
         if (b.dodismount)
         {                      // Waiting card removed
            rgbsd = 'B';
            jo_t j = jo_object_alloc ();
            jo_string (j, "action", cardstatus = "Remove card");
            revk_info ("SD", &j);
            b.dodismount = 0;
            while ((b.sdpresent = io (sdcd)))
               wait (1);
            continue;
         }
         if (!(b.sdpresent = io (sdcd)))
         {                      // No card
            jo_t j = jo_object_alloc ();
            jo_string (j, "error", cardstatus = "Card not present");
            revk_info ("SD", &j);
            rgbsd = 'M';
            revk_enable_wifi ();
            revk_enable_ap ();
            revk_enable_settings ();
            while (!(b.sdpresent = io (sdcd)))
               wait (1);        // Waiting for card inserted
         }
         if (!gpsdebug && (pos[0] || pos[1] || pos[2]) && (home[0] || home[1] || home[2]) && !b.home)
            revk_disable_wifi ();
         revk_disable_ap ();
         revk_disable_settings ();
         b.sdpresent = 1;
      } else if (b.dodismount)
      {
         b.dodismount = 0;
         wait (60);
         continue;
      }
      wait (1);
      ESP_LOGI (TAG, "Mounting filesystem");
      ret = esp_vfs_fat_sdspi_mount (sd_mount, &host, &slot_config, &mount_config, &card);
      if (ret != ESP_OK)
      {
         jo_t j = jo_object_alloc ();
         if (ret == ESP_FAIL)
            jo_string (j, "error", cardstatus = "Failed to mount");
         else
            jo_string (j, "error", cardstatus = "Failed to iniitialise");
         jo_int (j, "code", ret);
         revk_error ("SD", &j);
         rgbsd = 'R';
         sleep (1);
         continue;
      }
      ESP_LOGI (TAG, "Filesystem mounted");
      b.sdpresent = 1;          // we mounted, so must be
      rgbsd = 'G';              // Writing to card
      if (b.doformat && (ret = esp_vfs_fat_spiflash_format_rw_wl (sd_mount, "GPS")))
      {
         jo_t j = jo_object_alloc ();
         jo_string (j, "error", cardstatus = "Failed to format");
         jo_int (j, "code", ret);
         revk_error ("SD", &j);
      }
      rgbsd = 'R';              // Oddly this call can hang forever!
      {
         esp_vfs_fat_info (sd_mount, &sdsize, &sdfree);
         jo_t j = jo_object_alloc ();
         jo_string (j, "action", cardstatus = (b.doformat ? "Formatted" : "Mounted"));
         jo_int (j, "size", sdsize);
         jo_int (j, "free", sdfree);
         revk_info ("SD", &j);
      }
      rgbsd = 'Y';              // Mounted, ready
      b.doformat = 0;
      checkpostcode ();
      checkupload ();
      while (!b.doformat && !b.dodismount && !b.die)
      {
         FILE *o = NULL;
         int line = 0;
         char filename[100];
         uint64_t odo0 = 0,
            odo1 = 0;
         uint64_t starttime = 0;
         uint64_t endtime;
         uint8_t starthome = 0;
         uint8_t endhome = 0;
         double startlat = NAN,
            startlon = NAN;
         double endlat = NAN,
            endlon = NAN;
         while (!b.doformat && !b.dodismount)
         {
            rgbsd = (o ? 'G' : 'Y');
            if (!(b.sdpresent = io (sdcd)))
            {                   // card removed
               b.dodismount = 1;
               break;
            }
            fix_t *f = fixget (&fixsd);
            if (!f)
            {                   // End of queue
               if (o && !b.moving && fixpack.count < 2)
                  break;        // Stopped moving, close file, upload
               if (!o)
                  checkupload ();
               usleep (100000);
               continue;
            }
            if (!o && f->sett && f->setecef && f->setlla && f->quality && b.moving)
            {                   // Open file
               char *ts = getts (f->ecef.t, '-');
               if (ts)
               {
                  sprintf (filename, "%s/%s.%s", sd_mount, ts, loggpx ? "gpx" : "json");
                  free (ts);
                  o = fopen (filename, "w");
                  if (!o)
                  {             // Open failed
                     ESP_LOGE (TAG, "Failed open file %s", filename);
                     jo_t j = jo_object_alloc ();
                     jo_string (j, "error", cardstatus = "Failed to create file");
                     jo_string (j, "filename", filename);
                     revk_error ("SD", &j);
                  } else
                  {             // Open worked
                     starttime = f->ecef.t;
                     startlat = f->lat;
                     startlon = f->lon;
                     starthome = (f->home | b.home);
                     if (b.sdempty)
                        csvtime = 0;
                     b.sdempty = 0;
                     b.sdwaiting = 1;
                     if (!gpsdebug)
                        revk_disable_wifi ();
                     ESP_LOGI (TAG, "Open file %s", filename);
                     jo_t j = jo_object_alloc ();
                     jo_string (j, "action", cardstatus = "Log file created");
                     jo_string (j, "filename", filename);
                     revk_info ("SD", &j);
                     if (loggpx)
                     {
                        fprintf (o, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"    //
                                 "<gpx version=\"1.0\">\r\n"    //
                                 "<metadata><name>%s</name></metadata>\r\n"     //
                                 "<trk><trkseg>\r\n", revk_id);
                     } else
                     {
                        jo_t j = jo_object_alloc ();
                        jo_string (j, "id", revk_id);
                        jo_string (j, "name", hostname);
                        jo_string (j, "version", revk_version);
                        if (logpos)
                           jo_array (j, "gps");
                        char *json = jo_finisha (&j);
                        int len = strlen (json);
                        json[--len] = 0;
                        if (logpos)
                           json[--len] = 0;

                        fprintf (o, "%s\r\n", json);
                        free (json);
                     }
                  }
                  line = 0;
               }
            }
            if (o && logpos)
            {
               if (loggpx)
               {
                  if (f->setlla)
                  {
                     fprintf (o, "<trkpt lat=\"%.8lf\" lon=\"%.8lf\">", f->lat, f->lon);
                     if (!isnan (f->alt))
                        fprintf (o, "<ele>%.2f</ele>", f->alt);
                     if (f->sett)
                     {
                        char *ts = getts (f->ecef.t, 0);
                        if (ts)
                           fprintf (o, "<time>%s</time>", ts);
                        free (ts);
                     }
                     if (f->slow.fixmode >= 1)
                        fprintf (o, "<fix>%s</fix>", f->slow.fixmode == 1 ? "none" : f->slow.fixmode == 2 ? "2d" : "3d");
                     if (f->sats)
                        fprintf (o, "<sat>%d</sat>", f->sats);
                     if (!isnan (f->hdop) && f->hdop)
                        fprintf (o, "<hdop>%.1f</hdop>", f->hdop);
                     if (!isnan (f->slow.vdop) && f->slow.vdop)
                        fprintf (o, "<vdop>%.1f</vdop>", f->slow.vdop);
                     if (!isnan (f->slow.pdop) && f->slow.pdop)
                        fprintf (o, "<pdop>%.1f</pdop>", f->slow.pdop);
                     fprintf (o, "</trkpt>\r\n");
                  }
               } else
               {
                  jo_t j = log_line (f);
                  char *l = jo_finisha (&j);
                  if (line++)
                     fprintf (o, ",\r\n");
                  fprintf (o, "  %s", l);
                  free (l);
               }
            }
            if (f->setodo)
            {
               if (!odo0)
                  odo0 = f->odo;
               odo1 = f->odo;
            }
            if (f->sett && f->setecef && f->setlla)
            {
               endhome = (f->home | b.home);
               endtime = f->ecef.t;
               endlat = f->lat;
               endlon = f->lon;
            }
            fixadd (&fixfree, f);       // Discard
         }
         if (o)
         {                      // Close file
            if (odo0 >= ODOBASE && odo1 >= ODOBASE && odo1 > odo0)
               odo1 -= odo0;
            else
               odo0 = odo1 = 0;
            // Postcode open is second file
            char *startpostcode = getpostcode (startlat, startlon);
            char *endpostcode = getpostcode (endlat, endlon);
            ESP_LOGE (TAG, "Close file");
            if (loggpx)
            {
               fprintf (o, "</trkseg></trk>\r\n"        //
                        "</gpx>\r\n");
            } else
            {
               if (logpos)
                  fprintf (o, "\r\n ]");
               jo_t j = jo_object_alloc ();
               if (starttime)
               {
                  char *ts = getts (starttime, 0);
                  jo_object (j, "start");
                  jo_string (j, "ts", ts);
                  jo_litf (j, "lat", "%.9lf", startlat);
                  jo_litf (j, "lon", "%.9lf", startlon);
                  jo_bool (j, "home", starthome);
                  if (startpostcode)
                     jo_string (j, "postcode", startpostcode);
                  jo_close (j);
                  free (ts);
               }
               if (endtime)
               {
                  char *ts = getts (endtime, 0);
                  jo_object (j, "end");
                  jo_string (j, "ts", ts);
                  jo_litf (j, "lat", "%.9lf", endlat);
                  jo_litf (j, "lon", "%.9lf", endlon);
                  jo_bool (j, "home", endhome);
                  if (endpostcode)
                     jo_string (j, "postcode", endpostcode);
                  jo_close (j);
                  free (ts);
               }
               if (odo1)
                  jo_litf (j, "distance", "%lld.%02lld", odo1 / 100, odo1 % 100);
               char *json = jo_finisha (&j);
               fprintf (o, ",\r\n%s\r\n", json + 1);
               free (json);
            }
            fclose (o);
            jo_t j = jo_object_alloc ();
            jo_string (j, "action", cardstatus = "Log file closed");
            jo_string (j, "filename", filename);
            revk_info ("SD", &j);
            if (!csvtime)
               csvtime = starttime;
            if (logcsv)
            {
               char *ts = getts (csvtime, '-');
               sprintf (filename, "%s/%s.csv", sd_mount, ts);
               free (ts);
               o = fopen (filename, "r");
               if (o)
               {                // Append
                  fclose (o);
                  o = fopen (filename, "a");
               } else
               {                // Create
                  o = fopen (filename, "w");
                  if (o)
                  {
                     ESP_LOGI (TAG, "Open file %s", filename);
                     jo_t j = jo_object_alloc ();
                     jo_string (j, "action", cardstatus = "CSV file created");
                     jo_string (j, "filename", filename);
                     revk_info ("SD", &j);
                     fprintf (o, "\"Time\",\"Latitude\",\"Longitude\"");
                     if (b.postcode)
                        fprintf (o, ",\"Closest postcode\"");
                     fprintf (o, ",\"Distance\"\r\n");
                  }
               }
               if (!o)
               {
                  ESP_LOGE (TAG, "Failed open file %s", filename);
                  jo_t j = jo_object_alloc ();
                  jo_string (j, "error", cardstatus = "Failed to create CSV file");
                  jo_string (j, "filename", filename);
                  revk_error ("SD", &j);
               } else
               {
                  char *ts = getts (starttime, 0);
                  fprintf (o, "%s,%.9lf,%.9lf", ts, startlat, startlon);
                  if (b.postcode)
                     fprintf (o, ",\"%s\"", startpostcode ? : "");
                  fprintf (o, "\r\n");
                  free (ts);
                  ts = getts (endtime, 0);
                  fprintf (o, "%s,%.9lf,%.9lf", ts, endlat, endlon);
                  free (ts);
                  if (b.postcode)
                     fprintf (o, ",\"%s\"", endpostcode ? : "");
                  if (odo1)
                     fprintf (o, ",%lld.%02lld", odo1 / 100, odo1 % 100);
                  fprintf (o, "\r\n\r\n");
                  fclose (o);
               }
               free (startpostcode);
               free (endpostcode);
            }
         }
         checkupload ();
      }
      rgbsd = 'B';
      // All done, unmount partition and disable SPI peripheral
      esp_vfs_fat_sdcard_unmount (sd_mount, card);
      ESP_LOGI (TAG, "Card dismounted");
      {
         jo_t j = jo_object_alloc ();
         jo_string (j, "action", cardstatus = "Dismounted");
         revk_info ("SD", &j);
      }
   }
   vTaskDelete (NULL);
}

int
bargraph (char c, int p)
{
   if (p <= 0 || leds <= ledsd)
      return 0;
   int n = 255 * p * (leds - ledsd) / 100;
   int q = n / 255;
   n &= 255;
   int l = leds;
   while (q-- && l > ledsd)
      revk_led (strip, --l, 255, revk_rgb (c));
   if (n && l > ledsd)
      revk_led (strip, --l, n, revk_rgb (c));
   while (l > ledsd)
      revk_led (strip, --l, 255, revk_rgb ('K'));
   return p;
}

void
rgb_task (void *z)
{
   if (!leds || !strip)
   {
      vTaskDelete (NULL);
      return;
   }
   uint8_t blink = 0;
   while (!b.die)
   {
      usleep (100000);
      if (++blink >= 10)
         blink = 0;
      const uint8_t fades[] = { 128, 153, 178, 203, 228, 255, 228, 203, 178, 153 };
      uint8_t fade = fades[blink];
      int l = 0;
      if (ledsd)
         revk_led (strip, l++, b.sdwaiting ? fade : 255, revk_rgb (rgbsd));     // SD status (blink if data waiting)
      if (!bargraph ('Y', revk_ota_progress ()) && !bargraph ('C', upload))
      {
         if (b.sbas)
            revk_led (strip, l++, 255, revk_rgb ('M')); // SBAS
         for (int s = 0; s < SYSTEMS; s++)
         {                      // Two active sats per LED
            for (int n = 0; n < status.gsa[s] / 2 && l < leds; n++)
               revk_led (strip, l++, status.fixmode < 3 ? fade : 255, revk_rgb (system_colour[s]));
            if ((status.gsa[s] & 1) && l < leds)
               revk_led (strip, l++, 127, revk_rgb (system_colour[s])); // dim as 1 LED (no need to blink really)
         }
         if (l <= ledsd)
            revk_led (strip, l++, (status.fixmode < 3 ? fade : 255), revk_rgb ('R'));   // No sats (likely always blinking)
         while (l < leds)
            revk_led (strip, l++, 255, revk_rgb ('K'));
         // Flag issues in last LED
         if (!zdadue)
            revk_led (strip, leds - 1, b.charging ? fade : 255, revk_rgb ('R'));        // No GPS clock
         else if (!b.moving)
            revk_led (strip, leds - 1, b.charging ? fade : 255, revk_rgb (b.home ? 'O' : 'M')); // Not moving
      }
      led_strip_refresh (strip);
   }
   for (int i = 0; i < leds; i++)
      revk_led (strip, i, 255, revk_rgb ('K'));
   led_strip_refresh (strip);
   vTaskDelete (NULL);
}

void
register_uri (const httpd_uri_t * uri_struct)
{
   esp_err_t res = httpd_register_uri_handler (webserver, uri_struct);
   if (res != ESP_OK)
   {
      ESP_LOGE (TAG, "Failed to register %s, error code %d", uri_struct->uri, res);
   }
}

void
register_get_uri (const char *uri, esp_err_t (*handler) (httpd_req_t * r))
{
   httpd_uri_t uri_struct = {
      .uri = uri,
      .method = HTTP_GET,
      .handler = handler,
   };
   register_uri (&uri_struct);
}

void
web_head (httpd_req_t * req, const char *title)
{
   revk_web_head (req, title);
   revk_web_send (req, "<style>"        //
                  "body{font-family:sans-serif;background:#8cf;}"       //
                  "</style><body><h1>%s</h1>", title ? : "");
}

esp_err_t
web_root (httpd_req_t * req)
{
   web_head (req, *hostname ? hostname : appname);
   if (b.sdpresent)
   {
      if (sdsize)
         revk_web_send (req, "<p>%.1fGB SD card inserted%s</p>", (float) sdsize / 1000000000LL,
                        b.sdwaiting ? " (data waiting to upload)" : " (empty)");
      revk_web_send (req, "<p><i>Remove SD card to access settings</i>%s</p>", *url ? "" : " <b>Upload URL not set</b>");
   } else
      revk_web_send (req, "<p>SD card not present</p>");
   if (b.home)
      revk_web_send (req, "<p>At home</p>");
   return revk_web_foot (req, 0, 1, NULL);
}

void
revk_web_extra (httpd_req_t * req)
{
   revk_web_setting_s (req, "Target", "url", url, "URL or Email address", NULL, 0);
   if (strchr (url, '@'))
   {
      revk_web_setting_s (req, "Email host", "emailhost", emailhost, "Email host", NULL, 0);
      revk_web_setting_s (req, "Email port", "emailport", emailport, "Email port", NULL, 0);
      revk_web_setting_s (req, "Email user", "emailuser", emailuser, "Email user", NULL, 0);
      revk_web_setting_s (req, "Email pass", "emailpass", emailpass, "Email pass", NULL, 0);
      revk_web_setting_s (req, "Email from", "emailfrom", emailfrom, "Email from", NULL, 0);
   }
   if (pwr && (usb || (charging
#ifdef  CONFIG_IDF_TARGET_ESP32S3
                       && (charging & IO_MASK) <= 21
#endif
               )))
      revk_web_setting_b (req, "Power down", "powerman", powerman, "Turn off when USB power goes off");
   if (usb)
      revk_web_setting_b (req, "Power stop", "powerstop", powerstop, "End Journey quickly when power goes off");
   revk_web_setting_b (req, "GPX Log", "loggpx", loggpx, "Log files in GPX format");
   revk_web_setting_i (req, "Move time", "move", move, "Seconds moving to start journey (quicker if moving fast)");
   revk_web_setting_i (req, "Stop time", "stop", stop, "Seconds stopped to end journey (quicker if at home)");
   revk_web_send (req, "<tr><td colspan=4>");
   if ((!pos[0] && !pos[1] && !pos[2]) || home[0] != pos[0] || home[1] != pos[1] || home[2] != pos[2])
   {
      if (!isnan (status.hdop) && status.hdop != 0 && status.hdop <= 1 && pos[0])
         revk_web_send (req,
                        "<button onclick='var f=document.settings;f.home1.value=%ld;f.home2.value=%ld;f.home3.value=%ld;'>Set here as home</button>",
                        pos[0], pos[1], pos[2]);
      else
         revk_web_send (req, "Go outside and get a clean fix to set home location.");
      if (!isnan (status.hdop) && status.hdop != 0)
         revk_web_send (req, " HDOP=%.1f", status.hdop);
      if ((home[0] || home[1] || home[2]) && (pos[0] || pos[1] || pos[2]))
         revk_web_send (req, " Moved %.0fm",
                        sqrt ((home[0] - pos[0]) * (home[0] - pos[0]) + (home[1] - pos[1]) * (home[1] - pos[1]) +
                              (home[2] - pos[2]) * (home[2] - pos[2])));
   } else if (b.home)
      revk_web_send (req, "At home");
   revk_web_send (req, "</td></tr>");
   revk_web_setting_i (req, "Home X", "home1", home[0], "ECEF X");
   revk_web_setting_i (req, "Home Y", "home2", home[1], "ECEF Y");
   revk_web_setting_i (req, "Home Z", "home3", home[2], "ECEF Z");
}

void
revk_state_extra (jo_t j)
{
   if (cardstatus)
      jo_string (j, "SD", cardstatus);
   if (b.sdpresent && sdsize)
      jo_int (j, "sdsize", sdsize);
   if (b.charging)
      jo_bool (j, "charging", 1);
   if (b.usb)
      jo_bool (j, "usb", 1);
   int bat = 100 * 2 * (adc[0] - 3.3);
   if (bat < 0)
      bat = 0;
   if (bat > 100)
      bat = 100;
   jo_int (j, "bat", bat);
   jo_litf (j, "voltage", "%.1f", adc[0]);
   // Note adc[2] relates to temp, but not clear of mapping
   jo_close (j);
}

void
app_main ()
{
   revk_boot (&app_callback);
   cmd_mutex = xSemaphoreCreateBinary ();
   xSemaphoreGive (cmd_mutex);
   fix_mutex = xSemaphoreCreateBinary ();
   xSemaphoreGive (fix_mutex);
   vSemaphoreCreateBinary (ack_semaphore);
#define str(x) #x
   revk_register ("gps", 0, sizeof (gpsuart), &gpsuart, NULL, SETTING_SECRET);
   revk_register ("sd", 0, sizeof (sdled), &sdmosi, NULL, SETTING_SECRET | SETTING_BOOLEAN | SETTING_FIX);
   revk_register ("log", 0, sizeof (logpos), &logpos, "1", SETTING_SECRET | SETTING_BOOLEAN);
   revk_register ("pack", 0, sizeof (packdist), &packdist, "0", SETTING_SECRET | SETTING_LIVE);
   revk_register ("acc", 0, sizeof (accaddress), &accaddress, "0x19", SETTING_SECRET | SETTING_FIX);
#define b(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,SETTING_BOOLEAN);
#define bf(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,SETTING_BOOLEAN|SETTING_FIX);
#define bl(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,SETTING_BOOLEAN|SETTING_LIVE);
#define h(n,t) revk_register(#n,0,0,&n,NULL,SETTING_BINDATA|SETTING_HEX);
#define u32(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,0);
#define s32al(n,a,t) revk_register(#n,a,sizeof(*n),&n,NULL,SETTING_LIVE|SETTING_SIGNED);
#define u16(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,0);
#define s8(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,SETTING_SIGNED);
#define u8(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,0);
#define u8f(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,SETTING_FIX);
#define u8l(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,SETTING_LIVE);
#define u16l(n,d,t) revk_register(#n,0,sizeof(n),&n,#d,SETTING_LIVE);
#define s(n,d,t) revk_register(#n,0,0,&n,str(d),0);
#define io(n,d,t)         revk_register(#n,0,sizeof(n),&n,"- "str(d),SETTING_SET|SETTING_BITFIELD|SETTING_FIX);
   settings;
#undef ui
#undef u16
#undef u32
#undef s32al
#undef s8
#undef u8
#undef u8f
#undef u8l
#undef u16l
#undef bl
#undef bf
#undef b
#undef h
#undef s
#undef str
#undef io
   revk_start ();
   if (usb)
   {
      rtc_gpio_deinit (usb & IO_MASK);
      gpio_reset_pin (usb & IO_MASK);
      gpio_pulldown_en (usb & IO_MASK);
      gpio_set_direction (usb & IO_MASK, GPIO_MODE_INPUT);
   }
   if (charging)
   {
      rtc_gpio_deinit (charging & IO_MASK);
      gpio_reset_pin (charging & IO_MASK);
      gpio_set_direction (charging & IO_MASK, GPIO_MODE_INPUT);
   }
   if (pwr)
   {                            // System power
      gpio_hold_dis (pwr & IO_MASK);
      gpio_set_level (pwr & IO_MASK, (pwr & IO_INV) ? 0 : 1);
      gpio_set_direction (pwr & IO_MASK, GPIO_MODE_OUTPUT);
      gpio_hold_en (pwr & IO_MASK);
      usleep (100000);          // Power on
   }
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
   // Main task...
   if (gpstick)
   {
      gpio_reset_pin (gpstick & IO_MASK);
      gpio_set_direction (gpstick & IO_MASK, GPIO_MODE_INPUT);
   }
   gps_connect (gpsbaud);
   acc_init ();
   revk_task ("NMEA", nmea_task, NULL, 5);
   revk_task ("Log", log_task, NULL, 5);
   revk_task ("Pack", pack_task, NULL, 5);
   revk_task ("SD", sd_task, NULL, 10);
   // Web interface
   httpd_config_t config = HTTPD_DEFAULT_CONFIG ();
   config.max_uri_handlers = 5 + revk_num_web_handlers ();
   if (!httpd_start (&webserver, &config))
   {
      register_get_uri ("/", web_root);
      revk_web_settings_add (webserver);
   }
   while (1)
   {
      sleep (1);
      uint32_t up = uptime ();
      if (b.gpsinit)
         gps_init ();
      if (b.moving)
         busy = up;
      b.charging = io (charging);
      b.usb = io (usb);
      if (b.charging || b.usb)
         busy = up;
      if (!revk_shutting_down (NULL) && powerman && pwr && ((usb && !b.usb) || (charging && !b.charging && adc[0] < 3.7
#ifdef  CONFIG_IDF_TARGET_ESP32S3
                                                                            && (charging & IO_MASK) <= 21
#endif
                                                        )) && busy + (b.sdpresent ? 60 : 600) < up)
         revk_restart ("Power down", 1);
   }
}

void
power_shutdown (void)
{
   b.die = 1;
   sleep (2);
   if (powerman && pwr && ((usb && !b.usb) || (charging && !b.charging && adc[0] < 3.7
#ifdef  CONFIG_IDF_TARGET_ESP32S3
                                               && (charging & IO_MASK) <= 21
#endif
                           )))
   {                            // Deep sleep
      jo_t j = jo_object_alloc ();
      jo_string (j, "action", "poweroff");
      revk_error (TAG, &j);
      sleep (5);
      if (pwr)
      {                         // Power down
         ESP_LOGE (TAG, "Power off");
         gpio_hold_dis (pwr & IO_MASK);
         gpio_set_level (pwr & IO_MASK, (pwr & IO_INV) ? 1 : 0);
      }
      ESP_LOGE (TAG, "Deep sleep");
      if (usb)
      {                         // USB based
         rtc_gpio_set_direction_in_sleep (usb & IO_MASK, RTC_GPIO_MODE_INPUT_ONLY);
         rtc_gpio_pullup_dis (usb & IO_MASK);
         rtc_gpio_pulldown_en (usb & IO_MASK);
         esp_sleep_enable_ext0_wakeup (usb & IO_MASK, (usb & IO_INV) ? 0 : 1);
      } else
      {                         // Charging based
         rtc_gpio_set_direction_in_sleep (charging & IO_MASK, RTC_GPIO_MODE_INPUT_ONLY);
         rtc_gpio_pullup_en (charging & IO_MASK);
         rtc_gpio_pulldown_dis (charging & IO_MASK);
         esp_sleep_enable_ext0_wakeup (charging & IO_MASK, (charging & IO_INV) ? 0 : 1);
      }
      esp_deep_sleep (1000000LL * 3600LL);      // Sleep an hour
   }
}
