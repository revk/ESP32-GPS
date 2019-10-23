// Simple GPS
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)
static const char TAG[] = "GPS";

#include "revk.h"
#include <driver/i2c.h>
#include <driver/uart.h>
#include <math.h>
#include "oled.h"
#include "esp_sntp.h"

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
	u8(sun,0)	\
	b (mph, Y)	\

#define u32(n,d)	uint32_t n;
#define s8(n,d)	int8_t n;
#define u8(n,d)	uint8_t n;
#define b(n,d) uint8_t n;
#define s(n,d) char * n;
settings
#undef u32
#undef s8
#undef u8
#undef b
#undef s
double speed = 0;
double bearing = 0;
double lat = 0;
double lon = 0;
double alt = 0;
double gsep = 0;
double pdop = 0;
double hdop = 0;
double vdop = 0;
double course = 0;
int sats = 0;
int fix = 0;
int fixmode = 0;
int lonforce = 0;
int latforce = 0;
int altforce = 0;
int timeforce = 0;

static SemaphoreHandle_t cmd_mutex = NULL;

#include "rise.h"
#include "set.h"

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
     last_altitude,
     error;
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

//   printf("lat %lf, long %lf\n", latitude_offset * DEGS_PER_RAD, longitude_offset * DEGS_PER_RAD);

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
gpscmd (const void *s)
{                               // Send command to UART
   if (!s || *(char *) s != '$')
      return;
   uint8_t c = 0;
   for (const char *p = s + 1; *p; p++)
      c ^= *p;
   char temp[6];
   sprintf (temp, "*%02X\r\n", c);
   xSemaphoreTake (cmd_mutex, portMAX_DELAY);
   uart_write_bytes (gpsuart, s, strlen (s));
   uart_write_bytes (gpsuart, temp, 5);
   xSemaphoreGive (cmd_mutex);
   //revk_info (TAG, "Tx %s*%02X", s, c);
}

const char *
app_command (const char *tag, unsigned int len, const unsigned char *value)
{
   if (!strcmp (tag, "contrast"))
   {
      oled_set_contrast (atoi ((char *) value));
      return "";                // OK
   }
   if (!strcmp (tag, "wifi"))
   { // WiFi connected, but not need for SNTP as we have GPS
	   sntp_stop();
	   return "";
   }
   if (!strcmp (tag, "connect") || !strcmp (tag, "status"))
   {
      gpscmd ("$PMTK183");      // Log status
      // $PMTKLOG,Serial#, Type, Mode, Content, Interval, Distance, Speed, Status, Number, Percent*Checksum
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
         sscanf ((char *) value, "%d-%d-%d %d:%d:%d", &y, &m, &d, &H, &M, &S);
         t.tm_year = y - 1900;
         t.tm_mon = m - 1;
         t.tm_mday = d;
         t.tm_hour = H;
         t.tm_min = M;
         t.tm_sec = S;
         t.tm_isdst = -1;
         struct timeval v = { };
         v.tv_sec = mktime (&t);
         settimeofday (&v, NULL);
      }
      return "";
   }
   if (!strcmp (tag, "lat"))
   {
      if (!len)
         latforce = 0;
      else
      {
         latforce = 1;
         lat = strtod ((char *) value, NULL);
      }
      return "";
   }
   if (!strcmp (tag, "lon"))
   {
      if (!len)
         lonforce = 0;
      else
      {
         lonforce = 1;
         lon = strtod ((char *) value, NULL);
      }
      return "";
   }
   if (!strcmp (tag, "alt"))
   {
      if (!len)
         altforce = 0;
      else
      {
         altforce = 1;
         alt = strtod ((char *) value, NULL);
      }
      return "";
   }
   if (!strcmp (tag, "send") && len)
   {                            // Send arbitrary GPS command (do not include *XX or CR/LF)
      gpscmd (value);
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
   if (!strcmp (tag, "waas"))
   {
      gpscmd ("$PMTK301,2");    // WAAS DGPS
      return "";
   }
   if (!strcmp (tag, "sbas"))
   {
      gpscmd ("$PMTK313,1");    // SBAS (increases accuracy)
      gpscmd ("$PMTK319,1");    // SBAS integrity mode
      gpscmd ("$PMTK513,1");    // Search for SBAS sat
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
   if (!s || *s != '$' || s[1] != 'G' || s[2] != 'P')
      return;
   if (gpsdebug)
      revk_info ("Rx", "%s", s);
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
   {                            // Fix
      if (strlen (f[2]) > 2 && !latforce)
         lat = ((f[2][0] - '0') * 10 + f[2][1] - '0' + strtod (f[2] + 2, NULL) / 60) * (f[3][0] == 'N' ? 1 : -1);
      if (strlen (f[4]) > 3 && !lonforce)
         lon =
            ((f[4][0] - '0') * 100 + (f[4][1] - '0') * 10 + f[4][2] - '0' + strtod (f[4] + 3, NULL) / 60) * (f[5][0] ==
                                                                                                             'E' ? 1 : -1);
      fix = atoi (f[6]);
      sats = atoi (f[7]);
      if (!altforce)
         alt = strtod (f[9], NULL);
      gsep = strtod (f[10], NULL);
      return;
   }
   if (!strncmp (f[0], "GPRMC", 5) && n >= 12)
   {
      if (strlen (f[1]) >= 6 && strlen (f[9]) >= 6 && !timeforce)
      {
         struct tm t = { };
         t.tm_hour = (f[1][0] - '0') * 10 + f[1][1] - '0';
         t.tm_min = (f[1][2] - '0') * 10 + f[1][3] - '0';
         t.tm_sec = (f[1][4] - '0') * 10 + f[1][5] - '0';
         t.tm_year = 100 + (f[9][4] - '0') * 10 + f[9][5] - '0';
         t.tm_mon = (f[9][2] - '0') * 10 + f[9][3] - '0' - 1;
         t.tm_mday = (f[9][0] - '0') * 10 + f[9][1] - '0';
         struct timeval v = { };
         if (f[1][6] == '.')
            v.tv_usec = atoi (f[1] + 7) * 1000;
         v.tv_sec = mktime (&t);
         settimeofday (&v, NULL);
      }
      return;
   }
   if (!strncmp (f[0], "GPVTG", 5) && n >= 10)
   {
      course = strtod (f[1], NULL);
      speed = strtod (f[7], NULL);
      return;
   }
   if (!strncmp (f[0], "GPGSA", 5) && n >= 18)
   {
      fixmode = atoi (f[2]);
      pdop = strtod (f[15], NULL);
      hdop = strtod (f[16], NULL);
      vdop = strtod (f[17], NULL);
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
      char temp[100];
      int y = CONFIG_OLED_HEIGHT,
         x = 0;
      time_t now = time (0) + 1;
      struct tm t;
      localtime_r (&now, &t);
      if (t.tm_year > 100)
      {
         strftime (temp, sizeof (temp), "%F\004%T %Z", &t);
         oled_text (1, 0, 0, temp);
      }
      y -= 10;
      sprintf (temp, "Fix:%s %2d sat%s %4s", fixmode == 3 ? "3D" : fixmode == 2 ? "2D" : "  ",
               sats, sats == 1 ? " " : "s", fix == 2 ? "Diff" : fix == 1 ? "GPS" : "None");
      oled_text (1, 0, y, temp);
      y -= 3;                   // Line
      y -= 8;
      if (fixmode > 1)
         sprintf (temp, "Lat: %11.6lf   DOP", lat);
      else
         sprintf (temp, "%21s", "");
      oled_text (1, 0, y, temp);
      y -= 8;
      if (fixmode > 1)
         sprintf (temp, "Lon: %11.6lf %5.1fm", lon, hdop);
      else
         sprintf (temp, "%21s", "");
      oled_text (1, 0, y, temp);
      y -= 8;
      if (fixmode >= 3)
         sprintf (temp, "Alt: %6.1lfm     %5.1fm", alt, vdop);
      else
         sprintf (temp, "%21s", "");
      oled_text (1, 0, y, temp);
      y -= 3;                   // Line
      // Sun
      y -= 22;
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
      if (rise < set)
      {
         oled_icon (0, y, riseicon, 22, 21);
         o = rise - now;
      } else
      {
         oled_icon (0, y, seticon, 22, 21);
         o = set - now;
      }
      x = 22;
      if(fixmode<=1)
      {
         x = oled_text (3, x, y, "   ");
         x = oled_text (2, x, y, "   ");
         x = oled_text (1, x, y, "   ");
      }
      else
      if (o / 3600 < 1000)
      {                         // H:MM:SS
         sprintf (temp, "%3d", o / 3600);
         x = oled_text (3, x, y, temp);
         sprintf (temp, ":%02d", o / 60 % 60);
         x = oled_text (2, x, y, temp);
         sprintf (temp, ":%02d", o % 60);
         x = oled_text (1, x, y, temp);
      } else
      {                         // D HH:MM
         sprintf (temp, "%3d", o / 86400);
         x = oled_text (3, x, y, temp);
         sprintf (temp, "\002%02d", o / 3600 % 24);
         x = oled_text (2, x, y, temp);
         sprintf (temp, ":%02d", o / 60 % 60);
         x = oled_text (1, x, y, temp);
      }
      y -= 3;                   // Line
#if 0 // Show time
      y -= 8;
      localtime_r (&set, &t);
      strftime (temp, sizeof (temp), "%F\004%T %Z", &t);
      oled_text (1, 0, y, temp);
#endif
      // Speed
      double s = speed;
      double minspeed = hdop * 2;       // Use as basis for ignoring spurious speeds
      if (mph)
         s /= 1.609344;         // miles
      if (speed >= 999)
         strcpy (temp, "\002---");
      else if (speed >= 99.9)
         sprintf (temp, "\002%3.0lf", s);
      else if (hdop && speed > minspeed)
         sprintf (temp, "%4.1lf", s);
      else
         strcpy (temp, " 0.0");
      x = oled_text (5, 0, 13, temp);
      oled_text (-1, x, 13, mph ? "mph" : "km/h");
      if (hdop && speed > minspeed && speed <= 99.9)
         sprintf (temp, "%3.0f", course);
      else
         strcpy (temp, "---");
      x = oled_text (-1, x, 13 + 8, temp);
      oled_text (0, x, 13 + 8 + 5, "o");
      oled_unlock ();
   }
}

void
app_main ()
{
   cmd_mutex = xSemaphoreCreateMutex ();        // Shared command access
   revk_init (&app_command);
#define b(n,d) revk_register(#n,0,sizeof(n),&n,#d,SETTING_BOOLEAN);
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
   oled_set_contrast (oledcontrast);
   for (int x = 0; x < CONFIG_OLED_WIDTH; x++)
   {
      oled_pixel (x, CONFIG_OLED_HEIGHT - 12, 4);
      oled_pixel (x, CONFIG_OLED_HEIGHT - 12 - 3 - 24, 4);
      oled_pixel (x, CONFIG_OLED_HEIGHT - 12 - 3 - 24 - 3 - 22, 4);
      oled_pixel (x, 8, 4);
   }
   // Init UART
   uart_config_t uart_config = {
      .baud_rate = gpsbaud,     // $PMT251,baud to change rate used
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
   else if ((err = uart_driver_install (gpsuart, 256, 0, 0, NULL, 0)))
      revk_error (TAG, "UART install fail %s", esp_err_to_name (err));
   else
      revk_info (TAG, "PN532 UART %d Tx %d Rx %d", gpsuart, gpstx, gpsrx);
   if (gpsen >= 0)
   {                            // Enable
      gpio_set_level (gpsen, 1);
      gpio_set_direction (gpsen, GPIO_MODE_OUTPUT);
   }
   if (gpspps >= 0)
      gpio_set_direction (gpspps, GPIO_MODE_INPUT);
   if (gpsfix >= 0)
      gpio_set_direction (gpsfix, GPIO_MODE_INPUT);
   revk_task ("Display", display_task, NULL);
   // Main task...
   uint8_t buf[1000],
    *p = buf;
   int started = 0;
   while (1)
   {
      // Get line(s), the timeout should mean we see one or more whole lines typically
      int l = uart_read_bytes (gpsuart, p, buf + sizeof (buf) - p, 10);
      if (l < 0)
         sleep (1);
      if (l <= 0)
         continue;
      if (!started)
      {
         gpscmd ("$PMTK185,0"); // Start log
         started = 1;
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
               revk_error (TAG, "[%.*s] (%02X)", l - p, p, c);
            else
            {                   // Process line
               l[-3] = 0;
               if (p[1] == 'G')
                  nmea ((char *) p);
               else
                  revk_info ("Rx", "%s", p);    // Other packet
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
