// GPS module (tracker and/or display module)
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)
static const char TAG[] = "GPS";

#define	TSCALE	100             // Per second
#define	DSCALE	100000          // Per angle minute

#include "revk.h"
#include <esp32/aes.h>
#include <driver/i2c.h>
#include <driver/uart.h>
#include <math.h>
#include "oled.h"
#include "esp_sntp.h"
#include "esp_crc.h"

#define settings	\
	s8(oledsda,27)	\
	s8(oledscl,14)	\
	s8(oledaddress,0x3D)	\
	u8(oledcontrast,127)	\
	b(oledflip,Y)	\
	u32(gpsbaud,9600) \
	b(gpsdebug,N)	\
	s8(gpspps,34)	\
	s8(gpsuart,1)	\
	s8(gpsrx,33)	\
	s8(gpstx,32)	\
	s8(gpsfix,25)	\
	s8(gpsen,26)	\
      	b(atdebug,N)    \
        s8(atuart,2)	\
        u32(atbaud,115200)	\
        s8(attx,27)	\
        s8(atrx,26)	\
        s8(atkey,4)	\
        s8(atrst,5)	\
        s8(atpwr,-1)	\
	u8(fixpersec,10)\
	b(fixdump,N)	\
	b(fixdebug,N)	\
	b(battery,Y)	\
	u32(interval,600)\
	u32(keepalive,60)\
	u32(secondcm,10)\
	u32(margincm,50)\
	u32(retrycm,10)\
	u32(altscale,10)\
	s(apn,"mobiledata")\
	s(loghost,"mqtt.revk.uk")\
	u32(logport,6666)\
	h(aes,16)	\
	u8(sun,0)	\
	u32(logslow,0)	\
	u32(logfast,0)	\
	u32(speedlow,1)	\
	u32(speedhigh,4)\
	b(waas,Y)	\
	b(sbas,Y)	\
	b(aic,Y)	\
	b(easy,Y)	\
	b(always,N)	\
	b(backup,N)	\
	b(mph, Y)	\

#define u32(n,d)	uint32_t n;
#define s8(n,d)	int8_t n;
#define u8(n,d)	uint8_t n;
#define b(n,d) uint8_t n;
#define h(n,b) uint8_t n[b];
#define s(n,d) char * n;
settings
#undef u32
#undef s8
#undef u8
#undef b
#undef h
#undef s
float speed = 0;
float bearing = 0;
float lat = 0;
float lon = 0;
float alt = 0;
float gsep = 0;
float pdop = 0;
float hdop = 0;
float vdop = 0;
float course = 0;
int sats = 0;
int fixtype = 0;
int fixmode = 0;
char gotfix = 0;
char lonforce = 0;
char latforce = 0;
char altforce = 0;
char timeforce = 0;
char speedforce = 0;
char courseforce = 0;
char hdopforce = 0;
char pdopforce = 0;
char vdopforce = 0;
char gpsstarted = 0;
char iccid[22] = { };
char imei[22] = { };

#define MINL	0.1
time_t gpszda = 0;              // Last ZDA
time_t basetim = 0;             // Base time for fixtim
typedef struct fix_s fix_t;
struct fix_s
{                               // 12 byte fix data
   unsigned char keep:1;
   short tim:15;                // 0.1 seconds
   short alt;                   // 1 metre
   int lat;                     // min*10000
   int lon;                     // min*10000
};
#define	MAXFIX 6600
#define MAXTRACK 16
#define MAXDATA 1450            // Size of packet
#define	MAXSEND	((MAXDATA-8-2-4-28)/16*16/sizeof(fix_t))
uint8_t track[MAXTRACK][MAXDATA];
int tracklen[MAXTRACK] = { };

volatile unsigned int tracki = 0,
   tracko = 0;
volatile char trackmqtt = 0;
volatile uint32_t trackbase = 0;        // Send tracking for records after this time
SemaphoreHandle_t track_mutex = NULL;
fix_t fix[MAXFIX];
unsigned int fixnext = 0;       // Next fix to store
volatile int fixsave = 0;       // Time to save fixes
volatile int fixmove = 0;       // move base
volatile char fixnow = 0;       // Force fix

SemaphoreHandle_t cmd_mutex = NULL;
SemaphoreHandle_t at_mutex = NULL;

#include "day.h"
#include "night.h"

time_t sun_find_crossing (time_t start_time, double latitude, double longitude, double wanted_altitude);
void sun_position (double t, double latitude, double longitude, double *altitudep, double *azimuthp);

#define DEGS_PER_RAD             (180 / M_PI)
#define SECS_PER_DAY             86400

#define MEAN_ANOMOLY_C           6.24005822136
#define MEAN_ANOMOLY_K           0.01720196999
#define MEAN_LONGITUDE_C         4.89493296685
#define MEAN_LONGITUDE_K         0.01720279169
#define LAMBDA_K1                0.03342305517
#define LAMBDA_K2                0.00034906585
#define MEAN_OBLIQUITY_C         0.40908772337
#define MEAN_OBLIQUITY_K         -6.28318530718e-09

#define MAX_ERROR                0.0000001

#define SUN_SET                         (-50.0/60.0)
#define SUN_CIVIL_TWILIGHT              (-6.0)
#define SUN_NAUTICAL_TWILIGHT           (-12.0)
#define SUN_ASTRONOMICAL_TWILIGHT       (-18.0)

time_t
sun_rise (int y, int m, int d, double latitude, double longitude, double sun_altitude)
{
 struct tm tm = { tm_mon: m - 1, tm_year: y - 1900, tm_mday: d, tm_hour:6 - longitude / 15 };
   return sun_find_crossing (mktime (&tm), latitude, longitude, sun_altitude);
}

time_t
sun_set (int y, int m, int d, double latitude, double longitude, double sun_altitude)
{
 struct tm tm = { tm_mon: m - 1, tm_year: y - 1900, tm_mday: d, tm_hour:18 - longitude / 15 };
   return sun_find_crossing (mktime (&tm), latitude, longitude, sun_altitude);
}

time_t
sun_find_crossing (time_t start_time, double latitude, double longitude, double wanted_altitude)
{
   double t,
     last_t,
     new_t,
     altitude,
     error,
     last_altitude;
   time_t result;
   int try = 10;

   last_t = (double) start_time;
   sun_position (last_t, latitude, longitude, &last_altitude, NULL);
   t = last_t + 1;
   do
   {
      if (!try--)
         return 0;              // Not found
      sun_position (t, latitude, longitude, &altitude, NULL);
      error = altitude - wanted_altitude;
      result = (time_t) (0.5 + t);

      new_t = t - error * (t - last_t) / (altitude - last_altitude);
      last_t = t;
      last_altitude = altitude;
      t = new_t;
   } while (fabs (error) > MAX_ERROR);
   return result;
}

void
sun_position (double t, double latitude, double longitude, double *altitudep, double *azimuthp)
{
   struct tm tm;
   time_t j2000_epoch;

   double latitude_offset;      // Site latitude offset angle (NORTH = +ve)
   double longitude_offset;     // Site longitude offset angle (EAST = +ve)
   double j2000_days;           // Time/date from J2000.0 epoch (Noon on 1/1/2000)
   double clock_angle;          // Clock time as an angle
   double mean_anomoly;         // Mean anomoly angle
   double mean_longitude;       // Mean longitude angle
   double lambda;               // Apparent longitude angle (lambda)
   double mean_obliquity;       // Mean obliquity angle
   double right_ascension;      // Right ascension angle
   double declination;          // Declination angle
   double eqt;                  // Equation of time angle
   double hour_angle;           // Hour angle (noon = 0, +ve = afternoon)
   double altitude;
   double azimuth;

   latitude_offset = latitude / DEGS_PER_RAD;
   longitude_offset = longitude / DEGS_PER_RAD;

//   printf("lat %f, long %f\n", latitude_offset * DEGS_PER_RAD, longitude_offset * DEGS_PER_RAD);

   // Calculate clock angle based on UTC unixtime of user supplied time
   clock_angle = 2 * M_PI * fmod (t, SECS_PER_DAY) / SECS_PER_DAY;

   // Convert localtime 'J2000.0 epoch' (noon on 1/1/2000) to unixtime
   tm.tm_sec = 0;
   tm.tm_min = 0;
   tm.tm_hour = 12;
   tm.tm_mday = 1;
   tm.tm_mon = 0;
   tm.tm_year = 100;
   tm.tm_wday = tm.tm_yday = tm.tm_isdst = 0;
   j2000_epoch = mktime (&tm);

   j2000_days = (double) (t - j2000_epoch) / SECS_PER_DAY;

   // Calculate mean anomoly angle (g)
   // [1] g = g_c + g_k * j2000_days
   mean_anomoly = MEAN_ANOMOLY_C + MEAN_ANOMOLY_K * j2000_days;

   // Calculate mean longitude angle (q)
   // [1] q = q_c + q_k * j2000_days
   mean_longitude = MEAN_LONGITUDE_C + MEAN_LONGITUDE_K * j2000_days;

   // Calculate apparent longitude angle (lambda)
   // [1] lambda = q + l_k1 * sin(g) + l_k2 * sin(2 * g)
   lambda = mean_longitude + LAMBDA_K1 * sin (mean_anomoly) + LAMBDA_K2 * sin (2 * mean_anomoly);

   // Calculate mean obliquity angle (e)     No trim - always ~23.5deg
   // [1] e = e_c + e_k * j2000_days
   mean_obliquity = MEAN_OBLIQUITY_C + MEAN_OBLIQUITY_K * j2000_days;

   // Calculate right ascension angle (RA)   No trim - atan2 does trimming
   // [1] RA = atan2(cos(e) * sin(lambda), cos(lambda))
   right_ascension = atan2 (cos (mean_obliquity) * sin (lambda), cos (lambda));

   // Calculate declination angle (d)        No trim - asin does trimming
   // [1] d = asin(sin(e) * sin(lambda))
   declination = asin (sin (mean_obliquity) * sin (lambda));

   // Calculate equation of time angle (eqt)
   // [1] eqt = q - RA
   eqt = mean_longitude - right_ascension;

   // Calculate sun hour angle (h)
   // h = clock_angle + long_o + eqt - PI
   hour_angle = clock_angle + longitude_offset + eqt - M_PI;

   // Calculate sun altitude angle
   // [2] alt = asin(cos(lat_o) * cos(d) * cos(h) + sin(lat_o) * sin(d))
   altitude =
      DEGS_PER_RAD * asin (cos (latitude_offset) * cos (declination) * cos (hour_angle) +
                           sin (latitude_offset) * sin (declination));

   // Calculate sun azimuth angle
   // [2] az = atan2(sin(h), cos(h) * sin(lat_o) - tan(d) * cos(lat_o))
   azimuth =
      DEGS_PER_RAD * atan2 (sin (hour_angle), cos (hour_angle) * sin (latitude_offset) - tan (declination) * cos (latitude_offset));

   if (altitudep)
      *altitudep = altitude;
   if (azimuthp)
      *azimuthp = azimuth;
}

/***************************************************************************/
/* References for equations:-

[1] U.S. Naval Observatory Astronomical Applications Department
    http://aa.usno.navy.mil/AA/faq/docs/SunApprox.html

[2] Astronomical Formulae for Calculators, Jean Meeus, Page 44

[3] Ben Mack, 'Tate - louvre angle calcs take 3', 10/12/1999

*/
/***************************************************************************/

void
gpscmd (const char *fmt, ...)
{                               // Send command to UART
   char s[100];
   va_list ap;
   va_start (ap, fmt);
   vsnprintf (s, sizeof (s) - 5, fmt, ap);
   va_end (ap);
   uint8_t c = 0;
   char *p;
   for (p = s + 1; *p; p++)
      c ^= *p;
   if (gpsdebug)
      revk_info ("tx", "%s", s);
   if (*s == '$')
      p += sprintf (p, "*%02X\r\n", c); // We allowed space
   xSemaphoreTake (cmd_mutex, portMAX_DELAY);
   uart_write_bytes (gpsuart, s, p - s);
   xSemaphoreGive (cmd_mutex);
}

#define ATBUFSIZE 2000
char *atbuf = NULL;
int
atcmd (const void *cmd, int t1, int t2)
{                               // TODO process response cleanly...
   xSemaphoreTake (at_mutex, portMAX_DELAY);
   if (cmd)
   {
      uart_write_bytes (atuart, cmd, strlen ((char *) cmd));
      uart_write_bytes (atuart, "\r", 1);
      if (atdebug)
         revk_info ("attx", "%s", cmd);
   }
   int l = uart_read_bytes (atuart, (void *) atbuf, 1, (t1 ? : 100) / portTICK_PERIOD_MS);
   if (l > 0)
   {                            // initial response started
      int l2 = uart_read_bytes (atuart, (void *) atbuf + l, ATBUFSIZE - l - 1, 10 / portTICK_PERIOD_MS);
      if (l2 > 0 && t2)
      {                         // End of initial response
         l += l2;
         l2 = uart_read_bytes (atuart, (void *) atbuf + l, 1, t2 / portTICK_PERIOD_MS);
         if (l2 > 0)
         {                      // Secondary response started
            l2 = uart_read_bytes (atuart, (void *) atbuf + l, ATBUFSIZE - l - 1, 10 / portTICK_PERIOD_MS);
            if (l2 > 0)
               l += l2;         // End of secondary response
            else
               l = l2;
         }
      } else
         l = l2;
   }
   if (l >= 0)
   {
      atbuf[l] = 0;
      if (l && atdebug)
         revk_info ("atrx", "%s", atbuf);
   } else
      atbuf[0] = 0;
   xSemaphoreGive (at_mutex);
   return l;
}

void
lograte (int rate)
{
   static int lastrate = -1;
   if (lastrate == rate)
      return;
   if (!rate)
      gpscmd ("$PMTK185,1");    // Stop log
   else
   {
      if (lastrate <= 0)
         gpscmd ("$PMTK185,0"); // Start log
      gpscmd ("$PMTK187,1,%d", rate);
   }
   lastrate = rate;
}

const char *
app_command (const char *tag, unsigned int len, const unsigned char *value)
{
   if (!strcmp (tag, "test"))
   {
      revk_info ("test", "BE~%08X LE~%08X", esp_crc32_be (0, value, len), esp_crc32_le (0, value, len));
      return "";
   }
   if (!strcmp (tag, "contrast"))
   {
      oled_set_contrast (atoi ((char *) value));
      return "";                // OK
   }
   if (!strcmp (tag, "wifi"))
   {                            // WiFi connected, but not need for SNTP as we have GPS
      sntp_stop ();
      return "";
   }
   if (!strcmp (tag, "disconnect"))
   {
      xSemaphoreTake (track_mutex, portMAX_DELAY);
      trackmqtt = 0;
      xSemaphoreGive (track_mutex);
      return "";
   }
   if (!strcmp (tag, "connect"))
   {
      xSemaphoreTake (track_mutex, portMAX_DELAY);
      trackmqtt = 1;
      xSemaphoreGive (track_mutex);
      if (logslow || logfast)
         gpscmd ("$PMTK183");   // Log status
      if (attx < 0 || atrx < 0)
      {                         // Back to oldest over MQTT
         xSemaphoreTake (track_mutex, portMAX_DELAY);
         if (tracki < MAXTRACK)
            tracko = 0;
         else
            tracko = tracki - MAXTRACK;
         xSemaphoreGive (track_mutex);
      }
      if (*iccid)
         revk_info ("iccid", "%s", iccid);
      if (*imei)
         revk_info ("imei", "%s", imei);
      return "";
   }
   if (!strcmp (tag, "status"))
   {
      gpscmd ("$PMTK183");      // Log status
      return "";
   }
   if (!strcmp (tag, "resend"))
   {                            // Resend log data
      xSemaphoreTake (track_mutex, portMAX_DELAY);
      if (len)
      {
         struct tm t = { };
         strptime ((char *) value, "%F %T", &t);
         trackbase = mktime (&t);
      }
      if (tracki < MAXTRACK)
         tracko = 0;
      else
         tracko = tracki - MAXTRACK;
      xSemaphoreGive (track_mutex);
      return "";
   }
   if (!strcmp (tag, "fix") || !strcmp (tag, "upgrade") || !strcmp (tag, "restart"))
   {                            // Force fix dump now
      fixnow = 1;
      return "";
   }
   if (!strcmp (tag, "time"))
   {
      if (!len)
         timeforce = 0;
      else
      {
         timeforce = 1;
         struct tm t = { };
         int y = 0,
            m = 0,
            d = 0,
            H = 0,
            M = 0,
            S = 0;
         char z = 0;
         sscanf ((char *) value, "%d-%d-%d %d:%d:%d%c", &y, &m, &d, &H, &M, &S, &z);
         t.tm_year = y - 1900;
         t.tm_mon = m - 1;
         t.tm_mday = d;
         t.tm_hour = H;
         t.tm_min = M;
         t.tm_sec = S;
         if (!z)
            t.tm_isdst = -1;
         struct timeval v = { };
         v.tv_sec = mktime (&t);
         settimeofday (&v, NULL);
      }
      return "";
   }
#define force(x) if (!strcmp (tag, #x)) { if (!len) x##force = 0; else { x##force = 1; x = strtof ((char *) value, NULL); } return ""; }
   force (lat);
   force (lon);
   force (alt);
   force (course);
   force (speed);
   force (hdop);
   force (pdop);
   force (vdop);
#undef force
   if (!strcmp (tag, "tx") && len)
   {                            // Send arbitrary GPS command (do not include *XX or CR/LF)
      gpscmd ("%s", value);
      return "";
   }
   if (!strcmp (tag, "attx") && len)
   {                            // Send arbitrary AT command (do not include *XX or CR/LF)
      atcmd (value, 0, 0);
      return "";
   }
   if (!strcmp (tag, "status"))
   {
      gpscmd ("$PMTK18,1");     // Status log
      return "";
   }
   if (!strcmp (tag, "dump"))
   {
      gpscmd ("$PMTK622,1");    // Dump log
      return "";
   }
   if (!strcmp (tag, "erase"))
   {
      gpscmd ("$PMTK184,1");    // Erase
      return "";
   }
   if (!strcmp (tag, "fast"))
   {
      gpscmd ("$PMTK251,115200");       // Baud rate
      revk_setting ("gpsbaud", 6, "115200");    // Set to 115200
      return "";
   }
   if (!strcmp (tag, "hot"))
   {
      gpscmd ("$PMTK101");      // Hot start
      return "";
   }
   if (!strcmp (tag, "warm"))
   {
      gpscmd ("$PMTK102");      // Warm start
      return "";
   }
   if (!strcmp (tag, "cold"))
   {
      gpscmd ("$PMTK103");      // Cold start
      return "";
   }
   if (!strcmp (tag, "reset"))
   {
      gpscmd ("$PMTK104");      // Full cold start (resets to default settings including Baud rate)
      revk_setting ("gpsbaud", 4, "9600");      // Set to 9600
      return "";
   }
   if (!strcmp (tag, "sleep"))
   {
      gpscmd ("$PMTK291,7,0,10000,1");  // Low power (maybe we need to drive EN pin?)
      return "";
   }
   if (!strcmp (tag, "version"))
   {
      gpscmd ("$PMTK605");      // Version
      return "";
   }
   return NULL;
}

static void
nmea (char *s)
{
   if (gpsdebug)
      revk_info ("rx", "%s", s);
   if (!s || *s != '$' || s[1] != 'G' || s[2] != 'P')
      return;
   if (!gpsstarted && (esp_timer_get_time () > 10000000 || !revk_offline ()))
   {                            // The delay is to allow debug logging, etc.
      revk_info (TAG, "GPS running");
      gpscmd ("$PMTK220,%d", 1000 / fixpersec); // Fix rate
      gpscmd ("$PQTXT,W,0,1");  // Disable GPTXT
      gpscmd ("$PMTK314,0,0,%d,1,%d,0,0,0,0,0,0,0,0,0,0,0,0,%d,0", fixpersec, fixpersec * 10, fixpersec * 10);  // What to send
      gpscmd ("$PMTK301,%d", waas ? 2 : 0);     // WAAS
      gpscmd ("$PMTK313,%d", sbas ? 1 : 0);     // SBAS
      gpscmd ("$PMTK513,%d", sbas ? 1 : 0);     // SBAS
      gpscmd ("$PMTK286,%d", aic ? 1 : 0);      // AIC
      gpscmd ("$PMTK869,1,%d", easy ? 1 : 0);   // Easy
      gpscmd ("$PMTK225,%d", (always ? 8 : 0) + (backup ? 1 : 0));      // Periodic
      gpsstarted = 1;
   }
   char *f[50];
   int n = 0;
   s++;
   while (*s && n < sizeof (f) / sizeof (*f))
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
   if (!strncmp (f[0], "GPGGA", 5) && n >= 14)
   {                            // Fix: $GPGGA,093644.000,5125.1569,N,00046.9708,W,1,09,1.06,100.3,M,47.2,M,,

      if (strlen (f[1]) >= 10 && strlen (f[2]) >= 9 && strlen (f[4]) >= 10)
      {
         fixtype = atoi (f[6]);
         sats = atoi (f[7]);
         if (!altforce)
            alt = strtof (f[9], NULL);
         gsep = strtof (f[10], NULL);
         if (!latforce)
            lat = ((f[2][0] - '0') * 10 + f[2][1] - '0' + strtof (f[2] + 2, NULL) / 60) * (f[3][0] == 'N' ? 1 : -1);
         if (!lonforce)
            lon =
               ((f[4][0] - '0') * 100 + (f[4][1] - '0') * 10 + f[4][2] - '0' + strtof (f[4] + 3, NULL) / 60) * (f[5][0] ==
                                                                                                                'E' ? 1 : -1);
         gotfix = 1;
         if (gpszda)
         {                      // Store fix data
            char *p = f[2];
            int s = DSCALE;
            int fixlat = (((p[0] - '0') * 10 + p[1] - '0') * 60 + (p[2] - '0') * 10 + p[3] - '0') * s;
            p += 5;
            while (s && isdigit ((int) *p))
            {
               fixlat += s * (*p++ - '0');
               s /= 10;
            }
            if (f[3][0] == 'S')
               fixlat = 0 - fixlat;
            s = DSCALE;
            p = f[4];
            int fixlon = (((p[0] - '0') * 10 + p[1] - '0') * 60 + (p[2] - '0') * 100 + (p[3] - '0') * 10 + p[4] - '0') * s;
            p += 6;
            while (s && isdigit ((int) *p))
            {
               fixlon += s * (*p++ - '0');
               s /= 10;
            }
            if (f[5][0] == 'W')
               fixlon = 0 - fixlon;
            s = TSCALE;
            p = f[1];
            unsigned int fixtim =
               ((((p[0] - '0') * 10 + p[1] - '0') * 60 + (p[2] - '0') * 10 + p[3] - '0') * 60 + (p[4] - '0') * 10 + p[5] - '0') * s;
            p += 7;
            while (s && isdigit ((int) *p))
            {
               fixlon += s * (*p++ - '0');
               s /= 10;
            }
            if (fixtim / TSCALE + 1000 < (gpszda % 86400))
               fixtim += 864000;        // Day wrap
            if (!basetim)
               basetim = gpszda - 1;
            fixtim -= (basetim - gpszda / 86400 * 86400) * TSCALE;
            int fixalt = round (alt);
            if (fixnext < MAXFIX)
            {
               fix[fixnext].keep = 0;
               fix[fixnext].tim = fixtim;
               fix[fixnext].alt = fixalt;
               fix[fixnext].lat = fixlat;
               fix[fixnext].lon = fixlon;
               if (fixdump)
                  revk_info ("fix", "fix:%u tim=%u lat=%d lon=%d alt=%d", fixnext, fixtim, fixlat, fixlon, fixalt);
               fixnext++;
               if (fixmove)
               {                // Move back fixes
                  unsigned int n,
                    p = 0;
                  int diff = fix[fixmove].tim / TSCALE * TSCALE - TSCALE;
                  basetim += diff / TSCALE;
                  for (n = fixmove; n < fixnext; n++)
                  {
                     fix[p] = fix[n];
                     fix[p].tim -= diff;
                     p++;
                  }
                  fixnext = p;
                  fixmove = 0;
               }
               if (!fixsave && (fixnow || fixnext > MAXFIX - 100 || (gpszda > basetim && gpszda - basetim > interval)))
               {
                  if (fixdebug && fixnow)
                     revk_info ("fix", "Fix forced");
                  if (fixdebug && fixnext > MAXFIX - 100)
                     revk_info ("fix", "Fix full %d", fixnext);
                  if (fixdebug && (gpszda > basetim && gpszda - basetim > interval))
                     revk_info ("fix", "Fix time %u", gpszda - basetim);
                  fixsave = fixnext - 1;        // Save
               }
            }
         }
      }
      return;
   }
   if (!strncmp (f[0], "GPZDA", 5) && n >= 5)
   {                            // Time: $GPZDA,093624.000,02,11,2019,,
      if (strlen (f[1]) == 10 && !timeforce)
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
         gpszda = v.tv_sec = mktime (&t);
         settimeofday (&v, NULL);
      }
      return;
   }
   if (!strncmp (f[0], "GPVTG", 5) && n >= 10)
   {
      if (!courseforce)
         course = strtof (f[1], NULL);
      if (!speedforce)
         speed = strtof (f[7], NULL);
      if (speed > speedhigh)
         lograte (logfast);
      else if (speed < speedlow)
         lograte (logslow);
      return;
   }
   if (!strncmp (f[0], "GPGSA", 5) && n >= 18)
   {
      fixmode = atoi (f[2]);
      if (!pdopforce)
         pdop = strtof (f[15], NULL);
      if (!hdopforce)
         hdop = strtof (f[16], NULL);
      if (!vdopforce)
         vdop = strtof (f[17], NULL);
      return;
   }
}

static void
display_task (void *p)
{
   p = p;
   while (1)
   {
      if (gpspps >= 0)
      {
         int try = 20;
         if (gpio_get_level (gpspps))
            usleep (800000);    // In pulse
         while (!gpio_get_level (gpspps) && try-- > 0)
            usleep (10000);
      } else
         sleep (1);
      oled_lock ();
      int y = CONFIG_OLED_HEIGHT,
         x = 0;
      time_t now = time (0) + 1;
      struct tm t;
      localtime_r (&now, &t);
      if (t.tm_year > 100)
      {
         char temp[30];
         strftime (temp, sizeof (temp), "%F\004%T %Z", &t);
         oled_text (1, 0, 0, temp);
      }
      y -= 10;
      oled_text (1, 0, y, "Fix: %s %2d\002sat%s %s", revk_offline ()? " " : "*", sats, sats == 1 ? " " : "s",
                 speed < speedlow ? "-" : speed > speedhigh ? "+" : " ");
      oled_text (1, CONFIG_OLED_WIDTH - 6 * 6, y, "%6s", fixtype == 2 ? "Diff" : fixtype == 1 ? "GPS" : "No fix");
      y -= 3;                   // Line
      y -= 8;
      if (fixmode > 1)
         oled_text (1, 0, y, "Lat: %11.6f", lat);
      else
         oled_text (1, 0, y, "%16s", "");
      oled_text (1, CONFIG_OLED_WIDTH - 3 * 6, y, fixmode > 1 ? "DOP" : "   ");
      y -= 8;
      if (fixmode > 1)
         oled_text (1, 0, y, "Lon: %11.6f", lon);
      else
         oled_text (1, 0, y, "%16s", "");
      if (fixmode > 1)
         oled_text (1, CONFIG_OLED_WIDTH - 5 * 6 - 2, y, "%5.1fm", hdop);
      else
         oled_text (1, CONFIG_OLED_WIDTH - 5 * 6 - 2, y, "   \002  ");
      y -= 8;
      if (fixmode >= 3)
         oled_text (1, 0, y, "Alt: %6.1fm", alt);
      else
         oled_text (1, 0, y, "%16s", "");
      if (fixmode >= 3)
         oled_text (1, CONFIG_OLED_WIDTH - 5 * 6 - 2, y, "%5.1fm", vdop);
      else
         oled_text (1, CONFIG_OLED_WIDTH - 5 * 6 - 2, y, "   \002  ");
      y -= 3;                   // Line
      if (gotfix)
      {
         // Sun
         y -= 22;
         double sunalt,
           sunazi;
         sun_position (now, lat, lon, &sunalt, &sunazi);
         int o = 0;
         time_t rise,
           set;
         do
            rise = sun_rise (t.tm_year + 1900, t.tm_mon + 1, t.tm_mday + o++, lat, lon, sun ? : SUN_SET);
         while (rise <= now && o < 200);
         o = 0;
         do
            set = sun_set (t.tm_year + 1900, t.tm_mon + 1, t.tm_mday + o++, lat, lon, sun ? : SUN_SET);
         while (set <= now && o < 200);
         if (rise > set)
         {
            oled_icon (0, y, day, 22, 21);
            o = set - now;
         } else
         {
            oled_icon (0, y, night, 22, 21);
            o = rise - now;
            // Moon
#define LUNY 2551442.8768992    // Seconds per lunar cycle
            int s = now - 1571001050;   // Seconds since reference full moon
            int m = ((float) s / LUNY); // full moon count
            s -= (float) m *LUNY;       // seconds since full moon
            float phase = (float) s * M_PI * 2 / LUNY;
#define w (21.0/2.0)
            if (phase < M_PI)
            {                   // dim on right (northern hemisphere)
               float q = w * cos (phase);
               for (int Y = 0; Y < w * 2; Y++)
               {
                  float d = (float) Y + 0.5 - w;
                  float v = q * sqrt (1 - (float) d / w * (float) d / w) + w;
                  int l = ceil (v);
                  if (l)
                     oled_set (l - 1, y + Y, ((float) l - v) * oled_get (l - 1, y + Y));
                  for (int X = l; X < CONFIG_OLED_WIDTH; X++)
                     oled_set (X, Y + y, (X + Y) & 1 ? 0 : oled_get (X, Y + y) >> 3);
               }
            } else
            {                   // dim on left (northern hemisphere)
               float q = -w * cos (phase);
               for (int Y = 0; Y < w * 2; Y++)
               {
                  float d = (float) Y + 0.5 - w;
                  float v = q * sqrt (1 - (float) d / w * (float) d / w) + w;
                  int r = floor (v);
                  if (r < w * 2)
                     oled_set (r, Y + y, ((float) r + 1 - v) * oled_get (r, Y + y));
                  for (int X = 0; X < r; X++)
                     oled_set (X, Y + y, (X + Y) & 1 ? 0 : oled_get (X, Y + y) >> 3);
               }
            }
#undef w
         }
         x = 22;
         if (o / 3600 < 1000)
         {                      // H:MM:SS
            x = oled_text (3, x, y, "%3d", o / 3600);
            x = oled_text (2, x, y, ":%02d", o / 60 % 60);
            x = oled_text (1, x, y, ":%02d", o % 60);
         } else
         {                      // D HH:MM
            x = oled_text (3, x, y, "%3d", o / 86400);
            x = oled_text (2, x, y, "\002%02d", o / 3600 % 24);
            x = oled_text (1, x, y, ":%02d", o / 60 % 60);
         }
         // Sun angle
         x = CONFIG_OLED_WIDTH - 4 - 3 * 6;
         if (fixmode > 1)
            x = oled_text (1, x, y + 14, "%+3.0f", sunalt);
         else
            x = oled_text (1, x, y + 14, "   ");
         x = oled_text (0, x, y + 17, "o");
         y -= 3;                // Line
      }
#if 0                           // Show time
      y -= 8;
      localtime_r (&set, &t);
      strftime (temp, sizeof (temp), "%F\004%T %Z", &t);
      oled_text (1, 0, y, temp);
#endif
      // Speed
      y = 20;
      float s = speed;
      float minspeed = hdop * 2;        // Use as basis for ignoring spurious speeds
      if (mph)
         s /= 1.609344;         // miles
      if (speed >= 999)
         x = oled_text (5, 0, y, "\002---");
      else if (speed >= 99.9)
         x = oled_text (5, 0, y, "\002%3.0f", s);
      else if (hdop && speed > minspeed)
         x = oled_text (5, 0, y, "%4.1f", s);
      else
         x = oled_text (5, 0, y, " 0.0");
      oled_text (-1, CONFIG_OLED_WIDTH - 4 * 6, y + 2, "%4s", mph ? "mph" : "km/h");
      if (hdop && speed > minspeed && speed <= 99.9)
         x = oled_text (-1, CONFIG_OLED_WIDTH - 3 * 6 - 4, y + 24, "%3.0f", course);
      else
         x = oled_text (-1, CONFIG_OLED_WIDTH - 3 * 6 - 4, y + 24, "---");
      oled_text (0, x, y + 24 + 3, "o");
      oled_unlock ();
   }
}

unsigned int
trackmsg (uint8_t * buf, unsigned trackn)
{                               // Make a track message for sending
   unsigned int len = tracklen[trackn % MAXTRACK];
   uint8_t *src = track[trackn % MAXTRACK];
   if (len < 8)
      return len;
   memcpy (buf, src, len);
   if (!trackn || trackn == tracki - MAXTRACK)
      buf[11] = 0;              // Oldest
   else
      buf[11] = 1;              // Not oldest
   unsigned int crc = ~esp_crc32_le (0, buf, len);
   buf[len++] = crc;
   buf[len++] = crc >> 8;
   buf[len++] = crc >> 16;
   buf[len++] = crc >> 24;
   while ((len - 8) & 15)
      buf[len++] = 0;           // Padding
   uint8_t iv[16];
   memcpy (iv, buf, 8);
   memcpy (iv + 8, buf, 8);
   esp_aes_context ctx;
   esp_aes_init (&ctx);
   esp_aes_setkey (&ctx, aes, 128);
   esp_aes_crypt_cbc (&ctx, ESP_AES_ENCRYPT, len - 8, iv, buf + 8, buf + 8);
   esp_aes_free (&ctx);
   return len;
}

void
at_task (void *X)
{
   atbuf = malloc (ATBUFSIZE);
   while (1)
   {
      gpio_set_level (atpwr, 0);
      sleep (1);
      gpio_set_level (atrst, 0);
      gpio_set_level (atpwr, 1);
      sleep (1);
      gpio_set_level (atrst, 1);
      int try = 60;
      while ((atcmd ("AT", 0, 0) < 0 || (strncmp (atbuf, "AT", 2) && strncmp (atbuf, "OK", 2))) && --try)
         sleep (1);
      if (!try)
         continue;              // Cause power cycle and try again
      atcmd ("ATE0", 0, 0);
      atcmd (NULL, 0, 0);
      if (atcmd ("AT+GSN", 0, 0) > 0)
      {
         char *p = atbuf,
            *o = imei;
         while (*p && *p < ' ')
            p++;
         while (isdigit ((int) *p) && o < imei + sizeof (imei) - 1)
            *o++ = *p++;
         *o = 0;
         if (*imei)
            revk_info ("imei", "%s", imei);
      }
      if (atcmd ("AT+CCID", 0, 0) > 0)
      {
         char *p = atbuf,
            *o = iccid;
         while (*p && *p < ' ')
            p++;
         while (isdigit ((int) *p) && o < iccid + sizeof (iccid) - 1)
            *o++ = *p++;
         *o = 0;
         if (*iccid)
            revk_info ("iccid", "%s", iccid);
      }
      atcmd ("AT+CBC", 0, 0);
      int delay = 1;
      try = 10;
      while (1)
      {
         if (!--try)
            break;              // Power cycle
         while (delay--)
            atcmd (NULL, 1000, 0);
         delay = 1;
         if (atcmd ("AT+CIPSHUT", 2000, 0) < 0)
            continue;
         if (!strstr ((char *) atbuf, "SHUT OK"))
            continue;
         while (1)
         {
            sleep (1);
            if (atcmd ("AT+CREG?", 1000, 1000) < 0)
               continue;
            if (!strstr ((char *) atbuf, "OK"))
               continue;
            if (strstr ((char *) atbuf, "+CREG: 0,1") || strstr ((char *) atbuf, "+CREG: 0,5"))
               break;
         }
         {
            char temp[200];
            snprintf (temp, sizeof (temp), "AT+CSTT=\"%s\"", apn);
            if (atcmd (temp, 1000, 0) < 0)
               continue;
            if (!strstr ((char *) atbuf, "OK"))
               continue;
         }
         if (atcmd ("AT+CIICR", 10000, 0) < 0)
            continue;
         if (!strstr ((char *) atbuf, "OK"))
            continue;
         delay = 600;           // We established a connection so don't immediately close and re-establish as that is rude
         if (atcmd ("AT+CIFSR", 20000, 0) < 0)
            continue;
         if (!strstr ((char *) atbuf, "."))
            continue;           // Yeh, not an OK after the IP!!! How fucking stupid
#if 0
         if (atcmd ("AT+CIPMUX=0", 1000) < 0)
            continue;
         if (!strstr ((char *) atbuf, "OK"))
            continue;
#endif
         {
            char temp[200];
            snprintf (temp, sizeof (temp), "AT+CIPSTART=\"UDP\",\"%s\",%d", loghost, logport);
            if (atcmd (temp, 1000, 10000) < 0)
               continue;
            if (!strstr ((char *) atbuf, "OK"))
               continue;
         }
         if (!strstr ((char *) atbuf, "CONNECT OK"))
            continue;
         try = 10;
         revk_info (TAG, "Mobile connected");
         while (1)
         {                      // Connected, send data as needed
            if (!trackmqtt && tracko < tracki)
            {                   // Send data
               xSemaphoreTake (track_mutex, portMAX_DELAY);
               if (tracko < tracki)
               {
                  if (tracko + MAXTRACK >= tracki)
                  {             // Not wrapped
                     uint8_t buf[MAXDATA];
                     unsigned int len = trackmsg (buf, tracko);
                     if (len)
                     {
                        uint32_t ts = (buf[4] << 24) + (buf[5] << 16) + (buf[6] << 8) + buf[7];
                        if (ts > trackbase)
                        {
                           char temp[30];
                           snprintf (temp, sizeof (temp), "AT+CIPSEND=%d", len);
                           if (atcmd (temp, 1000, 0) < 0 || !strstr (atbuf, ">"))
                           {
                              xSemaphoreGive (track_mutex);
                              break;
                           }
                           uart_write_bytes (atuart, (void *) buf, len);
                           if (atcmd (NULL, 10000, 0) < 0 || !strstr (atbuf, "SEND OK"))
                           {
                              xSemaphoreGive (track_mutex);
                              break;
                           }
                        }
                     }
                  }
                  tracko++;
               }
               xSemaphoreGive (track_mutex);
            }
            sleep (1);          // Rate limit sending anyway
            // TODO keep alive
         }
         revk_info (TAG, "Mobile disconnected");
      }
   }
}

void
log_task (void *z)
{                               // Log via MQTT
   while (1)
   {
      if (trackmqtt && tracko < tracki)
      {                         // Send data
         xSemaphoreTake (track_mutex, portMAX_DELAY);
         if (tracko < tracki)
         {
            if (tracko + MAXTRACK >= tracki)
            {
               uint8_t buf[MAXDATA];
               unsigned int len = trackmsg (buf, tracko);
               if (len)
               {
                  uint32_t ts = (buf[4] << 24) + (buf[5] << 16) + (buf[6] << 8) + buf[7];
                  if (ts > trackbase)
                     revk_raw ("info", "udp", len, buf, 0);
               }
            }
            tracko++;
         }
         xSemaphoreGive (track_mutex);
      } else
         sleep (1);             // Wait for next message to send
   }
}

void
nmea_task (void *z)
{
   uint8_t buf[1000],
    *p = buf;
   while (1)
   {
      // Get line(s), the timeout should mean we see one or more whole lines typically
      int l = uart_read_bytes (gpsuart, p,
                               buf + sizeof (buf) - p,
                               10 / portTICK_PERIOD_MS);
      if (l < 0)
         sleep (1);
      if (l <= 0)
         continue;
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
               revk_error (TAG, "[%.*s] (%02X)", l - p, p, c);
            else
            {                   // Process line
               l[-3] = 0;
               if (p[1] == 'G')
                  nmea ((char *) p);
               else
               {
                  if (!strcmp ((char *) p, "$PMTK010,1"))
                     gpsstarted = 0;    // Resend config
                  if (strncmp ((char *) p, "$PMTK001", 8))
                     revk_info ("rx", "%s", p); // Other packet
               }
            }
         } else if (l > p)
            revk_error (TAG, "[%.*s]", l - p, p);
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

unsigned int
rdp (unsigned int H, unsigned int margincm, unsigned int *dlostp, unsigned int *dkeptp)
{                               // Reduce, non recursive
   float dlost = 0,
      dkept = 0;
   unsigned int l = 0,
      h = H;                    // Progress
   fix[0].keep = 1;
   fix[H].keep = 1;
   float marginsq = (float) margincm * (float) margincm / 10000.0;
   while (l < H)
   {
      if (l == h)
         for (h++; h < H && !fix[h].keep; h++); // Find second half
      if (l + 1 == h)
      {                         // No points
         l++;
         continue;
      }
      fix_t *a = &fix[l];
      fix_t *b = &fix[h];
      // Centre for working out metres
      int clat = a->lat / 2 + b->lat / 2;
      int clon = a->lon / 2 + b->lon / 2;
      int calt = ((int) a->alt + (int) b->alt) / 2;
      int ctim = ((int) a->tim + (int) b->tim) / 2;
      float slat = 111111.0 * cos (M_PI * clat / 60.0 / 180.0 / DSCALE);
      inline float x (fix_t * p)
      {
         return (float) (p->lon - clon) * slat / 60.0 / DSCALE;
      }
      inline float y (fix_t * p)
      {
         return (float) (p->lat - clat) * 111111.0 / 60.0 / DSCALE;
      }
      inline float z (fix_t * p)
      {
         return (float) (p->alt - calt) / (float) altscale;
      }
      inline float t (fix_t * p)
      {
         return (float) (p->tim - ctim) * secondcm / 100.0 / TSCALE;
      }
      inline float distsq (float dx, float dy, float dz, float dt)
      {                         // Distance in 4D space
         return dx * dx + dy * dy + dz * dz + dt * dt;
      }
      float DX = x (b) - x (a);
      float DY = y (b) - y (a);
      float DZ = z (b) - z (a);
      float DT = t (b) - t (a);
      float LSQ = distsq (DX, DY, DZ, DT);
      int bestn = -1;
      float best = 0;
      int n;
      for (n = l + 1; n < h; n++)
      {
         fix_t *p = &fix[n];
         if (p->tim)
         {
            float d = 0;
            if (!LSQ)
               d = distsq (x (p) - x (a), y (p) - y (a), z (p) - z (a), t (p) - t (a)); // Simple distance from point
            else
            {
               float T = ((x (p) - x (a)) * DX + (y (p) - y (a)) * DY + (z (p) - z (a)) * DZ + (t (p) - t (a)) * DT) / LSQ;
               d = distsq (x (a) + T * DX - x (p), y (a) + T * DY - y (p), z (a) + T * DZ - z (p), t (a) + T * DT - t (p));
            }
            if (bestn >= 0 && d <= best)
               continue;
            bestn = n;          // New furthest
            best = d;
         }
      }
      if (best < marginsq)
      {                         // All points are within margin - so all to be pruned
         if (best > dlost)
            dlost = best;
         for (n = l + 1; n < h; n++)
            fix[n].tim = 0;
         l = h;                 // Next block
         continue;
      }
      if (!dkept || best < dkept)
         dkept = best;
      fix[bestn].keep = 1;      // keep this middle point
      h = bestn;                // First half recursive
   }
   // Pack
   unsigned int i,
     o = 0;
   for (i = 0; i <= H; i++)
      if (fix[i].tim)
      {
         fix[o] = fix[i];
         fix[o].keep = 0;
         o++;
      }
   if (dlostp)
      *dlostp = ceil (sqrt (dlost) * 100.0);
   if (dkeptp)
      *dkeptp = floor (sqrt (dkept) * 100.0);
   return o - 1;
}

void
app_main ()
{
   esp_err_t err;
   cmd_mutex = xSemaphoreCreateMutex ();        // Shared command access
   at_mutex = xSemaphoreCreateMutex (); // Shared command access
   track_mutex = xSemaphoreCreateMutex ();
   revk_init (&app_command);
#define b(n,d) revk_register(#n,0,sizeof(n),&n,#d,SETTING_BOOLEAN);
#define h(n,b) revk_register(#n,0,sizeof(n),&n,NULL,SETTING_BINARY|SETTING_HEX);
#define u32(n,d) revk_register(#n,0,sizeof(n),&n,#d,0);
#define s8(n,d) revk_register(#n,0,sizeof(n),&n,#d,SETTING_SIGNED);
#define u8(n,d) revk_register(#n,0,sizeof(n),&n,#d,0);
#define s(n,d) revk_register(#n,0,0,&n,d,0);
   settings
#undef u32
#undef s8
#undef u8
#undef b
#undef h
#undef s
      if (oledsda >= 0 && oledscl >= 0)
      oled_start (1, oledaddress, oledscl, oledsda, oledflip);
   oled_set_contrast (oledcontrast);
   for (int x = 0; x < CONFIG_OLED_WIDTH; x++)
   {
      oled_set (x, CONFIG_OLED_HEIGHT - 12, 4);
      oled_set (x, CONFIG_OLED_HEIGHT - 12 - 3 - 24, 4);
      oled_set (x, CONFIG_OLED_HEIGHT - 12 - 3 - 24 - 3 - 22, 4);
      oled_set (x, 8, 4);
   }
   if (oledsda >= 0 && oledscl >= 0)
      revk_task ("Display", display_task, NULL);
   // Main task...
   if (gpspps >= 0)
      gpio_set_direction (gpspps, GPIO_MODE_INPUT);
   if (gpsfix >= 0)
      gpio_set_direction (gpsfix, GPIO_MODE_INPUT);
   if (gpspps >= 0)
      gpio_set_direction (gpspps, GPIO_MODE_INPUT);
   if (gpsen >= 0)
   {                            // Enable
      gpio_set_level (gpsen, 1);
      gpio_set_direction (gpsen, GPIO_MODE_OUTPUT);
   }
   {
      // Init UART for GPS
      uart_config_t uart_config = {
         .baud_rate = gpsbaud,
         .data_bits = UART_DATA_8_BITS,
         .parity = UART_PARITY_DISABLE,
         .stop_bits = UART_STOP_BITS_1,
         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      };
      if ((err = uart_param_config (gpsuart, &uart_config)))
         revk_error (TAG, "UART param fail %s", esp_err_to_name (err));
      else if ((err = uart_set_pin (gpsuart, gpstx, gpsrx, -1, -1)))
         revk_error (TAG, "UART pin fail %s", esp_err_to_name (err));
      else if ((err = uart_driver_install (gpsuart, 256, 0, 0, NULL, 0)))
         revk_error (TAG, "UART install fail %s", esp_err_to_name (err));
   }
   if (attx >= 0 && atrx >= 0 && atpwr >= 0)
   {
      // Init UART for Mobile
      uart_config_t uart_config = {
         .baud_rate = atbaud,
         .data_bits = UART_DATA_8_BITS,
         .parity = UART_PARITY_DISABLE,
         .stop_bits = UART_STOP_BITS_1,
         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      };
      if ((err = uart_param_config (atuart, &uart_config)))
         revk_error (TAG, "UART param fail %s", esp_err_to_name (err));
      else if ((err = uart_set_pin (atuart, attx, atrx, -1, -1)))
         revk_error (TAG, "UART pin fail %s", esp_err_to_name (err));
      else if ((err = uart_driver_install (atuart, 256, 0, 0, NULL, 0)))
         revk_error (TAG, "UART install fail %s", esp_err_to_name (err));
      if (atkey >= 0)
         gpio_set_direction (atkey, GPIO_MODE_OUTPUT);
      if (atrst >= 0)
         gpio_set_direction (atrst, GPIO_MODE_OUTPUT);
      gpio_set_direction (atpwr, GPIO_MODE_OUTPUT);
      revk_task ("Mobile", at_task, NULL);
   }
   revk_task ("NMEA", nmea_task, NULL);
   revk_task ("Log", log_task, NULL);
   while (1)
   {                            // main task
      sleep (1);
      if (fixsave)
      {                         // Time to save a fix
         fixnow = 0;
         // Reduce fixes
         if (fixdebug)
            revk_info ("fix", "%u fixes recorded", fixsave + 1);
         unsigned int last = fixsave;
         unsigned int m = margincm;
         while (m < 100000)
         {
            unsigned int dlost,
              dkept;
            last = rdp (last, m, &dlost, &dkept);
            if (fixdebug && last < fixsave)
               revk_info ("fix", "Reduced to %u fixes at %ucm", last + 1, dlost);
            if (last <= MAXSEND)
               break;
            m = dkept + retrycm;
         }
         if (last <= MAXSEND)
         {
            // Make tracking packet
            xSemaphoreTake (track_mutex, portMAX_DELAY);
            if (tracki >= MAXTRACK && tracko < tracki + 1 - MAXTRACK)
               tracko = tracki + 1 - MAXTRACK;  // Lost as wrapped
            tracklen[tracki % MAXTRACK] = 0;    // Ensure not sent until we have put in data
            xSemaphoreGive (track_mutex);
            uint8_t *t = track[tracki],
               *p = t;
            *p++ = 0x2A;        // Version
            *p++ = revk_binid >> 16;
            *p++ = revk_binid >> 8;
            *p++ = revk_binid;
            *p++ = basetim >> 24;
            *p++ = basetim >> 16;
            *p++ = basetim >> 8;
            *p++ = basetim;
            uint8_t *e = p;
            p += 2;             // Length
            *p++ = 0;           // Message type
            *p++ = 0;           // TODO how the hell do I do change of type for oldest when signed and encrypted... Grrr
            int n;
            for (n = 1; n <= last; n++)
            {                   // Don't send first as it is duplicate of last from previous packet
               fix_t *f = &fix[n];
               unsigned int v = f->tim;
               *p++ = v >> 8;
               *p++ = v;
               v = f->alt;
               *p++ = v >> 8;
               *p++ = v;
               v = f->lat;
               *p++ = v >> 24;
               *p++ = v >> 16;
               *p++ = v >> 8;
               *p++ = v;
               v = f->lon;
               *p++ = v >> 24;
               *p++ = v >> 16;
               *p++ = v >> 8;
               *p++ = v;
            }
            e[0] = (p - t - 10) >> 8;   // Poke length
            e[1] = (p - t - 10);
            xSemaphoreTake (track_mutex, portMAX_DELAY);
            tracklen[tracki % MAXTRACK] = p - t;
            tracki++;
            xSemaphoreGive (track_mutex);
         }
         fixmove = fixsave;     // move back for next block
         fixsave = 0;
      }
   }
}
