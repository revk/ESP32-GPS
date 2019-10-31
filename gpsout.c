// GPS - write out set of lat/lon from database
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

typedef struct point_s point_t;
struct point_s
{
   int id;
   time_t utc;
   double lat,
     lon;
};

int
main (int argc, const char *argv[])
{
   const char *sqlhostname = NULL;
   const char *sqldatabase = "gps";
   const char *sqlusername = NULL;
   const char *sqlpassword = NULL;
   const char *sqlconffile = NULL;
   const char *sqltable = "gps";
   const char *from = NULL;
   const char *to = NULL;
   const char *device = NULL;
   int debug = 0;
   double margin = 0.1;
   int json = 0;
   int quiet = 0;
   int delete = 0;
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
         {"margin", 0, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &margin, 0, "Max allow error", "m"},
         {"device", 'D', POPT_ARG_STRING, &device, 0, "Device", "ID"},
         {"from", 'F', POPT_ARG_STRING, &from, 0, "From", "YYYY-MM-DD HH:MM:SS (UTC)"},
         {"to", 'T', POPT_ARG_STRING, &to, 0, "To", "YYYY-MM-DD HH:MM:SS (UTC)"},
         {"json", 0, POPT_ARG_NONE, &json, 0, "JSON outout"},
         {"quiet", 0, POPT_ARG_NONE, &quiet, 0, "Quiet"},
         {"delete", 0, POPT_ARG_NONE, &delete, 0, "Delete pruned from database"},
         {"debug", 'V', POPT_ARG_NONE, &debug, 0, "Debug"},
         POPT_AUTOHELP {}
      };

      optCon = poptGetContext (NULL, argc, argv, optionsTable, 0);

      int c;
      if ((c = poptGetNextOpt (optCon)) < -1)
         errx (1, "%s: %s\n", poptBadOption (optCon, POPT_BADOPTION_NOALIAS), poptStrerror (c));

      if (poptPeekArg (optCon) || !device)
      {
         poptPrintUsage (optCon, stderr, 0);
         return -1;
      }
      poptFreeContext (optCon);
   }

   time_t tfrom = time (0) - 3600;      // default
   if (from)
      tfrom = sql_time_utc (from);
   time_t tto = time (0);       // default
   if (to)
      tto = sql_time_utc (to);
   SQL sql;
   sql_real_connect (&sql, sqlhostname, sqlusername, sqlpassword, sqldatabase, 0, NULL, 0, 1, sqlconffile);
   // Get points
   SQL_RES *res = sql_safe_query_store_free (&sql,
                                             sql_printf ("SELECT * FROM `%#S` WHERE `utc`>=%#T AND `utc`<=%#T AND `device`=%#s ORDER BY `utc`",
                                                         sqltable,
                                                         tfrom, tto, device));
   int num = sql_num_rows (res);
   point_t *points = malloc (num * sizeof (*points));
   memset (points, 0, num * sizeof (*points));
   int n = 0;
   while (sql_fetch_row (res) && n < num)
   {
      points[n].id = atoi (sql_colz (res, "ID"));
      points[n].utc = sql_time_utc (sql_colz (res, "utc"));
      points[n].lat = strtod (sql_colz (res, "lat"), NULL);
      points[n].lon = strtod (sql_colz (res, "lon"), NULL);
      n++;
   }
   sql_free_result (res);
   void prune (int l, int h)
   {
      if (l + 1 >= h)
         return;                // Not intermediate points
      point_t *a = &points[l];
      point_t *b = &points[h];
      // Centre for working out metres
      double clat = (a->lat + b->lat) / 2;
      double clon = (a->lon + b->lon) / 2;
      double slat = 111111.0 * cos (M_PI * clat / 180.0);
      inline double x (point_t * p)
      {
         return (p->lon - clon) * slat;
      }
      inline double y (point_t * p)
      {
         return (p->lat - clat) * 111111.0;
      }
      double B = sqrt ((y (b) - y (a)) * (y (b) - y (a)) + (x (b) - x (a)) * (x (b) - x (a)));
      int bestn = -1;
      double best = 0;
      int n;
      for (n = l + 1; n < h; n++)
      {
         point_t *p = &points[n];
         double d = 0;
         if (!B)
            d = sqrt ((x (p) - x (a)) * (x (p) - x (a)) + (y (p) - y (a)) * (y (p) - y (a)));
         else
         {
            double A = ((y (b) - y (a)) * x (p) - (x (b) - x (a)) * y (p) + x (b) * y (a) - y (b) * x (a));
            if (A < 0)
               A = 0.0 - A;
            d = A / B;
         }
         if (bestn >= 0 && d < best)
            continue;
         bestn = n;             // New furthest
         best = d;
      }
      if (debug)
         fprintf (stderr, "Prune %d/%d/%d %lfm\n", l, bestn, h, best);
      if (best < margin)
      {                         // All points are within margin - so all to be pruned
         for (n = l + 1; n < h; n++)
         {
            if (delete)
               sql_safe_query_free (&sql, sql_printf ("DELETE FROM `%#S` WHERE `ID`=%d", sqltable, points[n].id));
            points[n].id = 0;
         }
         return;
      }
      prune (l, bestn);
      prune (bestn, h);
   }
   if (margin)
   {
      if (delete)
         sql_transaction (&sql);
      prune (0, num - 1);
      if (delete)
         sql_safe_commit (&sql);
   }
   // Output
   if (json)
      printf ("[\n");
   for (n = 0; n < num; n++)
      if (points[n].id)
      {
         if (json)
         {
            struct tm t;
            localtime_r (&points[n].utc, &t);
            char temp[22];
            strftime (temp, sizeof (temp), "%F %T", &t);
            printf ("{\"utc\":\"%s\",\"lat\":%lf,\"lon\":%lf}\n", temp, points[n].lat, points[n].lon);
         } else if (!quiet)
            printf ("%lf,%lf\n", points[n].lat, points[n].lon);
      }
   if (json)
      printf ("]\n");
   free (points);
   sql_close (&sql);
   return 0;
}
