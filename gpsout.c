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
   int maxtime = 60;
   double maxdist = 100;
   double maxerror = 0.1;
   int prune = 0;
   int json = 0;
   int quiet = 0;
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
         {"max-time", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &maxtime, 0, "Max time pruned", "seconds"},
         {"max-dist", 0, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &maxdist, 0, "Max dist pruned", "m"},
         {"max-error", 0, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &maxerror, 0, "Max error pruned", "m"},
         {"device", 'D', POPT_ARG_STRING, &device, 0, "Device", "ID"},
         {"from", 'F', POPT_ARG_STRING, &from, 0, "From", "YYYY-MM-DD HH:MM:SS (UTC)"},
         {"to", 'T', POPT_ARG_STRING, &to, 0, "To", "YYYY-MM-DD HH:MM:SS (UTC)"},
         {"prune", 0, POPT_ARG_NONE, &prune, 0, "Do pruning"},
         {"json", 0, POPT_ARG_NONE, &json, 0, "JSON outout"},
         {"quiet", 0, POPT_ARG_NONE, &quiet, 0, "Quiet"},
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
   SQL_RES *res = sql_safe_query_store_free (&sql,
                                             sql_printf ("SELECT * FROM `%#S` WHERE `utc`>=%#T AND `utc`<=%#T AND `device`=%#s",
                                                         sqltable,
                                                         tfrom, tto, device));
   if (json)
      printf ("[\n");
   double scalelat,
     scalelon;
   double maxsquare = maxdist * maxdist;
   struct point_s
   {
      struct point_s *next;
      int id;
      time_t utc;
      double lat,
        lon;
      double x,
        y;                      // relative to first point
      char prune;
   } *first = NULL, *last = NULL;
   while (sql_fetch_row (res))
   {
      void output (time_t utc, double lat, double lon)
      {
         if (json)
         {
            struct tm t;
            localtime_r (&utc, &t);
            char temp[22];
            strftime (temp, sizeof (temp), "%F %T", &t);
            printf ("{\"utc\":\"%s\",\"lat\":%lf,\"lon\":%lf}\n", temp, lat, lon);
         } else if (!quiet)
            printf ("%lf,%lf\n", lat, lon);
      }
      double lat = strtod (sql_colz (res, "lat"), NULL);
      double lon = strtod (sql_colz (res, "lon"), NULL);
      time_t utc = sql_time (sql_colz (res, "utc"));
      if (!prune)
      {
         output (utc, lat, lon);
         continue;
      }
      struct point_s *p = malloc (sizeof (*p));
      memset (p, 0, sizeof (*p));
      p->id = atoi (sql_colz (res, "ID"));
      p->utc = utc;
      p->lat = lat;
      p->lon = lon;
      void newfirst (void)
      {
         last = first;
         first->x = 0;
         first->y = 0;
         scalelat = (double) 11111;     // Yes, constant for that is based on French definition of metre
         scalelon = (double) 11111 *cos (first->lat);
      }
      if (first && (p->utc - first->utc > maxtime || (p->x * p->x) * (p->y * p->y) > maxsquare))
      {
         double dist (struct point_s *a, struct point_s *p, struct point_s *b)
         {                      // Distance of point p from line a/b
            if (a->x == b->x && a->y == b->y)
               return sqrt ((p->x - a->x) * (p->x - a->x) + (p->y - a->y) * (p->y - a->y));     // Point
            double A = (b->y - a->y) * p->x - (b->x - a->x) * p->y + b->x * a->y - b->y * a->x;
            if (A < 0)
               A = 0.0 - A;
            double B = sqrt ((b->y - a->y) * (b->y - a->y) + (b->x - a->x) * (b->x - a->x));
            return A / B;

         }
         int rdp (struct point_s *f, struct point_s *t)
         {                      // Prune: https://en.wikipedia.org/wiki/Ramer–Douglas–Peucker_algorithm
            f->prune = 0;
            t->prune = 0;
            if (f == t || f->next == t)
               return 0;        // Nothing to prune
            struct point_s *p,
             *b = NULL;
            double best = 0;
            for (p = f->next; p != t; p = p->next)
            {
               double d = dist (f, p, t);
               if (!b || d > best)
               {
                  b = p;
                  best = d;
               }
            }
            if (best < maxerror)
            {                   // Prune all
               int n = 0;
               for (p = f->next; p != t; p = p->next)
               {
                  p->prune = 1;
                  n++;
               }
               return n;
            }
            // Recurse
            return rdp (f, b) + rdp (b, t);
         }
         rdp (first, last);
         while (first->next)
         {
            if (!first->prune)
               output (first->utc, first->lat, first->lon);
            struct point_s *n = first->next;
            free (first);
            first = n;
         }
         last = first;
         newfirst ();
      }
      if (first)
      {                         // Offset data
         p->y = (p->lat - first->lat) * scalelat;
         p->x = (p->lon - first->lon) * scalelon;
      }
      if (first)
         last->next = p;
      else
      {
         first = p;
         newfirst ();
      }
      last = p;
   }
   if (json)
      printf ("]\n");
   sql_free_result (res);
   sql_close (&sql);
   return 0;
}
