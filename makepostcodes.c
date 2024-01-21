// Make postcode database

#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <ctype.h>
#include <err.h>

#define	GRID	1000
#define	RANGE	10000
#define	MAGIC	20240121

int debug = 0;

typedef struct postcode_s postcode_t;
struct postcode_s
{
   postcode_t *next;
   const char *postcode;
   int e,
     n;
};

postcode_t *postcodes = NULL;

int
main (int argc, const char *argv[])
{
   // Load CSVs
   if (argc <= 1)
      errx (1, "List CSV files");
   int mine = 0,
      maxe = 0,
      minn = 0,
      maxn = 0;
   for (int n = 1; n < argc; n++)
   {
      char *line = NULL;
      size_t linem = 0;
      FILE *i = fopen (argv[n], "r");
      if (!i)
         err (1, "Cannot open %s", argv[n]);
      ssize_t l = 0;
      while ((l = getline (&line, &linem, i)) >= 0)
      {
         char *postcode = line,
            *p = postcode;
         if (*postcode == '"')
         {
            p = ++postcode;
            while (*p && *p != '"')
               p++;
            if (*p)
               *p++ = 0;
            if (*p == ',')
               p++;
         }
         while (*p && *p != ',')
            p++;                // size
         if (*p)
            p++;
         int e = atoi (p);
         while (*p && *p != ',')
            p++;                // e
         if (*p)
            p++;
         int n = atoi (p);
         int l = strlen (postcode);
         if (l >= 6 && postcode[l - 4] == ' ' && e && n)
         {
            memmove (postcode + l - 4, postcode + l - 3, 4);    // Remove space
            postcode_t *new = malloc (sizeof (*new));
            new->postcode = strdup (postcode);
            new->e = e;
            new->n = n;
            new->next = postcodes;
            postcodes = new;
            if (!mine || mine > e)
               mine = e;
            if (maxe < e)
               maxe = e;
            if (!minn || minn > n)
               minn = n;
            if (maxn < n)
               maxn = n;
         }
      }
      fclose (i);
      free (line);
   }
   int gridw = (1 + maxe / GRID - mine / GRID);
   int gridh = (1 + maxn / GRID - minn / GRID);
   // Generate postcode file
   // Header
   void writeu32 (unsigned int i)
   {                            // LE
      putchar (i);
      putchar (i >> 8);
      putchar (i >> 16);
      putchar (i >> 24);
   }
   writeu32 (MAGIC);
   writeu32 (GRID);
   writeu32 (mine / GRID);
   writeu32 (minn / GRID);
   writeu32 (gridw);
   writeu32 (gridh);
   postcode_t *find (int e, int n)
   {
      long best = 0;
      postcode_t *found = NULL;
      for (postcode_t * p = postcodes; p; p = p->next)
      {
         long d = (long) (p->e - e) * (long) (p->e - e) + (long) (p->n - n) * (long) (p->n - n);
         if (!found || d < best)
         {
            best = d;
            found = p;
         }
      }
      if (best > (long) RANGE * RANGE)
         return NULL;
      return found;
   }
   int pos = 0;
   postcode_t *list = NULL,
      **listn = &list;
   for (int y = 0; y < gridh; y++)
   {
      int bn = minn / GRID * GRID + y * GRID,
         tn = bn + GRID - 1;
      warnx ("%07dN %.2d%%", bn, 100 * y / gridh);
      for (int x = 0; x < gridw; x++)
      {
         int le = mine / GRID * GRID + x * GRID,
            re = le + GRID - 1;
         void add (postcode_t * p)
         {
            warnx ("Add %7d %8s %06d/%07d for %06d/%07d", pos, p->postcode, p->e, p->n, le, bn);
            postcode_t *new = malloc (sizeof (*new));
            new->e = p->e;
            new->n = p->n;
            new->postcode = p->postcode;
            new->next = NULL;
            *listn = new;
            listn = &new->next;
            pos++;
         }
         writeu32 (pos);
         postcode_t *f1 = find (le, bn);
         if (f1 && (f1->n < bn || f1->n > tn || f1->e < le || f1->e > re))
            add (f1);
         postcode_t *f2 = find (re, bn);
         if (f2 && f2 != f1 && (f2->n < bn || f2->n > tn || f2->e < le || f2->e > re))
            add (f2);
         postcode_t *f3 = find (le, tn);
         if (f3 && f3 != f2 && f3 != f1 && (f3->n < bn || f3->n > tn || f3->e < le || f3->e > re))
            add (f3);
         postcode_t *f4 = find (re, tn);
         if (f4 && f4 != f3 && f4 != f2 && f4 != f1 && (f4->n < bn || f4->n > tn || f4->e < le || f4->e > re))
            add (f4);
         for (postcode_t * f = postcodes; f; f = f->next)
            if (f->n >= bn && f->n <= tn && f->e >= le && f->e <= re)
               add (f);
      }
   }
   for (postcode_t * p = list; p; p = p->next)
   {
      writeu32 (p->e);
      writeu32 (p->n);
      char pc[8] = { 0 };
      strncpy (pc, p->postcode, sizeof (pc));
      fwrite (pc, 8, 1, stdout);
   }
}
