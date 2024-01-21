// Make postcode database

#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <ctype.h>
#include <err.h>

#define	STEP	10              // Edge scan steps
#define	GRID	1000            // Grid squares
#define	RANGE	1000            // Max to postcode
#define	MAGIC	20240121        // File magic number

// Postcode file format
// Initial, LE 32 bit quantities
// - MAGIC
// - GRID
// - MINE/GRID
// - MINN/GRID
// - GRIDWIDTH
// - GRIDHEIGHT
// Then for each row from MINN up, and each column from MINE across, LE 32 bit number for record, plus 1
// - Note read next record for next grid square start, hence plus 1
// Records (all in grid and nearest entries that are not in grid))
// - E (LE 32 bit)
// - N (LE 32 bit)
// - postcode (8 bytes, null terminated, no space)

int debug = 0;

typedef struct postcode_s postcode_t;
struct postcode_s
{
   postcode_t *next;
   const char *postcode;
   int e,
     n;
};

// Blocks to speed up search
#define	BLOCK	1000
#define	BLOCKW	(700000/BLOCK)
#define	BLOCKH	(1300000/BLOCK)
postcode_t *postcodes[BLOCKW][BLOCKH] = { 0 };

int
main (int argc, const char *argv[])
{
   // Load CSVs
   if (argc <= 1)
      errx (1, "List CSV files");
   int mine = 0,
      maxe = 0,
      minn = 0,
      maxn = 0,
      total = 0;
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
         int x = e / BLOCK,
            y = n / BLOCK;
         if (l >= 6 && postcode[l - 4] == ' ' && e && n && x < BLOCKW && y < BLOCKH)
         {
            total++;
            memmove (postcode + l - 4, postcode + l - 3, 4);    // Remove space
            postcode_t *new = malloc (sizeof (*new));
            new->postcode = strdup (postcode);
            new->e = e;
            new->n = n;
            new->next = postcodes[x][y];
            postcodes[x][y] = new;
            if (!mine || mine > e)
               mine = e;
            if (maxe < e)
               maxe = e;
            if (!minn || minn > n)
               minn = n;
            if (maxn < n)
               maxn = n;
         } else
            warnx ("%s %d/%d %d/%d %d/%d", postcode, e, n, x, y, BLOCKW, BLOCKH);
      }
      fclose (i);
      free (line);
   }
   warnx ("E %d-%d N %d-%d total=%d", mine, maxe, minn, maxn, total);
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
   int pos = 0;
   postcode_t *list = NULL,
      *liste = NULL;
   int maxcount = 0;
   for (int y = 0; y < gridh; y++)
   {
      int bn = minn / GRID * GRID + y * GRID,
         tn = bn + GRID - 1;
      for (int x = 0; x < gridw; x++)
      {
         writeu32 (pos);
         int le = mine / GRID * GRID + x * GRID,
            re = le + GRID - 1;
         int count = 0;
         postcode_t *dedup = liste;
         void add (postcode_t * p, int e, int n)
         {
            if (!p)
               return;
            for (postcode_t * d = dedup; d; d = d->next)
               if (d->postcode == p->postcode)
                  return;
            //warnx ("Add %7d %-7s %06d/%07d for %06d/%07d %d", pos, p->postcode, p->e, p->n, e, n, count);
            postcode_t *new = malloc (sizeof (*new));
            new->e = p->e;
            new->n = p->n;
            new->postcode = p->postcode;
            new->next = NULL;
            if (!liste)
               dedup = list = new;
            else
               liste->next = new;
            liste = new;
            pos++;
            count++;
         }
         postcode_t *find (int e, int n)
         {
            long best = 0;
            postcode_t *found = NULL;
            for (int x = e / BLOCK - 1; x <= e / BLOCK + 1; x++)
            {
               if (x < 0 || x >= BLOCKW)
                  continue;
               for (int y = n / BLOCK - 1; y <= n / BLOCK + 1; y++)
               {
                  if (y < 0 || y >= BLOCKH)
                     continue;
                  for (postcode_t * p = postcodes[x][y]; p; p = p->next)
                  {
                     long d = (long) (p->e - e) * (long) (p->e - e) + (long) (p->n - n) * (long) (p->n - n);
                     if (!found || d < best)
                     {
                        best = d;
                        found = p;
                     }
                  }
               }
            }
            if (best > (long) RANGE * RANGE)
               return NULL;
            add (found, e, n);
            return found;
         }
         void edge (postcode_t * p1, int e1, int n1, postcode_t * p2, int e2, int n2)
         {
            if (p1 == p2)
               return;
            if ((long) (e1 - e2) * (long) (e1 - e2) + (long) (n1 - n2) * (long) (n1 - n2) < STEP * STEP)
               return;
            int em = (e1 + e2) / 2,
               nm = (n1 + n2) / 2;
            postcode_t *m = find (em, nm);
            edge (p1, e1, n1, m, em, nm);
            edge (m, em, nm, p2, e2, n2);
         }
         postcode_t *lb = find (le, bn);        // Corners
         postcode_t *rb = find (re, bn);
         postcode_t *lt = find (le, tn);
         postcode_t *rt = find (re, tn);
         edge (lb, le, bn, rb, re, bn);
         edge (lt, le, tn, rt, re, tn);
         edge (lb, le, bn, lt, le, tn);
         edge (rb, re, bn, rt, re, tn);
         // In grid
         for (postcode_t * f = postcodes[le / BLOCK][bn / BLOCK]; f; f = f->next)
            if (f->n >= bn && f->n <= tn && f->e >= le && f->e <= re)
               add (f, (le + re) / 2, (bn + tn) / 2);
         if (count > maxcount)
            maxcount = count;
      }
      warnx ("%07dN %.2d%% %d", bn, 100 * y / gridh, pos);
   }
   writeu32 (pos);
   warnx ("Max per grid %d", maxcount);
   for (postcode_t * p = list; p; p = p->next)
   {
      writeu32 (p->e);
      writeu32 (p->n);
      char pc[8] = { 0 };
      strncpy (pc, p->postcode, sizeof (pc));
      fwrite (pc, 8, 1, stdout);
   }
}
