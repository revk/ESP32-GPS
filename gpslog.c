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
#include <fcntl.h>
#include <popt.h>
#include <err.h>
#include <malloc.h>
#include <time.h>
#include <sqllib.h>
#include <mosquitto.h>
#include <math.h>
#include <openssl/evp.h>

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

const char *sqlhostname = NULL;
const char *sqldatabase = "gps";
const char *sqlusername = NULL;
const char *sqlpassword = NULL;
const char *sqlconffile = NULL;
const char *sqltable = "gps";
const char *sqldevice = "device";


static const unsigned int crc32_table[] = {
   0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,
   0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
   0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
   0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
   0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9,
   0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
   0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011,
   0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
   0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
   0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
   0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81,
   0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
   0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49,
   0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
   0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
   0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
   0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae,
   0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
   0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
   0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
   0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
   0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
   0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066,
   0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
   0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e,
   0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
   0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
   0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
   0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
   0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
   0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686,
   0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
   0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
   0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
   0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f,
   0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
   0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47,
   0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
   0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
   0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
   0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7,
   0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
   0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f,
   0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
   0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
   0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
   0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f,
   0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
   0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
   0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
   0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
   0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
   0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30,
   0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
   0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088,
   0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
   0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
   0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
   0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
   0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
   0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0,
   0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
   0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
   0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};


unsigned int
xcrc32 (const unsigned char *buf, int len, unsigned int init)
{
   unsigned int crc = init;
   while (len--)
   {
      crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ *buf) & 255];
      buf++;
   }
   return crc;
}

void
process_udp (SQL * sqlp, unsigned int len, unsigned char *data)
{
   if (len < 8 + 16)
   {
      warnx ("Bad UDP len %d", len);
      return;
   }
   if (*data != 0x2A)
   {
      warnx ("Bad version %02X", *data);
      return;
   }
   char id[7];
   sprintf (id, "%02X%02X%02X", data[1], data[2], data[3]);
   time_t t = (data[4] << 24) + (data[5] << 16) + (data[6] << 8) + (data[7]);
   SQL_RES *res = sql_safe_query_store_free (sqlp, sql_printf ("SELECT * from `%#S` WHERE `device`=%#s", sqldevice, id));
   const char *process (void)
   {                            // Simplify error checking and ensure free
      if (!sql_fetch_row (res))
         return "Unknown device";
      char *aes = sql_colz (res, "aes");
      if (strlen (aes) < 32)
         return "Bad AES in database";
      unsigned char key[16],
        iv[16];;
      int n;
      for (n = 0; n < sizeof (key); n++)
         key[n] =
            (((isalpha (aes[n * 2]) ? 9 : 0) + (aes[n * 2] & 0xF)) << 4) + isalpha (aes[n * 2 + 1] ? 9 : 0) +
            (aes[n * 2 + 1] & 0xF);
      memcpy (iv, data, 8);
      memcpy (iv + 8, data, 8);
      EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new ();
      if (!ctx)
         return "Cannot make ctx";
      if (EVP_DecryptInit_ex (ctx, EVP_aes_128_cbc (), NULL, key, iv) != 1)
         return "Decrypt error (int)";
      EVP_CIPHER_CTX_set_padding (ctx, 0);
      if (EVP_DecryptUpdate (ctx, data + 8, &n, data + 8, len - 8) != 1)
         return "Decrypt error (update)";
      if (EVP_DecryptFinal_ex (ctx, data + 8 + n, &n) != 1)
         return "Decrypt error (final)";
      unsigned char *p = data + 10;
      unsigned char *e = p + (data[8] << 8) + data[9];
      if (e + 4 > data + len)
         return "Bad length";
      // CRC
      unsigned int crc = xcrc32 (data, e - data, 0);
      fprintf (stderr, "CRC %d: %08X %02X %02X %02X %02X\n", (int) (e - data), crc, e[0], e[1], e[2], e[3]);
      // TODO
      unsigned int type = (p[0] << 8) + p[1];
      p += 2;
      if (!type)
      {                         // Tracking
         sql_transaction (sqlp);
         while (p + 12 <= e)
         {
            unsigned short tim = (p[0] << 8) + p[1];
            short alt = (p[2] << 8) + p[3];
            int lat = (p[4] << 24) + (p[5] << 16) + (p[6] << 8) + p[7];
            int lon = (p[8] << 24) + (p[9] << 16) + (p[10] << 8) + p[11];
            sql_safe_query (sqlp,
                            sql_printf ("REPLACE INTO `%#S` SET `device`=%#s,`utc`=concat(%#U,'.%u'),`alt`=%d,`lat`=%f,lon=%f", sqltable,
                                        id, t + (tim / 10), tim % 10, alt, (float) lat / 600000.0, (float) lon / 600000.0));
            p += 12;
         }
         if (sql_commit (sqlp))
            return "Bad SQL commit";
         if (p < e)
            return "Bad length";
      } else
         return "Unknown message type";
      return NULL;
   }
   const char *e = process ();
   if (e)
      warnx ("%s: %s", id, e);  // Error
   sql_free_result (res);
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
      unsigned char rx[1500];
      struct sockaddr_in6 from;
      socklen_t fromlen = sizeof (from);
      size_t len = recvfrom (s, rx, sizeof (rx) - 1, 0, (struct sockaddr *) &from, &fromlen);
      if (len < 0)
         return -1;
      process_udp (&sql, len, rx);
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
   int debug = 0;
   int save = 0;
   int locus = 0;               // Flash log download
   {                            // POPT
      poptContext optCon;       // context for parsing command-line options
      const struct poptOption optionsTable[] = {
         {"sql-conffile", 'c', POPT_ARG_STRING, &sqlconffile, 0, "SQL conf file", "filename"},
         {"sql-hostname", 'H', POPT_ARG_STRING, &sqlhostname, 0, "SQL hostname", "hostname"},
         {"sql-database", 'd', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqldatabase, 0, "SQL database", "db"},
         {"sql-username", 'U', POPT_ARG_STRING, &sqlusername, 0, "SQL username", "name"},
         {"sql-password", 'P', POPT_ARG_STRING, &sqlpassword, 0, "SQL password", "pass"},
         {"sql-table", 't', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqltable, 0, "SQL log table", "table"},
         {"sql-device", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &sqldevice, 0, "SQL device table", "table"},
         {"sql-debug", 'v', POPT_ARG_NONE, &sqldebug, 0, "SQL Debug"},
         {"mqtt-hostname", 'h', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &mqtthostname, 0, "MQTT hostname", "hostname"},
         {"mqtt-username", 'u', POPT_ARG_STRING, &mqttusername, 0, "MQTT username", "username"},
         {"mqtt-password", 'p', POPT_ARG_STRING, &mqttpassword, 0, "MQTT password", "password"},
         {"mqtt-appname", 'a', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &mqttappname, 0, "MQTT appname", "appname"},
         {"mqtt-id", 0, POPT_ARG_STRING, &mqttid, 0, "MQTT id", "id"},
         {"locus", 'L', POPT_ARG_NONE, &locus, 0, "Get LOCUS file"},
         {"save", 0, POPT_ARG_NONE, &save, 0, "Save LOCUS file"},
         {"bindport", 0, POPT_ARG_INT, &bindport, 0, "UDP port to bind for collecting tracking"},
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
      asprintf (&sub, "info/%s/#", mqttappname);
      int e = mosquitto_subscribe (mqtt, NULL, sub, 0);
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
      if (bindport)
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
      if (!strcmp (type, "udp"))
         process_udp (&sql, msg->payloadlen, msg->payload);
      else if (!strcmp (type, "rx"))
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
            if (locus && atoi (f[9]))
            {                   // There is data to log
               char *topic;
               if (asprintf (&topic, "command/%s/%s/dump", mqttappname, l->tag) < 0)
                  errx (1, "malloc");
               int e = mosquitto_publish (mqtt, NULL, topic, 0, NULL, 1, 0);
               if (e)
                  errx (1, "MQTT publish failed %s (%s)", mosquitto_strerror (e), topic);
               free (topic);
            }
            if (bindport)
            {
               char *topic;
               if (asprintf (&topic, "command/%s/%s/resend", mqttappname, l->tag) < 0)
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
                  if (sql_commit (&sql))
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
