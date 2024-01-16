// JSON 2 GPX

#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <ctype.h>
#include <err.h>
#include <ajl.h>
#include <math.h>

int debug = 0;
double rdp = 0;
double timescale = 10.0;

double
p (j_t P, const char *tag)
{
   if (!P)
      return 0;
   const char *v = j_get (P, tag);
   if (!v)
      return 0;
   return strtod (v, NULL);
}

double
dist2 (j_t A, j_t B)
{                               // Distance between two fixes
   j_t a = j_find (A, "ecef");
   j_t b = j_find (B, "ecef");
   double X = p (a, "x") - p (b, "x");
   double Y = p (a, "y") - p (b, "y");
   double Z = p (a, "z") - p (b, "z");
   double T = 0;
   if (timescale)
      T = (p (a, "t") - p (b, "t")) / timescale;;
   return X * X + Y * Y + Z * Z + T * T;
}

j_t
findmax (j_t A, j_t B, double *dsqp)
{
   if (dsqp)
      *dsqp = 0;
   if (!A || !B || A == B || j_next (A) == B)
      return NULL;
   double b2 = dist2 (B, A);
   j_t m = NULL;
   double best = 0;
   for (j_t C = j_next (A); C && C != B; C = j_next (C))
   {
      double h2 = 0;
      double a2 = dist2 (A, C),
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
      //if (debug) warnx ("a=%lf b=%lf c=%lf h=%lf", sqrt(a2), sqrt(b2), sqrt(c2), sqrt(h2));
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
dordp (j_t j, j_t B)
{
   j_t A = NULL;
   j_t L = NULL;
   while (j && j != B)
   {
      if (!j_find (j, "ecef"))
         j_store_true (j, "delete");
      else
      {
         if (!A)
            A = j;
         L = j;
      }
      j = j_next (j);
   }
   B = L;
   if (!A || A == B || j_next (A) == B)
      return;
   double dsq = 0;
   j_t M = findmax (A, B, &dsq);
   if (debug)
      warnx ("%s-%s-%s %lf", j_get (A, "seq"), j_get (M, "seq"), j_get (B, "seq"), sqrt (dsq));
   if (dsq >= rdp * rdp)
   {
      dordp (A, M);
      dordp (M, B);
   } else
   {
      while ((A = j_next (A)) && A != B)
         j_store_true (A, "delete");
   }
}

int
main (int argc, const char *argv[])
{
   poptContext optCon;          // context for parsing command-line options
   {                            // POPT
      const struct poptOption optionsTable[] = {
         {"rdp", 0, POPT_ARG_DOUBLE, &rdp, 0, "Ramer-Douglas-Peucker", "metres"},
         {"timescale", 0, POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &timescale, 0, "Scale for metres to seconds", "M"},
         {"debug", 'v', POPT_ARG_NONE, &debug, 0, "Debug"},
         POPT_AUTOHELP {}
      };

      optCon = poptGetContext (NULL, argc, argv, optionsTable, 0);
      poptSetOtherOptionHelp (optCon, "[jsonfiles]");

      int c;
      if ((c = poptGetNextOpt (optCon)) < -1)
         errx (1, "%s: %s\n", poptBadOption (optCon, POPT_BADOPTION_NOALIAS), poptStrerror (c));

      if (!poptPeekArg (optCon))
      {
         poptPrintUsage (optCon, stderr, 0);
         return -1;
      }
   }
   const char *fn;
   while ((fn = poptGetArg (optCon)))
   {
      j_t j = j_create ();
      const char *e = j_read_file (j, fn);
      if (e)
         warnx ("Failed %s: %s", fn, e);
      else
      {
         if (debug)
            j_err (j_write_pretty (j, stdout));
         j_t g = j_find (j, "gps");
         if (g)
         {
            const char *e = strrchr (fn, '.');
            if (!e)
               e = fn + strlen (fn);
            char *ofn = NULL;
            asprintf (&ofn, "%.*s.gpx", (int) (e - fn), fn);
            FILE *o = fopen (ofn, "w");
            if (!o)
               warn ("Cannot open %s", ofn);
            else
            {
               const char *id = j_get (j, "id");
               fprintf (o, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"       //
                        "<gpx version=\"1.0\">\n"       //
                        "<metadata><name>%s</name></metadata>\n"        //
                        "<trk><trkseg>\n", id);
               if (rdp)
                  dordp (j_first (g), NULL);
               for (j_t e = j_first (g); e; e = j_next (e))
               {
                  if (j_get (e, "delete"))
                     continue;
                  const char *ts = j_get (e, "ts");
                  const char *lat = j_get (e, "lat");
                  const char *lon = j_get (e, "lon");
		  if(!ts||!lat||!lon)continue;
                  const char *alt = j_get (e, "alt");
                  const char *hdop = j_get (e, "hdop");
                  const char *vdop = j_get (e, "vdop");
                  const char *pdop = j_get (e, "pdop");
                  int sats = atoi (j_get (e, "sats.used") ? : "");
                  int fix = atoi (j_get (e, "fixmode") ? : "");
                  fprintf (o, "<trkpt lat=\"%s\" lon=\"%s\">", lat, lon);
                  if (alt)
                     fprintf (o, "<ele>%s</ele>", alt);
                  fprintf (o, "<time>%s</time>", ts);
                  if (fix >= 1)
                     fprintf (o, "<fix>%s</fix>", fix == 1 ? "none" : fix == 2 ? "2d" : "3d");
                  if (sats)
                     fprintf (o, "<sat>%d</sat>", sats);
                  if (hdop)
                     fprintf (o, "<hdop>%s</hdop>", hdop);
                  if (vdop)
                     fprintf (o, "<vdop>%s</vdop>", vdop);
                  if (pdop)
                     fprintf (o, "<pdop>%s</pdop>", pdop);
                  fprintf (o, "</trkpt>\n");
               }
               fprintf (o, "</trkseg></trk>\n"  //
                        "</gpx>\n");
               fclose (o);
            }
            free (ofn);
         }
         j_delete (&j);
      }
   }

   poptFreeContext (optCon);
   return 0;
}
