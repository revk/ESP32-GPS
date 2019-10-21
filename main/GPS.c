// Brexit clock
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)
static const char TAG[] = "Brexit";

#include "revk.h"
#include <driver/i2c.h>
#include <math.h>
#include "oled.h"
#include "logo.h"

// Setting for "logo" is 64x48 bytes (4 bits per pixel)
// Note that MQTT config needs to allow a large enough message for the logo
#define LOGOW	128
#define	LOGOH	48

#define settings	\
	s8(oledsda,5)	\
	s8(oledscl,18)	\
	s8(oledaddress,0x3D)	\
	u8(oledcontrast,127)	\
	b(oledflip)	\
	b(f)	\
	s(deadline,CONFIG_BREXIT_DEADLINE)	\
	s(tagline,CONFIG_BREXIT_TAGLINE)	\

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
static uint8_t logo[LOGOW * LOGOH / 2];

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
      revk_register ("logo", 0, sizeof (logo), &logo, NULL, SETTING_BINARY);    // fixed logo
   {
      int p;
      for (p = 0; p < sizeof (logo) && !logo[p]; p++);
      if (p == sizeof (logo))
         memcpy (logo, brexit, sizeof (logo));  // default
   }
   if (oledsda >= 0 && oledscl >= 0)
      oled_start (1, oledaddress, oledscl, oledsda, oledflip);
   oled_icon (0, 10, logo, LOGOW, LOGOH);
   oled_text (1, 0, 0, *tagline ? tagline : "Clock not set...");
   // Main task...
   while (1)
   {
      char s[30];
      static time_t showtime = 0;
      time_t now = time (0);
      if (now != showtime)
      {
         oled_lock ();
         showtime = now;
         struct tm nowt;
         localtime_r (&showtime, &nowt);
         if (nowt.tm_year > 100)
         {
            strftime (s, sizeof (s), "%F\004%T %Z", &nowt);
            oled_text (1, 0, 0, s);
            int X,
              Y = CONFIG_OLED_HEIGHT - 1;
            if (deadline)
            {
               // Work out the time left
               int y = 0,
                  m = 0,
                  d = 0,
                  H = 0,
                  M = 0,
                  S = 0;
               sscanf (deadline, "%d-%d-%d %d:%d:%d", &y, &m, &d, &H, &M, &S);
               if (!m)
                  m = 1;
               if (!d)
                  d = 1;
               if (!y)
               {                // Regular date
                  y = nowt.tm_year + 1900;
                  if (nowt.tm_mon * 2678400 + nowt.tm_mday * 86400 + nowt.tm_hour * 3600 + nowt.tm_min * 60 + nowt.tm_sec >
                      (m - 1) * 2678400 + d * 86400 + H * 3600 + M * 60 + S)
                     y++;
               }
               // Somewhat convoluted to allow for clock changes
               int days = 0;
               {                // Work out days (using H:M:S non DST)
                struct tm target = { tm_year: y - 1900, tm_mon: m - 1, tm_mday:d };
                struct tm today = { tm_year: nowt.tm_year, tm_mon: nowt.tm_mon, tm_mday:nowt.tm_mday };
                  days = (mktime (&target) - mktime (&today)) / 86400;
                  if (nowt.tm_hour * 3600 + nowt.tm_min * 60 + nowt.tm_sec > H * 3600 + M * 60 + S)
                     days--;    // Passed current time
               }
             struct tm deadt = { tm_year: y - 1900, tm_mon: m - 1, tm_mday: d - days, tm_hour: H, tm_min: M, tm_sec: S, tm_isdst:-1 };
               int seconds = mktime (&deadt) - mktime (&nowt);
               if (days < 0)
                  days = seconds = 0;   // Deadline reached
               sprintf (s, "%4d", days);
               Y -= 5 * 7;
               X = oled_text (5, 0, Y, s);
               oled_text (1, X, Y + 3 * 9, "D");
               oled_text (1, X, Y + 2 * 9, "A");
               oled_text (1, X, Y + 1 * 9, "Y");
               oled_text (1, X, Y + 0 * 9, days == 1 ? " " : "S");
               Y -= 4 * 7 + 3;
               sprintf (s, "%02d", seconds / 3600);
               X = oled_text (4, 0, Y, s);
               sprintf (s, ":%02d", seconds / 60 % 60);
               X = oled_text (3, X, Y, s);
               sprintf (s, ":%02d", seconds % 60);
               X = oled_text (2, X, Y, s);
            }
         }
         oled_unlock ();
      }
      // Next second
      usleep (1000000LL - (esp_timer_get_time () % 1000000LL));
   }
}
