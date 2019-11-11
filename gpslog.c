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
         for (int n = 0; n < len + MACLEN; n++)
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
      unsigned int period = 0;
      time_t last = sql_time_utc (sql_colz (device, "lastupdateutc"));
      resend = 0;
      unsigned int lastupdate = 0;
      int margin = -1;
      unsigned int fixes = 0;
      float ascale = 1.0 / ASCALE;
      sql_transaction (sqlp);
      while (p < e && !(*p & TAGF_FIX))
      {                         // Process tags
         unsigned int dlen = 0;
         if (*p < 0x20)
            dlen = 0;
         else if (*p < 0x40)
            dlen = 1;
         else if (*p < 0x60)
            dlen = 2;
         else if (*p < 0x7F)
            dlen = 4;
         else
            dlen = 3 + (p[2] << 8) + p[3];
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
         else if (*p == TAGF_BALLOON)
            ascale = ALT_BALLOON;
         else if (*p == TAGF_FLIGHT)
            ascale = ALT_FLIGHT;
         else if (*p == TAGF_MARGIN)
         {
            margin = (p[1] << 8) + p[2];
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
                  if (tim > lastfix)
                     lastfix = tim;
                  int lat = (q[0] << 24) + (q[1] << 16) + (q[2] << 8) + q[3];
                  q += 4;
                  int lon = (q[0] << 24) + (q[1] << 16) + (q[2] << 8) + q[3];
                  q += 4;
                  sql_string_t f = { };
                  sql_sprintf (&f, "REPLACE INTO `%#S` SET `device`=%u,`utc`='%U." TPART "',`lat`=%.8lf,`lon`=%.8lf", sqlgps, devid,
                               t + (tim / TSCALE), tim % TSCALE, (double) lat / 60.0 / DSCALE, (double) lon / 60.0 / DSCALE);
                  if (fixtags & TAGF_FIX_ALT)
                  {
                     short alt = (q[0] << 8) + q[1];
                     float a = (float) alt * ascale;
                     q += 2;
                     sql_sprintf (&f, ",`alt`=%.1f", a);
                  }
                  if (fixtags & TAGF_FIX_SATS)
                  {
                     sql_sprintf (&f, ",`sats`=%u", *q & 0x7F);
                     q++;
                  }
                  if ((fixtags & TAGF_FIX_HDOP) && *q)
                  {
                     sql_sprintf (&f, ",`hdop`=%u." HPART, *q / HSCALE, *q % HSCALE);
                     q++;
                  }
                  sql_safe_query_s (sqlp, &f);
                  p += fixlen;
                  fixes++;
               }
               if (lastfix >= 0)
                  sql_sprintf (&s, ",`lastfixutc`='%U." TPART "'", t + (lastfix / TSCALE), lastfix % TSCALE);
            }
         }
         if (addr)
            sql_sprintf (&s, ",`ip`=%#s,`port`=%u,`lastip`=now()", addr, port);
         else if (sql_time (sql_colz (device, "lastip")) < time (0) - 3600)
            sql_sprintf (&s, ",`ip`=NULL,`port`=NULL,`lastip`=NULL");
         if (atoi (sql_colz (device, "auth")) != id)
            sql_sprintf (&s, ",`auth`=%u", id);
         sql_sprintf (&s, " WHERE `ID`=%u", devid);
         sql_safe_query_s (sqlp, &s);
         sql_sprintf (&s, "INSERT INTO `%#S` SET `device`=%u,`utc`=%#T,`period`=%u,`received`=NOW(),`fixes`=%u", sqllog, devid, t,
                      period, fixes);
         if (addr)
            sql_sprintf (&s, ",`ip`=%#s,`port`=%u", addr, port);
         if (margin >= 0 && margin < 65536)
            sql_sprintf (&s, ",`margin`=%u." MPART, margin / MSCALE, margin % MSCALE);
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
   SQL_RES *res = sql_safe_query_store_free (sqlp, sql_printf ("SELECT * from `%#S` WHERE `ID`=%u", sqlauth, authid));
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
               (((isalpha (aes[n * 2]) ? 9 : 0) + (aes[n * 2] & 0xF)) << 4) + (isalpha (aes[n * 2 + 1]) ? 9 : 0) +
               (aes[n * 2 + 1] & 0xF);
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
               (((isalpha (login[n * 2]) ? 9 : 0) + (login[n * 2] & 0xF)) << 4) + (isalpha (login[n * 2 + 1]) ? 9 : 0) +
               (login[n * 2 + 1] & 0xF);
         unsigned char mac[32] = { };
         HMAC (EVP_sha256 (), key, keylen, buf, len, mac, NULL);
         memcpy (buf + len, mac, MACLEN);
         len += MACLEN;
      }
   }
   sql_free_result (res);
   buf[len++] = VERSION;        // SIM800 truncates
   return len;
}

const char *bindhost = NULL;
const char *bindport = NULL;
int
udp_task (void)
{
   int s = -1;
 const struct addrinfo hints = { ai_flags: AI_PASSIVE, ai_socktype: SOCK_DGRAM, ai_family:AF_INET6 };
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
      size_t len = recvfrom (s, rx, sizeof (rx) - 1, 0, (struct sockaddr *) &from, &fromlen);
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
         {"sql-conffile", 'c', POPT_ARG_STRING, &sqlconffile, 0, "SQL conf file", "filename"},
         {"sql-hostname", 'H', POPT_ARG_STRING, &sqlhostname, 0, "SQL hostname", "hostname"},
         {"sql-database", 'd', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqldatabase, 0, "SQL database", "db"},
         {"sql-username", 'U', POPT_ARG_STRING, &sqlusername, 0, "SQL username", "name"},
         {"sql-password", 'P', POPT_ARG_STRING, &sqlpassword, 0, "SQL password", "pass"},
         {"sql-table", 't', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqlgps, 0, "SQL log table", "table"},
         {"sql-device", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqldevice, 0, "SQL device table", "table"},
         {"sql-debug", 'v', POPT_ARG_NONE, &sqldebug, 0, "SQL Debug"},
         {"mqtt-hostname", 'h', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &mqtthostname, 0, "MQTT hostname", "hostname"},
         {"mqtt-username", 'u', POPT_ARG_STRING, &mqttusername, 0, "MQTT username", "username"},
         {"mqtt-password", 'p', POPT_ARG_STRING, &mqttpassword, 0, "MQTT password", "password"},
         {"mqtt-appname", 'a', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &mqttappname, 0, "MQTT appname", "appname"},
         {"mqtt-id", 0, POPT_ARG_STRING, &mqttid, 0, "MQTT id", "id"},
         {"locus", 'L', POPT_ARG_NONE, &locus, 0, "Get LOCUS file"},
         {"resend", 0, POPT_ARG_NONE, &resend, 0, "Ask resend of tracking over MQTT on connect"},
         {"save", 0, POPT_ARG_NONE, &save, 0, "Save LOCUS file"},
         {"port", 0, POPT_ARG_STRING, &bindport, 0, "UDP port to bind for collecting tracking"},
         {"bindhost", 0, POPT_ARG_STRING, &bindhost, 0, "UDP host to bind for collecting tracking"},
         {"debug", 'V', POPT_ARG_NONE, &debug, 0, "Debug"},
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
      int e = mosquitto_subscribe (mqtt, NULL, sub, 0);
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
               int e = mosquitto_publish (mqtt, NULL, topic, 0, NULL, 1, 0);
               if (e)
                  errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), topic);
               free (topic);
            }
            unsigned int devid = 0;
            SQL_RES *device = sql_safe_query_store_free (&sql, sql_printf ("SELECT * from `%#S` WHERE `tag`=%#s", sqldevice, tag));
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
                  int e = mosquitto_publish (mqtt, NULL, topic, 0, NULL, 1, 0);
                  if (e)
                     errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), topic);
                  free (topic);
               }
            } else
            {                   // No device
               sql_safe_query_free (&sql, sql_printf ("INSERT INTO `%#S` SET `tag`=%#s", sqldevice, tag));
               devid = sql_insert_id (&sql);
            }
            unsigned int authid = 0,
               old = 0,
               new = 0;
            SQL_RES *auth = sql_safe_query_store_free (&sql,
                                                       sql_printf
                                                       ("SELECT * from `%#S` WHERE `device`=%#S ORDER BY `ID` DESC LIMIT 1",
                                                        sqlauth,
                                                        sql_col (device, "ID")));
            if (sql_fetch_row (auth))
            {
               authid = atoi (sql_colz (auth, "ID"));
               if (sql_col (auth, "replaces"))
                  new = 1;
               if (!new && sql_time (sql_colz (auth, "issued")) < time (0) - 86400)
                  old = 1;
            }
            if (!authid || old)
            {                   // Allocate authentication
               int r = open ("/dev/urandom", O_RDONLY);
               if (r >= 0)
               {
                  char auth[3 + 16 + 16];
                  if (read (r, auth, sizeof (auth)) == sizeof (auth))
                  {
                     char hex[sizeof (auth) * 2 + 1];
                     for (int n = 0; n < sizeof (auth); n++)
                        sprintf (hex + n * 2, "%02X", auth[n]);
                     sql_transaction (&sql);
                     sql_string_t s = { };
                     sql_sprintf (&s, "INSERT INTO `%#S` SET `issued`=NOW(),`device`=%u,`aes`=%#.32s,`auth`=%#.32s", sqlauth, devid,
                                  hex + 6, hex + 6 + 32);
                     if (authid)
                        sql_sprintf (&s, ",`replaces`=%u", authid);
                     sql_safe_query_s (&sql, &s);
                     authid = sql_insert_id (&sql);
                     new = 1;
                     if (sql_commit (&sql))
                        warnx ("%s commit failed", tag);

                  }
                  close (r);
               }
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
                  if (asprintf (&value, "%06X%s%s", atoi (sql_colz (auth, "ID")), sql_colz (auth, "aes"), sql_colz (auth, "auth")) <
                      0)
                     errx (1, "malloc");
                  int e = mosquitto_publish (mqtt, NULL, topic, strlen (value), value, 1, 0);
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
            if (strcmp (v, l->version))
            {
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
            time_t resend = process_udp (&sql, msg->payloadlen, msg->payload, NULL, 0);
            if (resend)
            {
               char temp[22];
               struct tm t;
               gmtime_r (&resend, &t);
               strftime (temp, sizeof (temp), "%F %T", &t);
               char *topic;
               if (asprintf (&topic, "command/%s/%s/resend", mqttappname, tag) < 0)
                  errx (1, "malloc");
               int e = mosquitto_publish (mqtt, NULL, topic, strlen (temp), temp, 1, 0);
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
         } else if (!strcmp (type, "rx"))
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
                  int e = mosquitto_publish (mqtt, NULL, topic, 0, NULL, 1, 0);
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
                        warnx ("%08X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                               (int) (p - l->data), p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12],
                               p[13], p[14], p[15]);
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
                              double d = (1.0 + ((double) (v & 0x7FFFFF)) / 8388607.0) * pow (2, ((int) ((v >> 23) & 0xFF) - 127));
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
                        int e = mosquitto_publish (mqtt, NULL, topic, 0, NULL, 1, 0);
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
