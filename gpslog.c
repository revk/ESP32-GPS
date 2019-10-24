// GPS dump - when a GPS unit reports it has log data, this initiates a dump and stored to database
// Copyright (c) 2019 Adrian Kennard, Andrews & Arnold Limited, see LICENSE file (GPL)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <popt.h>
#include <err.h>
#include <malloc.h>
#include <time.h>
#include <sqllib.h>
#include <mosquitto.h>
#include <math.h>

typedef struct log_s log_t;
struct log_s
{
   log_t *next;
   char *tag;
   int lines;
   int line;
   int len;
   int ptr;
   unsigned char *data;
};
log_t *logs = NULL;

int
main (int argc, const char *argv[])
{
   const char *sqlhostname = NULL;
   const char *sqldatabase = "gps";
   const char *sqlusername = NULL;
   const char *sqlpassword = NULL;
   const char *sqlconffile = NULL;
   const char *sqltable = "gps";
   const char *mqtthostname = "localhost";
   const char *mqttusername = NULL;
   const char *mqttpassword = NULL;
   const char *mqttappname = "GPS";
   const char *mqttid = NULL;
   int debug = 0;
   int save = 0;
   {                            // POPT
      poptContext optCon;       // context for parsing command-line options
      const struct poptOption optionsTable[] = {
         {"sql-conffile", 'c', POPT_ARG_STRING, &sqlconffile, 0, "SQL conf file", "filename"},
         {"sql-hostname", 'H', POPT_ARG_STRING, &sqlhostname, 0, "SQL hostname", "hostname"},
         {"sql-database", 'd', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqldatabase, 0, "SQL database", "db"},
         {"sql-username", 'U', POPT_ARG_STRING, &sqlusername, 0, "SQL username", "name"},
         {"sql-password", 'P', POPT_ARG_STRING, &sqlpassword, 0, "SQL password", "pass"},
         {"sql-table", 't', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqltable, 0, "SQL table", "table"},
         {"sql-debug", 'v', POPT_ARG_NONE, &sqldebug, 0, "SQL Debug"},
         {"mqtt-hostname", 'h', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &mqtthostname, 0, "MQTT hostname", "hostname"},
         {"mqtt-username", 'u', POPT_ARG_STRING, &mqttusername, 0, "MQTT username", "username"},
         {"mqtt-password", 'p', POPT_ARG_STRING, &mqttpassword, 0, "MQTT password", "password"},
         {"mqtt-appname", 'a', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &mqttappname, 0, "MQTT appname", "appname"},
         {"mqtt-id", 0, POPT_ARG_STRING, &mqttid, 0, "MQTT id", "id"},
         {"save", 0, POPT_ARG_NONE, &save, 0, "Save LOCUS file"},
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
      asprintf (&sub, "info/%s/#", mqttappname);
      int e = mosquitto_subscribe (mqtt, NULL, sub, 0);
      if (e)
         errx (1, "MQTT subscribe failed %s (%s)", mosquitto_strerror (e), sub);
      if (debug)
         warnx ("MQTT Sub %s", sub);
      free (sub);
      asprintf (&sub, "command/%s/*/status", mqttappname);
      e = mosquitto_publish (mqtt, NULL, sub, 0, NULL, 1, 0);
      if (e)
         errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), sub);
      free (sub);
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
      char *type = strrchr (topic, '/');
      if (!type)
      {
         warnx ("Unknown topic %s", topic);
         return;
      }
      *type++ = 0;
      char *tag = strrchr (topic, '/');
      if (!tag)
      {
         warnx ("Unknown topic %s", topic);
         return;
      }
      *tag++ = 0;
      char *val = malloc (msg->payloadlen + 1);
      memcpy (val, msg->payload, msg->payloadlen);
      val[msg->payloadlen] = 0;
      log_t *l;
      for (l = logs; l && strcmp (l->tag, tag); l = l->next);
      if (!l)
      {
         l = malloc (sizeof (*l));
         memset (l, 0, sizeof (*l));
         l->tag = strdup (tag);
         l->next = logs;
         logs = l;
         if (debug)
            warnx ("New device [%s]", tag);
      }
      if (!strcmp (type, "rx"))
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
         {                      // Status: $PMTKLOG,32,1,a,31,15,0,0,0,8032,100
            if (atoi (f[9]))
            {                   // There is data to log
               char *topic;
               if (asprintf (&topic, "command/%s/%s/dump", mqttappname, l->tag) < 0)
                  errx (1, "malloc");
               int e = mosquitto_publish (mqtt, NULL, topic, 0, NULL, 1, 0);
               if (e)
                  errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), topic);
               free (topic);
            }
         } else if (n >= 2 && !strcmp (val, "$PMTKLOX"))
         {                      // Data: $PMTKLOX,0,1366
            int type = atoi (f[1]);
            if (!type)
            {                   // Start
               int rec = atoi (f[2]);
               if (l->len)
               {                // Clear previous data not completed
                  free (l->data);
                  l->data = NULL;
                  l->len = 0;
                  l->ptr = 0;
               }
               l->lines = rec;
               l->line = 0;     // Expected
               if (debug)
                  warnx ("%s Download %d lines", l->tag, rec);
            } else if (type == 1)
            {                   // Data
               if (l->lines)
               {
                  int rec = atoi (f[2]);
                  if (l->line != rec)
                  {
                     warnx ("%s Expected %d got %d", l->tag, l->line, rec);
                     l->lines = 0;      // Bad
                  } else if (l->lines <= rec)
                  {
                     warnx ("%s Max %d got %d", l->tag, l->lines, rec);
                     l->lines = 0;      // Bad
                  } else
                  {             // Process: $PMTKLOX,1,1356,8820B15D,025FAD4D,42387A48,BF480046,9720B15D,025CAD4D,42037948,BF4A0060,A620B15D,025FAD4D,424E7B48,BF4B001C,B520B15D,0263AD4D,42007D48,BF4C007C,C420B15D,025EAD4D,42047848,BF4B0036,D320B15D,025DAD4D,42787648,BF4B0050
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
            {                   // End
               if (!l->lines)
                  warnx ("%s Download failed", l->tag);
               else if (l->line != l->lines)
                  warnx ("%s Bad completion, expected %d got %d", l->tag, l->lines, l->line);
               else
               {                // Save
                  if (debug)
                     warnx ("%s Downloaded %d lines", l->tag, l->lines);

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
                            (int) (p - l->data), p[0], p[1], p[2],
                            p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
#endif
                     if (!((p - l->data) & 0xFFF))
                     {          // Header stuff?
                        p += 0x40;
                        continue;
                     }
                     unsigned char c = 0;
                     for (int q = 0; q < 16; q++)
                        c ^= p[q];
                     if (c)
                     {
                        warnx ("%s Bad checksum at %04X", l->tag, (int) (p - l->data));
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
                                                 sqltable, l->tag, ts, fix, lat, lon, height));
                     }
                     p += 16;
                  }
                  if (!sql_commit ())
                  {
                     warnx ("%s commit failed", l->tag);
                     l->lines = 0;
                  }
                  if (save)
                  {
                     chdir ("/tmp");
                     int f = open (l->tag, O_WRONLY | O_CREAT);
                     write (f, l->data, l->ptr);
                     close (f);
                  }
                  if (!debug && l->lines)
                  {
                     // Clear log
                     char *topic;
                     if (asprintf (&topic, "command/%s/%s/erase", mqttappname, l->tag) < 0)
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
               warnx ("%s Unknown type %d", l->tag, type);
         } else if (strcmp (val, "$PMTK001"))
            warnx ("rx %s %s", l->tag, val);    // Unknown
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
