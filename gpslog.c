// GPS dump - when a GPS unit reports it has log data, this initiates a dump and stored to database
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <popt.h>
#include <err.h>
#include <malloc.h>
#include <time.h>
#include <sqllib.h>
#include <mosquitto.h>
#include <math.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include "main/revkgps.h"
#include "ostn02.h"

#define WGS84_A 6378137.0
#define WGS84_IF 298.257223563
#define WGS84_F (1 / WGS84_IF)
#define WGS84_B (WGS84_A * (1 - WGS84_F))
#define WGS84_E (sqrtl(2 * WGS84_F - WGS84_F * WGS84_F))

void
wgsecef2llh (const long double ecef[3], long double llh[3])
{
   /* Distance from polar axis. */
   const long double p = sqrtl (ecef[0] * ecef[0] + ecef[1] * ecef[1]);

   /* Compute longitude first, this can be done exactly. */
   if (p != 0)
   {
      llh[1] = atan2l (ecef[1], ecef[0]);
   } else
   {
      llh[1] = 0;
   }

   /* If we are close to the pole then convergence is very slow, treat this is a
    * special case. */
   if (p < WGS84_A * 1e-16)
   {
      llh[0] = copysign (M_PI_2l, ecef[2]);
      llh[2] = fabsl (ecef[2]) - WGS84_B;
      return;
   }

   /* Caluclate some other constants as defined in the Fukushima paper. */
   const long double P = p / WGS84_A;
   const long double e_c = sqrtl (1. - WGS84_E * WGS84_E);
   const long double Z = fabsl (ecef[2]) * e_c / WGS84_A;

   /* Initial values for S and C correspond to a zero height solution. */
   long double S = Z;
   long double C = e_c * P;

   /* Neither S nor C can be negative on the first iteration so
    * starting prev = -1 will not cause and early exit. */
   long double prev_C = -1;
   long double prev_S = -1;

   long double A_n,
     B_n,
     D_n,
     F_n;

   /* Iterate a maximum of 10 times. This should be way more than enough for all
    * sane inputs */
   for (int i = 0; i < 10; i++)
   {
      /* Calculate some intermediate variables used in the update step based on
       * the current state. */
      A_n = sqrtl (S * S + C * C);
      D_n = Z * A_n * A_n * A_n + WGS84_E * WGS84_E * S * S * S;
      F_n = P * A_n * A_n * A_n - WGS84_E * WGS84_E * C * C * C;
      B_n = 1.5 * WGS84_E * S * C * C * (A_n * (P * S - Z * C) - WGS84_E * S * C);

      /* Update step. */
      S = D_n * F_n - B_n * S;
      C = F_n * F_n - B_n * C;

      /* The original algorithm as presented in the paper by Fukushima has a
       * problem with numerical stability. S and C can grow very large or small
       * and over or underflow a double. In the paper this is acknowledged and
       * the proposed resolution is to non-dimensionalise the equations for S and
       * C. However, this does not completely solve the problem. The author caps
       * the solution to only a couple of iterations and in this period over or
       * underflow is unlikely but as we require a bit more precision and hence
       * more iterations so this is still a concern for us.
       *
       * As the only thing that is important is the ratio T = S/C, my solution is
       * to divide both S and C by either S or C. The scaling is chosen such that
       * one of S or C is scaled to unity whilst the other is scaled to a value
       * less than one. By dividing by the larger of S or C we ensure that we do
       * not divide by zero as only one of S or C should ever be zero.
       *
       * This incurs an extra division each iteration which the author was
       * explicityl trying to avoid and it may be that this solution is just
       * reverting back to the method of iterating on T directly, perhaps this
       * bears more thought?
       */

      if (S > C)
      {
         C = C / S;
         S = 1;
      } else
      {
         S = S / C;
         C = 1;
      }

      /* Check for convergence and exit early if we have converged. */
      if (fabsl (S - prev_S) < 1e-16 && fabsl (C - prev_C) < 1e-16)
      {
         break;
      }
      prev_S = S;
      prev_C = C;
   }

   A_n = sqrtl (S * S + C * C);
   llh[0] = copysign (1.0, ecef[2]) * atanl (S / (e_c * C));
   llh[2] = (p * e_c * C + fabsl (ecef[2]) * S - WGS84_A * e_c * A_n) / sqrtl (e_c * e_c * C * C + S * S);
   //fprintf (stderr, "x=%Lf y=%Lf z=%Lf lat=%Lf (%Lf) lon=%Lf (%Lf) alt=%Lf\n", ecef[0], ecef[1], ecef[2], llh[0], llh[0] * 180.0L / M_PIl, llh[1], llh[1] * 180.0L / M_PIl, llh[2]);
}


typedef struct device_s device_t;
struct device_s
{
   device_t *next;
   char *tag;
   char *iccid;
   char *imei;
   char *version;
   char online:1;
   int lines;
   int line;
   int len;
   int ptr;
   unsigned char *data;
};
device_t *devices = NULL;

int debug = 0;
const char *sqlhostname = NULL;
const char *sqldatabase = "gps";
const char *sqlusername = NULL;
const char *sqlpassword = NULL;
const char *sqlconffile = NULL;
const char *sqlgps = "gps";
const char *sqldevice = "device";
const char *sqllog = "log";
const char *sqlauth = "auth";

time_t
process_udp (SQL * sqlp, unsigned int len, unsigned char *data, const char *addr, unsigned short port)
{
   time_t resend = 0;
   if (len == 1 && *data == VERSION)
   {
      if (addr)
         sql_safe_query_free (sqlp, sql_printf ("UPDATE `%#S` SET `lastip`=now() WHERE `ip`=%#s AND `port`=%u", addr, port));
      return resend;            // keep alive
   }
   if (len < HEADLEN + 16 + MACLEN)
   {
      warnx ("Bad UDP len %d", len);
      return resend;
   }
   if (*data != VERSION)
   {
      warnx ("Bad version %02X", *data);
      return resend;
   }
   unsigned int id = (data[1] << 16) + (data[2] << 8) + data[3];
   time_t t = (data[4] << 24) + (data[5] << 16) + (data[6] << 8) + (data[7]);
   SQL_RES *auth = sql_safe_query_store_free (sqlp, sql_printf ("SELECT * from `%#S` WHERE `id`=%u", sqlauth, id));
   SQL_RES *device = NULL;
   unsigned int devid = 0;
   const char *process (void)
   {                            // Simplify error checking and ensure free
      if (!sql_fetch_row (auth))
         return "Unknown auth";
      devid = atoi (sql_colz (auth, "device"));
      if (!devid)
         return "Missing device";
      device = sql_safe_query_store_free (sqlp, sql_printf ("SELECT * FROM `%#S` WHERE `ID`=%u", sqldevice, devid));
      if (!sql_fetch_row (device))
         return "Missing device record";
      char *login = sql_col (auth, "auth");
      if (!login)
         return "No auth in database";
      if (debug)
      {
         fprintf (stderr, "Data(%u):", len);
         for (int n = 0; n < len; n++)
            fprintf (stderr, " %02X", data[n]);
         fprintf (stderr, "\n");
      }
      len = HEADLEN + MACLEN + ((len - HEADLEN - MACLEN) / 16 * 16);    // Lose trailing padding
      char badmac = 0;
      {                         // HMAC
         len -= MACLEN;         // Remove HMAC
         int keylen = strlen (login) / 2;
         char key[keylen];
         for (int n = 0; n < keylen; n++)
            key[n] =
               (((isalpha (login[n * 2]) ? 9 : 0) + (login[n * 2] & 0xF)) << 4) + (isalpha (login[n * 2 + 1]) ? 9 : 0) +
               (login[n * 2 + 1] & 0xF);
         unsigned char mac[32] = { };
         if (!HMAC (EVP_sha256 (), key, keylen, data, len, mac, NULL))
            return "MAC calc fail";
         if (memcmp (mac, data + len, MACLEN))
         {
            if (debug)
            {
               fprintf (stderr, "MAC:");
               for (int n = 0; n < MACLEN; n++)
                  fprintf (stderr, " %02X", mac[n]);
               fprintf (stderr, "\nMAC:");
               for (int n = 0; n < MACLEN; n++)
                  fprintf (stderr, " %02X", data[len + n]);
               fprintf (stderr, "\n");
            }
            badmac = 1;
         }
      }
      // Decrypt
      char *aes = sql_colz (auth, "aes");
      if (strlen (aes) < 32)
         return "Bad AES in database";
      unsigned char key[16],
        iv[16] = { };
      int n;
      for (n = 0; n < sizeof (key); n++)
         key[n] =
            (((isalpha (aes[n * 2]) ? 9 : 0) + (aes[n * 2] & 0xF)) << 4) + (isalpha (aes[n * 2 + 1]) ? 9 : 0) +
            (aes[n * 2 + 1] & 0xF);
      memcpy (iv, data, HEADLEN > sizeof (iv) ? sizeof (iv) : HEADLEN);
      EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new ();
      if (!ctx)
         return "Cannot make ctx";
      if (EVP_DecryptInit_ex (ctx, EVP_aes_128_cbc (), NULL, key, iv) != 1)
         return "Decrypt error (int)";
      EVP_CIPHER_CTX_set_padding (ctx, 0);
      if (EVP_DecryptUpdate (ctx, data + HEADLEN, &n, data + HEADLEN, len - HEADLEN) != 1)
         return "Decrypt error (update)";
      if (EVP_DecryptFinal_ex (ctx, data + HEADLEN + n, &n) != 1)
         return "Decrypt error (final)";
      if (debug)
      {
         fprintf (stderr, "Data(%u):", len);
         for (n = 0; n < len; n++)
            fprintf (stderr, " %02X", data[n]);
         fprintf (stderr, "\n");
      }
      if (badmac)
         return "Bad MAC";
      unsigned char *p = data + 8;
      unsigned char *e = data + len;
      int period = 0;
      int expected = 0;
      time_t last = sql_time_utc (sql_colz (device, "lastupdateutc"));
      resend = 0;
      unsigned int lastupdate = 0;
      int margin = -1;
      unsigned int fixes = 0;
      float ascale = 1.0 / ASCALE;
      float tempc = -999;
      sql_transaction (sqlp);
      char ecef = 0;
      int64_t rx = 0,
         ry = 0,
         rz = 0;
      while (p < e && !(*p & TAGF_FIX))
      {                         // Process tags
         unsigned int dlen = 0;
         if (*p < 0x20)
            dlen = 0;
         else if (*p < 0x40)
            dlen = 1;
         else if (*p < 0x60)
            dlen = 2;
         else if (*p < 0x70)
            dlen = 4;
         else
            dlen = 1 + p[1];
         if (*p == TAGF_FIRST)
         {                      // New first data
            unsigned int first = (p[1] << 24) + (p[2] << 16) + (p[3] << 8) + p[4];      // New base
            if (first > last)
            {
               last = lastupdate = first;
               if (debug)
                  fprintf (stderr, "Restarted from %u\n", (unsigned int) last);
            }
         } else if (*p == TAGF_PERIOD)
            period = (p[1] << 8) + p[2];
         else if (*p == TAGF_EXPECTED)
            expected = (p[1] << 8) + p[2];
         else if (*p == TAGF_BALLOON)
            ascale = ALT_BALLOON;
         else if (*p == TAGF_FLIGHT)
            ascale = ALT_FLIGHT;
         else if (*p == TAGF_MARGIN)
            margin = (p[1] << 8) + p[2];
         else if (*p == TAGF_TEMPC)
            tempc = (float) ((signed char *) p)[1] / CSCALE;
         else if (*p == TAGF_ECEF && p[1] == 9)
         {
            rx = 1000000LL * (((int32_t) ((p[2] << 24) | (p[3] << 16) | (p[4] << 8))) >> 8);
            ry = 1000000LL * (((int32_t) ((p[5] << 24) | (p[6] << 16) | (p[7] << 8))) >> 8);
            rz = 1000000LL * (((int32_t) ((p[8] << 24) | (p[9] << 16) | (p[10] << 8))) >> 8);
            ecef = 1;
         } else if (*p == TAGF_INFO && p[1] > 1)
         {                      // Info
            char *tag = (char *) p + 2;
            char *e = tag + p[1];
            char *q = tag;
            while (q < e && *q)
               q++;
            if (q < e)
            {
               q++;
               if (!strcmp (tag, "ICCID"))
               {
                  char *iccid = sql_colz (device, "iccid");
                  if (strlen (iccid) != e - q || memcmp (iccid, q, e - q))
                     sql_safe_query_free (sqlp,
                                          sql_printf ("UPDATE `%#S` SET `iccid`=%#.*s WHERE `ID`=%d", sqldevice, (int) (e - q), q,
                                                      devid));
               } else if (!strcmp (tag, "IMEI"))
               {
                  char *imei = sql_colz (device, "imei");
                  if (strlen (imei) != e - q || memcmp (imei, q, e - q))
                     sql_safe_query_free (sqlp,
                                          sql_printf
                                          ("UPDATE `%#S` SET `imei`=%#.*s WHERE `ID`=%d", sqldevice, (int) (e - q), q, devid));
               } else
                  warnx ("Unknown info %s", tag);
            }
         } else if (*p && debug)
            fprintf (stderr, "Unknown tag %02X\n", *p);
         p += 1 + dlen;
      }
      if (debug)
         fprintf (stderr, "Track %u->%u (%u)\n", (unsigned int) t, (unsigned int) t + period, period);
      if (t > last)
      {
         if (period && t + period < time (0) + 10)
         {                      // Security to ignore invalid message
            if (debug)
               fprintf (stderr, "Missing %u seconds, resend\n", (unsigned int) (t - last));
            resend = (last ? : 1);      // Missing data
         }
      } else
      {                         // Process
         sql_string_t s = { };
         if (!lastupdate)
            lastupdate = t + period;
         if (lastupdate < last)
            lastupdate = last;
         long double lat = 0,
            lon = 0,
            alt = 0,
            E = -1,
            N = -1,
            H = -1;
	 unsigned int hepe=0;
         sql_sprintf (&s, "UPDATE `%#S` SET `lastupdateutc`=%#U", sqldevice, lastupdate);
         if (p + 2 <= e && (*p & TAGF_FIX))
         {                      // We have fixes
            unsigned char fixtags = *p++;
            unsigned char fixlen = *p++;
            if (p < e && fixlen)
            {                   // Fixes
               int lastfix = -1;
               while (p + fixlen <= e)
               {
                  unsigned char *q = p;
                  unsigned short tim = (q[0] << 8) + q[1];
                  q += 2;
                  sql_string_t f = { };
                  sql_sprintf (&f,
                               "REPLACE INTO `%#S` SET `device`=%u,`utc`='%U."
                               TPART "'", sqlgps, devid, t + (tim / TSCALE), tim % TSCALE);
                  if (tim > lastfix)
                     lastfix = tim;
                  if (ecef)
                  {
                     int32_t dx = (q[0] << 24) | (q[1] << 16) | (q[2] << 8) | q[3];
                     q += 4;
                     int32_t dy = (q[0] << 24) | (q[1] << 16) | (q[2] << 8) | q[3];
                     q += 4;
                     int32_t dz = (q[0] << 24) | (q[1] << 16) | (q[2] << 8) | q[3];
                     q += 4;
                     //fprintf (stderr, "dx=%d dy=%d dz=%d rx=%lld ry=%lld rz=%lld\n", dx, dy, dz, rx, ry, rz);
                     rx += dx;
                     ry += dy;
                     rz += dz;
                     int64_t v = rx;
                     sql_sprintf (&f, ",`ecefx`=");
                     if (v < 0)
                     {
                        v = 0 - v;
                        sql_sprintf (&f, "-");
                     }
                     sql_sprintf (&f, "%lld.%06lld", v / 1000000LL, v % 1000000LL);
                     v = ry;
                     sql_sprintf (&f, ",`ecefy`=");
                     if (v < 0)
                     {
                        v = 0 - v;
                        sql_sprintf (&f, "-");
                     }
                     sql_sprintf (&f, "%lld.%06lld", v / 1000000LL, v % 1000000LL);
                     v = rz;
                     sql_sprintf (&f, ",`ecefz`=");
                     if (v < 0)
                     {
                        v = 0 - v;
                        sql_sprintf (&f, "-");
                     }
                     sql_sprintf (&f, "%lld.%06lld", v / 1000000LL, v % 1000000LL);
                     long double ecef[3] = { (long double) rx / 1000000.0,
                        (long double) ry / 1000000.0,
                        (long double) rz / 1000000.0
                     };
                     long double llh[3] = { };
                     wgsecef2llh (ecef, llh);
                     sql_sprintf (&f, ",`lat`=%.9Lf", lat = llh[0] * 180.0 / M_PIl);
                     sql_sprintf (&f, ",`lon`=%.9Lf", lon = llh[1] * 180.0 / M_PIl);
                     sql_sprintf (&f, ",`alt`=%.9Lf", alt = llh[2]);
                     if (OSTN02_LL2EN (lat, lon, &E, &N, &H) <= 0)
                        E = 0;
                     else
                        sql_sprintf (&f, ",`E`=%.9Lf,`N`=%.9Lf", E, N);
                  } else
                  {
                     lat = (q[0] << 24) + (q[1] << 16) + (q[2] << 8) + q[3];
                     q += 4;
                     lon = (q[0] << 24) + (q[1] << 16) + (q[2] << 8) + q[3];
                     q += 4;
                     sql_sprintf (&f, ",`lat`=%.9Lf,`lon`=%.9Lf", (double) lat / 60.0 / DSCALE, (double) lon / 60.0 / DSCALE);
                     if (OSTN02_LL2EN (lat, lon, &E, &N, &H) <= 0)
                        E = -1;
                     else
                        sql_sprintf (&f, ",`E`=%.9Lf,`N`=%.9Lf", E, N);
                  }
                  if (fixtags & TAGF_FIX_ALT)
                  {
                     short a = (q[0] << 8) + q[1];
                     if (a != -32768 && a != 32767)
                     {
                        alt = (float) a *ascale;
                        q += 2;
                        sql_sprintf (&f, ",`alt`=%.1f", alt);
                     }
                  }
                  if (fixtags & TAGF_FIX_SATS)
                  {
                     int s = (*q & TAGF_FIX_STATS_MASK);
                     if (s)
                        sql_sprintf (&f, ",`sats`=%u", s);
                     q++;
                  }
                  if (fixtags & TAGF_FIX_HEPE)
                  {
                     hepe = *q;
                     if (hepe)
                        sql_sprintf (&f, ",`hepe`=%u." EPART, hepe / ESCALE, hepe % ESCALE);
                     q++;
                  }
                  sql_safe_query_s (sqlp, &f);
                  p += fixlen;
                  fixes++;
               }
               if (lastfix >= 0)
               {
                  sql_sprintf (&s, ",`lastfixutc`='%U." TPART "'", t + (lastfix / TSCALE), lastfix % TSCALE);
                  if (ecef)
                  {
                     long long v = rx;
                     sql_sprintf (&s, ",`ecefx`=");
                     if (v < 0)
                     {
                        sql_sprintf (&s, "-");
                        v = 0 - v;
                     }
                     sql_sprintf (&s, "%lld.%06lld", v / 1000000LL, v % 1000000LL);
                     v = ry;
                     sql_sprintf (&s, ",`ecefy`=");
                     if (v < 0)
                     {
                        sql_sprintf (&s, "-");
                        v = 0 - v;
                     }
                     sql_sprintf (&s, "%lld.%06lld", v / 1000000LL, v % 1000000LL);
                     v = rz;
                     sql_sprintf (&s, ",`ecefz`=");
                     if (v < 0)
                     {
                        sql_sprintf (&s, "-");
                        v = 0 - v;
                     }
                     sql_sprintf (&s, "%lld.%06lld", v / 1000000LL, v % 1000000LL);
                  }
                  sql_sprintf (&s, ",`lat`=%.9Lf", lat);
                  sql_sprintf (&s, ",`lon`=%.9Lf", lon);
                  sql_sprintf (&s, ",`alt`=%.9Lf", alt);
                  if (E >= 0)
                     sql_sprintf (&s, ",`E`=%.9Lf,`N`=%.9Lf", E, N);
		  if(hepe)
                        sql_sprintf (&s, ",`hepe`=%u." EPART, hepe / ESCALE, hepe % ESCALE);
               }
            }
         }
         if (addr)
            sql_sprintf (&s, ",`ip`=%#s,`port`=%u,`lastip`=now()", addr, port);
         else if (sql_time (sql_colz (device, "lastip")) < time (0) - 3600)
            sql_sprintf (&s, ",`ip`=NULL,`port`=NULL,`lastip`=NULL");
         if (atoi (sql_colz (device, "auth")) != id)
            sql_sprintf (&s, ",`auth`=%u", id);
         if (expected > 0)
            sql_sprintf (&s, ",`nextupdateutc`=%#U", t + expected);
         sql_sprintf (&s, " WHERE `ID`=%u", devid);
         // Log
         sql_safe_query_s (sqlp, &s);
         sql_sprintf (&s, "INSERT INTO `%#S` SET `device`=%u,`utc`=%#T,`received`=NOW(),`fixes`=%u", sqllog, devid, t, fixes);
         if (period > 0)
            sql_sprintf (&s, ",`endutc`=%#T", t + period);
         if (addr)
            sql_sprintf (&s, ",`ip`=%#s,`port`=%u", addr, port);
         if (margin >= 0 && margin < 65536)
            sql_sprintf (&s, ",`margin`=%u." MPART, margin / MSCALE, margin % MSCALE);
         if (tempc > -999)
            sql_sprintf (&s, ",`tempc`=%.1f", tempc);
         sql_safe_query_s (sqlp, &s);
         if (sql_col (auth, "replaces"))
         {                      // New key in use, delete old
            sql_safe_query_free (sqlp, sql_printf ("UPDATE `%#S` SET `replaces`=NULL WHERE `ID`=%u", sqlauth, id));
            sql_safe_query_free (sqlp, sql_printf ("DELETE FROM `%#S` WHERE `ID`=%#s", sqlauth, sql_col (auth, "replaces")));
         }
         if (sql_commit (sqlp))
            return "Bad SQL commit";
      }
      return NULL;
   }
   const char *e = process ();
   if (e)
      warnx ("%u: %s", id, e);  // Error
   if (device)
      sql_free_result (device);
   sql_free_result (auth);
   return resend;
}

unsigned int
encode (SQL * sqlp, unsigned char *buf, unsigned int len)
{
   while ((len & 0xF) != HEADLEN)
      buf[len++] = 0;           // Pad
   unsigned int authid = (buf[1] << 16) + (buf[2] << 8) + buf[3];
   SQL_RES *res = sql_safe_query_store_free (sqlp,
                                             sql_printf ("SELECT * from `%#S` WHERE `ID`=%u",
                                                         sqlauth,
                                                         authid));
   if (sql_fetch_row (res))
   {
      // Encryption
      char *aes = sql_colz (res, "aes");
      if (strlen (aes) >= 32)
      {
         unsigned char key[16],
           iv[16] = { };
         int n;
         for (n = 0; n < sizeof (key); n++)
            key[n] =
               (((isalpha (aes[n * 2]) ? 9 : 0) +
                 (aes[n * 2] & 0xF)) << 4) + (isalpha (aes[n * 2 + 1]) ? 9 : 0) + (aes[n * 2 + 1] & 0xF);
         memcpy (iv, buf, HEADLEN > sizeof (iv) ? sizeof (iv) : HEADLEN);
         EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new ();
         EVP_EncryptInit_ex (ctx, EVP_aes_128_cbc (), NULL, key, iv);
         EVP_CIPHER_CTX_set_padding (ctx, 0);
         EVP_EncryptUpdate (ctx, buf + HEADLEN, &n, buf + HEADLEN, len - HEADLEN);
         EVP_EncryptFinal_ex (ctx, buf + HEADLEN + n, &n);
      }
      // HMAC
      char *login = sql_col (res, "auth");
      if (login && *login)
      {
         int keylen = strlen (login) / 2;
         char key[keylen];
         for (int n = 0; n < keylen; n++)
            key[n] =
               (((isalpha (login[n * 2]) ? 9 : 0) +
                 (login[n * 2] & 0xF)) << 4) + (isalpha (login[n * 2 + 1]) ? 9 : 0) + (login[n * 2 + 1] & 0xF);
         unsigned char mac[32] = { };
         HMAC (EVP_sha256 (), key, keylen, buf, len, mac, NULL);
         memcpy (buf + len, mac, MACLEN);
         len += MACLEN;
      }
   }
   sql_free_result (res);
   return len;
}

const char *bindhost = NULL;
const char *bindport = NULL;
int
udp_task (void)
{
   int s = -1;
 const struct addrinfo hints = { ai_flags: AI_PASSIVE, ai_socktype: SOCK_DGRAM, ai_family:AF_INET6
   };
   struct addrinfo *res;
   if (getaddrinfo (bindhost, bindport, &hints, &res))
      err (1, "getaddrinfo");
   if (!res)
      errx (1, "Cannot find bind address");
   s = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
   if (s < 0)
      err (1, "socket");
   int on = 1;
   if (setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof (on)))
      err (1, "setsockopt");
   if (bind (s, res->ai_addr, res->ai_addrlen))
      err (1, "bind");
   freeaddrinfo (res);
   SQL sql;
   sql_real_connect (&sql, sqlhostname, sqlusername, sqlpassword, sqldatabase, 0, NULL, 0, 1, sqlconffile);
   while (1)
   {
      unsigned char rx[2000];
      struct sockaddr_in6 from;
      socklen_t fromlen = sizeof (from);
      size_t len = recvfrom (s, rx, sizeof (rx) - 1, 0,
                             (struct sockaddr *) &from,
                             &fromlen);
      if (len < 0)
         return -1;
      unsigned short port = 0;
      char addr[INET6_ADDRSTRLEN + 1] = "";
      if (from.sin6_family == AF_INET)
      {
         inet_ntop (from.sin6_family, &((struct sockaddr_in *) &from)->sin_addr, addr, sizeof (addr));
         port = ((struct sockaddr_in *) &from)->sin_port;
      } else
      {
         inet_ntop (from.sin6_family, &from.sin6_addr, addr, sizeof (addr));
         port = from.sin6_port;
      }
      if (!strncmp (addr, "::ffff:", 7) && strchr (addr, '.'))
         strcpy (addr, addr + 7);
      time_t resend = process_udp (&sql, len, rx, addr, port);
      if (resend)
      {                         // TODO request resend
         unsigned char buf[100],
          *p = buf;
         *p++ = VERSION;
         *p++ = rx[1];          // ID
         *p++ = rx[2];
         *p++ = rx[3];
         time_t now = time (0);
         *p++ = now >> 24;
         *p++ = now >> 16;
         *p++ = now >> 8;
         *p++ = now;
         *p++ = TAGT_RESEND;    // resend
         *p++ = resend >> 24;   // resend reference
         *p++ = resend >> 16;
         *p++ = resend >> 8;
         *p++ = resend;
         sendto (s, buf, encode (&sql, buf, p - buf), 0, (struct sockaddr *) &from, fromlen);
      }
   }
}

int
main (int argc, const char *argv[])
{
   const char *mqtthostname = "localhost";
   const char *mqttusername = NULL;
   const char *mqttpassword = NULL;
   const char *mqttappname = "GPS";
   const char *mqttid = NULL;
   int save = 0;
   int resend = 0;
   int locus = 0;               // Flash log download
   {                            // POPT
      poptContext optCon;       // context for parsing command-line options
      const struct poptOption optionsTable[] = {
         {"sql-conffile", 'c', POPT_ARG_STRING,
          &sqlconffile, 0, "SQL conf file",
          "filename"},
         {"sql-hostname", 'H', POPT_ARG_STRING,
          &sqlhostname, 0, "SQL hostname",
          "hostname"},
         {"sql-database", 'd',
          POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
          &sqldatabase, 0, "SQL database", "db"},
         {"sql-username", 'U', POPT_ARG_STRING,
          &sqlusername, 0, "SQL username",
          "name"},
         {"sql-password", 'P', POPT_ARG_STRING,
          &sqlpassword, 0, "SQL password",
          "pass"},
         {"sql-table", 't',
          POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqlgps, 0,
          "SQL log table", "table"},
         {"sql-device", 0,
          POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqldevice,
          0, "SQL device table", "table"},
         {"sql-debug", 'v', POPT_ARG_NONE,
          &sqldebug, 0, "SQL Debug"},
         {"mqtt-hostname", 'h',
          POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
          &mqtthostname, 0, "MQTT hostname",
          "hostname"},
         {"mqtt-username", 'u', POPT_ARG_STRING,
          &mqttusername, 0, "MQTT username",
          "username"},
         {"mqtt-password", 'p', POPT_ARG_STRING,
          &mqttpassword, 0, "MQTT password",
          "password"},
         {"mqtt-appname", 'a',
          POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
          &mqttappname, 0, "MQTT appname",
          "appname"},
         {"mqtt-id", 0, POPT_ARG_STRING, &mqttid,
          0, "MQTT id", "id"},
         {"locus", 'L', POPT_ARG_NONE, &locus, 0,
          "Get LOCUS file"},
         {"resend", 0, POPT_ARG_NONE, &resend, 0,
          "Ask resend of tracking over MQTT on connect"},
         {"save", 0, POPT_ARG_NONE, &save, 0,
          "Save LOCUS file"},
         {"port", 0, POPT_ARG_STRING, &bindport,
          0,
          "UDP port to bind for collecting tracking"},
         {"bindhost", 0, POPT_ARG_STRING,
          &bindhost, 0,
          "UDP host to bind for collecting tracking"},
         {"debug", 'V', POPT_ARG_NONE, &debug, 0,
          "Debug"},
         POPT_AUTOHELP {}
      };
      optCon = poptGetContext (NULL, argc, argv, optionsTable, 0);
      int c;
      if ((c = poptGetNextOpt (optCon)) < -1)
         errx (1, "%s: %s\n", poptBadOption (optCon, POPT_BADOPTION_NOALIAS), poptStrerror (c));
      if (poptPeekArg (optCon))
      {
         poptPrintUsage (optCon, stderr, 0);
         return -1;
      }
      poptFreeContext (optCon);
   }
   if (bindport)
   {                            // UDP tracking
      pid_t child = fork ();
      if (!child)
         return udp_task ();
   }
   SQL sql;
   int e = mosquitto_lib_init ();
   if (e)
      errx (1, "MQTT init failed %s", mosquitto_strerror (e));
   struct mosquitto *mqtt = mosquitto_new (mqttid, 1, NULL);
   if (mqttusername)
   {
      e = mosquitto_username_pw_set (mqtt, mqttusername, mqttpassword);
      if (e)
         errx (1, "MQTT auth failed %s", mosquitto_strerror (e));
   }
   void connect (struct mosquitto *mqtt, void *obj, int rc)
   {
      obj = obj;
      rc = rc;
      char *sub = NULL;
      asprintf (&sub, "state/%s/#", mqttappname);
      int e = mosquitto_subscribe (mqtt, NULL, sub,
                                   0);
      if (e)
         errx (1, "MQTT subscribe failed %s (%s)", mosquitto_strerror (e), sub);
      if (debug)
         warnx ("MQTT Sub %s", sub);
      free (sub);
      asprintf (&sub, "info/%s/#", mqttappname);
      e = mosquitto_subscribe (mqtt, NULL, sub, 0);
      if (e)
         errx (1, "MQTT subscribe failed %s (%s)", mosquitto_strerror (e), sub);
      if (debug)
         warnx ("MQTT Sub %s", sub);
      free (sub);
      if (locus)
      {
         asprintf (&sub, "command/%s/*/status", mqttappname);
         e = mosquitto_publish (mqtt, NULL, sub, 0, NULL, 1, 0);
         if (e)
            errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), sub);
         free (sub);
      }
      if (resend)
      {
         asprintf (&sub, "command/%s/*/resend", mqttappname);
         e = mosquitto_publish (mqtt, NULL, sub, 0, NULL, 1, 0);
         if (e)
            errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), sub);
         free (sub);
      }
   }
   void disconnect (struct mosquitto *mqtt, void *obj, int rc)
   {
      obj = obj;
      rc = rc;
   }
   void message (struct mosquitto *mqtt, void *obj, const struct mosquitto_message *msg)
   {
      obj = obj;
      char *topic = strdupa (msg->topic);
      if (!msg->payloadlen)
      {
         warnx ("No payload %s", topic);
         return;
      }
      char *type = NULL;
      char *tag = NULL;
      char *message = topic;
      char *app = strchr (topic, '/');
      if (app)
      {
         *app++ = 0;
         tag = strchr (app, '/');
         if (tag)
         {
            *tag++ = 0;
            type = strchr (tag, '/');
            if (type)
               *type++ = 0;
         }
      }
      if (!tag)
      {
         warnx ("No tag %s", topic);
         return;
      }
      char *val = malloc (msg->payloadlen + 1);
      memcpy (val, msg->payload, msg->payloadlen);
      val[msg->payloadlen] = 0;
      device_t *l;
      for (l = devices; l && strcmp (l->tag, tag); l = l->next);
      if (!l)
      {
         l = malloc (sizeof (*l));
         memset (l, 0, sizeof (*l));
         l->tag = strdup (tag);
         l->next = devices;
         devices = l;
         if (debug)
            warnx ("New device [%s]", tag);
      }
      if (!strcmp (message, "state"))
      {
         if (!type && *val == '0')
         {                      // Off wifi/mqtt
            if (l->online)
               sql_safe_query_free (&sql, sql_printf ("UPDATE `%#S` SET `mqtt`=NULL WHERE `tag`=%#s", sqldevice, tag));
            l->online = 0;
         } else if (!type && *val == '1')
         {                      // device connected
            if (resend)
            {
               char *topic;
               if (asprintf (&topic, "command/%s/%s/resend", mqttappname, tag) < 0)
                  errx (1, "malloc");
               int e = mosquitto_publish (mqtt, NULL, topic, 0,
                                          NULL, 1, 0);
               if (e)
                  errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), topic);
               free (topic);
            }
            unsigned int devid = 0;
            SQL_RES *device = sql_safe_query_store_free (&sql,
                                                         sql_printf ("SELECT * from `%#S` WHERE `tag`=%#s",
                                                                     sqldevice,
                                                                     tag));
            if (sql_fetch_row (device))
            {
               devid = atoi (sql_colz (device, "ID"));
               if (l->iccid)
                  free (l->iccid);
               l->iccid = strdup (sql_colz (device, "iccid"));
               if (l->imei)
                  free (l->imei);
               l->imei = strdup (sql_colz (device, "imei"));
               if (l->version)
                  free (l->version);
               l->version = strdup (sql_colz (device, "version"));
               if (*sql_colz (device, "upgrade") == 'Y')
               {
                  sql_safe_query_free (&sql, sql_printf ("UPDATE `%#S` SET `upgrade`='N' WHERE `tag`=%#s", sqldevice, tag));
                  char *topic;
                  if (asprintf (&topic, "command/%s/%s/upgrade", mqttappname, tag) < 0)
                     errx (1, "malloc");
                  int e = mosquitto_publish (mqtt, NULL, topic, 0,
                                             NULL, 1, 0);
                  if (e)
                     errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), topic);
                  free (topic);
               }
            } else
            {                   // No device
               sql_safe_query_free (&sql, sql_printf ("INSERT INTO `%#S` SET `tag`=%#s", sqldevice, tag));
               devid = sql_insert_id (&sql);
               sql_free_result (device);
               device = sql_safe_query_store_free (&sql, sql_printf ("SELECT * from `%#S` WHERE `ID`=%u", sqldevice, devid));
               if (!sql_fetch_row (device))
                  warnx ("WTF");
            }
            unsigned int authid = 0,
               old = 0,
               new = 0;
            SQL_RES *auth = sql_safe_query_store_free (&sql,
                                                       sql_printf
                                                       ("SELECT * from `%#S` WHERE `device`=%#S ORDER BY `ID` DESC LIMIT 1",
                                                        sqlauth,
                                                        sql_col (device,
                                                                 "ID")));
            if (sql_fetch_row (auth))
            {
               authid = atoi (sql_colz (auth, "ID"));
               if (sql_col (auth, "replaces"))
                  new = 1;
               if (!new && sql_time (sql_colz (auth, "issued")) < time (0) - 86400)
                  old = 1;
            }
            if (!authid || old || (!new && !sql_col (device, "auth")))
            {                   // Allocate authentication
               int r = open ("/dev/urandom", O_RDONLY);
               if (r >= 0)
               {
                  unsigned char auth[3 + 16 + 16];
                  if (read (r, auth, sizeof (auth)) == sizeof (auth))
                  {
                     char hex[sizeof (auth) * 2 + 1];
                     for (int n = 0; n < sizeof (auth); n++)
                        sprintf (hex + n * 2, "%02X", auth[n]);
                     sql_transaction (&sql);
                     sql_string_t s = { };
                     sql_sprintf (&s, "INSERT INTO `%#S` SET `issued`=NOW(),`device`=%u,`aes`=%#.32s,`auth`=%#.32s",
                                  sqlauth, devid, hex + 6, hex + 6 + 32);
                     if (authid)
                        sql_sprintf (&s, ",`replaces`=%u", authid);
                     sql_safe_query_s (&sql, &s);
                     authid = sql_insert_id (&sql);
                     if (authid > 0xFFFFFF
                         && sql_query_free (&sql,
                                            sql_printf ("UPDATE `%#S` SET `ID`=%u WHERE `ID`=%u", sqlauth,
                                                        authid & 0xFFFFFF, authid)))
                        sql_safe_rollback (&sql);       // Really should not happen
                     else
                     {
                        if (sql_commit (&sql))
                           warnx ("%s commit failed", tag);
                        else
                           new = 1;
                     }
                  } else
                     warnx ("Bad read random");
                  close (r);
               } else
                  warnx ("Bad open random");
            }
            sql_free_result (auth);
            sql_free_result (device);
            if (new)
            {                   // Does device need new key?
               auth = sql_safe_query_store_free (&sql, sql_printf ("SELECT * FROM `%#S` WHERE `ID`=%u", sqlauth, authid));
               if (sql_fetch_row (auth))
               {
                  char *topic;
                  if (asprintf (&topic, "setting/%s/%s/auth", mqttappname, tag) < 0)
                     errx (1, "malloc");
                  char *value;
                  if (asprintf
                      (&value, "%06X%s%s", atoi (sql_colz (auth, "ID")), sql_colz (auth, "aes"), sql_colz (auth, "auth")) < 0)
                     errx (1, "malloc");
                  int e = mosquitto_publish (mqtt, NULL, topic,
                                             strlen (value),
                                             value, 1, 0);
                  if (e)
                     errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), topic);
                  free (topic);
                  free (value);
               }
               sql_free_result (auth);
            }
            char *v = val;
            while (*v && *v != ' ')
               v++;
            while (*v == ' ')
               v++;
            if (!strncmp (v, "ESP32 ", 6))
               v += 6;
            while (*v == ' ')
               v++;
            char *e = v;
            while (*e && *e != ' ')
               e++;
            *e = 0;
            if (!l->version || strcmp (v, l->version))
            {
               if (l->version)
                  free (l->version);
               l->version = strdup (v);
               sql_safe_query_free (&sql,
                                    sql_printf ("UPDATE `%#S` SET `version`=%#s WHERE `ID`=%u", sqldevice, l->version, devid));
            }
            if (!l->online)
               sql_safe_query_free (&sql, sql_printf ("UPDATE `%#S` SET `mqtt`=NOW() WHERE `ID`=%u", sqldevice, devid));
            l->online = 1;
         }
      } else if (!strcmp (message, "info") && type)
      {
         if (!strcmp (type, "udp"))
         {
            time_t resend = process_udp (&sql, msg->payloadlen,
                                         msg->payload, NULL, 0);
            if (resend)
            {
               char temp[22];
               struct tm t;
               gmtime_r (&resend, &t);
               strftime (temp, sizeof (temp), "%F %T", &t);
               char *topic;
               if (asprintf (&topic, "command/%s/%s/resend", mqttappname, tag) < 0)
                  errx (1, "malloc");
               int e = mosquitto_publish (mqtt, NULL, topic,
                                          strlen (temp), temp,
                                          1, 0);
               if (e)
                  errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), topic);
               free (topic);
            }
         } else if (!strcmp (type, "iccid"))
         {
            if (strcmp (l->iccid, val))
            {
               free (l->iccid);
               l->iccid = strdup (val);
               sql_safe_query_free (&sql, sql_printf ("UPDATE `%#S` SET `iccid`=%#s WHERE `tag`=%#s", sqldevice, l->iccid, tag));
            }
         } else if (!strcmp (type, "imei"))
         {
            if (strcmp (l->imei, val))
            {
               free (l->imei);
               l->imei = strdup (val);
               sql_safe_query_free (&sql, sql_printf ("UPDATE `%#S` SET `imei`=%#s WHERE `tag`=%#s", sqldevice, l->imei, tag));
            }
         } else if (!strcmp (type, "gpsrx"))
         {
            char *f[100],
             *p = val;
            int n = 0;
            while (*p && n < sizeof (f) / sizeof (*f))
            {
               f[n++] = p;
               while (*p && *p != ',')
                  p++;
               if (*p)
                  *p++ = 0;
            }
            if (n >= 11 && !strcmp (val, "$PMTKLOG"))
            {                   // Status: $PMTKLOG,32,1,a,31,15,0,0,0,8032,100
               if (locus && atoi (f[9]))
               {                // There is data to log
                  char *topic;
                  if (asprintf (&topic, "command/%s/%s/dump", mqttappname, tag) < 0)
                     errx (1, "malloc");
                  int e = mosquitto_publish (mqtt, NULL, topic, 0,
                                             NULL, 1, 0);
                  if (e)
                     errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), topic);
                  free (topic);
               }
            } else if (n >= 2 && !strcmp (val, "$PMTKLOX"))
            {                   // Data: $PMTKLOX,0,1366
               int type = atoi (f[1]);
               if (!type)
               {                // Start
                  int rec = atoi (f[2]);
                  if (l->len)
                  {             // Clear previous data not completed
                     free (l->data);
                     l->data = NULL;
                     l->len = 0;
                     l->ptr = 0;
                  }
                  l->lines = rec;
                  l->line = 0;  // Expected
                  if (debug)
                     warnx ("%s Download %d lines", tag, rec);
               } else if (type == 1)
               {                // Data
                  if (l->lines)
                  {
                     int rec = atoi (f[2]);
                     if (l->line != rec)
                     {
                        warnx ("%s Expected %d got %d", tag, l->line, rec);
                        l->lines = 0;   // Bad
                     } else if (l->lines <= rec)
                     {
                        warnx ("%s Max %d got %d", tag, l->lines, rec);
                        l->lines = 0;   // Bad
                     } else
                     {          // Process: $PMTKLOX,1,1356,8820B15D,025FAD4D,42387A48,BF480046,9720B15D,025CAD4D,42037948,BF4A0060,A620B15D,025FAD4D,424E7B48,BF4B001C,B520B15D,0263AD4D,42007D48,BF4C007C,C420B15D,025EAD4D,42047848,BF4B0036,D320B15D,025DAD4D,42787648,BF4B0050
                        int q = 3;
                        while (q < n)
                        {
                           unsigned long v = strtoul (f[q], NULL, 16);
                           if (l->len < l->ptr + 4)
                              l->data = realloc (l->data, l->len += 1024);
                           l->data[l->ptr++] = (v >> 24);
                           l->data[l->ptr++] = (v >> 16);
                           l->data[l->ptr++] = (v >> 8);
                           l->data[l->ptr++] = v;
                           q++;
                        }
                        l->line++;
                     }
                  }
               } else if (type == 2)
               {                // End
                  if (!l->lines)
                     warnx ("%s Download failed", tag);
                  else if (l->line != l->lines)
                     warnx ("%s Bad completion, expected %d got %d", tag, l->lines, l->line);
                  else
                  {             // Save
                     if (debug)
                        warnx ("%s Downloaded %d lines", tag, l->lines);
                     // There looks to be a header and then records
                     // 16-byte LOCUS buffer format
                     // 0-3 = UTC (in UNIX ticks format)
                     // 4 = FIX flag
                     // 5-8 = Latitude
                     // 9-12 = Longitude
                     // 13-14 = Height
                     // 15 = Checksum
                     sql_transaction (&sql);
                     unsigned char *p = l->data,
                        *e = l->data + l->ptr;
                     while (p + 16 <= e)
                     {
#if 0
                        warnx
                           ("%08X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                            (int) (p - l->data), p[0], p[1], p[2],
                            p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
#endif
                        if (!((p - l->data) & 0xFFF))
                        {       // Header stuff?
                           p += 0x40;
                           continue;
                        }
                        unsigned char c = 0;
                        for (int q = 0; q < 16; q++)
                           c ^= p[q];
                        if (c)
                        {
                           warnx ("%s Bad checksum at %04X", tag, (int) (p - l->data));
                           l->lines = 0;
                           break;
                        }
                        time_t ts = p[0] + (p[1] << 8) + (p[2] << 16) + (p[3] << 24);
                        if (ts != 0xFFFFFFFF)
                        {
                           unsigned char fix = p[4];
                           double get (int o)
                           {
                              unsigned int v = p[o] + (p[o + 1] << 8) + (p[o + 2] << 16) + (p[o + 3] << 24);
                              if (v == 0xFFFFFFFF)
                                 return 0;
                              double d = (1.0 + ((double) (v & 0x7FFFFF)) / 8388607.0) * pow (2,
                                                                                              ((int) ((v >> 23) & 0xFF) - 127));
                              if (v & 0x80000000)
                                 d = -d;
                              return d;
                           }
                           double lat = get (5);
                           double lon = get (9);
                           short height = p[13] + (p[14] << 8);
                           if (isnormal (lat) && isnormal (lon))
                              sql_safe_query_free (&sql,
                                                   sql_printf
                                                   ("INSERT IGNORE INTO `%#S` SET `device`=%#s,`utc`=%#U,`fix`=%d,`lat`=%lf,`lon`=%lf,height=%d",
                                                    sqlgps, tag, ts, fix, lat, lon, height));
                        }
                        p += 16;
                     }
                     if (sql_commit (&sql))
                     {
                        warnx ("%s commit failed", tag);
                        l->lines = 0;
                     }
                     if (save)
                     {
                        chdir ("/tmp");
                        int f = open (tag, O_WRONLY | O_CREAT);
                        write (f, l->data, l->ptr);
                        close (f);
                     }
                     if (!debug && l->lines)
                     {
                        // Clear log
                        char *topic;
                        if (asprintf (&topic, "command/%s/%s/erase", mqttappname, tag) < 0)
                           errx (1, "malloc");
                        int e = mosquitto_publish (mqtt, NULL, topic, 0,
                                                   NULL, 1, 0);
                        if (e)
                           errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), topic);
                        free (topic);
                     }
                  }
                  // Free
                  free (l->data);
                  l->data = NULL;
                  l->len = 0;
                  l->ptr = 0;
                  l->lines = 0;
                  l->line = 0;
               } else
                  warnx ("%s Unknown type %d", tag, type);
            } else if (strcmp (val, "$PMTK001") && strncmp (val, "$PMTK010", 8))
               warnx ("rx %s %s", tag, val);    // Unknown
         }
      }
      free (val);
   }

   mosquitto_connect_callback_set (mqtt, connect);
   mosquitto_disconnect_callback_set (mqtt, disconnect);
   mosquitto_message_callback_set (mqtt, message);
   e = mosquitto_connect (mqtt, mqtthostname, 1883, 60);
   if (e)
      errx (1, "MQTT connect failed (%s) %s", mqtthostname, mosquitto_strerror (e));
   sql_real_connect (&sql, sqlhostname, sqlusername, sqlpassword, sqldatabase, 0, NULL, 0, 1, sqlconffile);
   e = mosquitto_loop_forever (mqtt, -1, 1);
   if (e)
      errx (1, "MQTT loop failed %s", mosquitto_strerror (e));
   mosquitto_destroy (mqtt);
   mosquitto_lib_cleanup ();
   sql_close (&sql);
   return 0;
}
