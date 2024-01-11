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

int
main (int argc, const char *argv[])
{
   int debug = 0;
   poptContext optCon;          // context for parsing command-line options
   {                            // POPT
      const struct poptOption optionsTable[] = {
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
                        "<name>%s</name>\n"     //
                        "<trk><trkseg>\n", id);
               for (j_t e = j_first (g); e; e = j_next (e))
               {
                  const char *ts = j_get (e, "ts");
                  const char *lat = j_get (e, "lat");
                  const char *lon = j_get (e, "lon");
                  const char *alt = j_get (e, "alt");
                  fprintf (o, "<trkpt lat=\"%s\" lon=\"%s\">", lat, lon);
                  if (alt)
                     fprintf (o, "<ele>%s</ele>", alt);
                  fprintf (o, "<time>%s</time>", ts);
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
