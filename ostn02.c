// OSGM02 algorithm
// Conversion for OSGB grid reference

//#define	DEBUG

#include <stdio.h>
#include <math.h>
#define	ETRS89

#include "OSTN02_OSGM02_GB.h"

#define	pi	3.1415926535897932384626433832795028L
#define secl(x) (1.0L/cosl(x))

#ifdef	OSGB36
static const long double a = 6377563.396L;      // semi-major-axis
static const long double b = 6356256.910L;      // semi-minor-axis
#endif
#ifdef	ETRS89
static const long double a = 6378137.000L;      // semi-major-axis
static const long double b = 6356752.3141L;     // semi-minor-axis
#endif
static const long double F0 = 0.9996012717L;    // scale factor on central meridian
static const long double phi0 = 49.0L * pi / 180.0L;    // True origin N
static const long double lambda0 = -2.0L * pi / 180.0L; // True origin W
static const long double E0 = 400000.0L;        // Easting of true origin
static const long double N0 = -100000.0L;       // Northing of true origin

static int
mapping (long double E, long double N, long double *se, long double *sn, long double *sg)
{
   if (se)
      *se = 0;
   if (sn)
      *sn = 0;
   if (sg)
      *sg = 0;
   if (E < 0 || E >= 700000 || N < 0 || N >= 1250000)
      return -1;                // out of range
   int index = ((int) E / 1000) + ((int) N / 1000) * 701;
   int datum = OSTN02_d[index];
   if (!datum)
      return datum;
   int se0 = OSTN02_e[index];
   int se1 = OSTN02_e[index + 1];
   int se2 = OSTN02_e[index + 702];
   int se3 = OSTN02_e[index + 701];
   int sn0 = OSTN02_n[index];
   int sn1 = OSTN02_n[index + 1];
   int sn2 = OSTN02_n[index + 702];
   int sn3 = OSTN02_n[index + 701];
   int sg0 = OSTN02_g[index];
   int sg1 = OSTN02_g[index + 1];
   int sg2 = OSTN02_g[index + 702];
   int sg3 = OSTN02_g[index + 701];
   int x0 = (int) E / 1000 * 1000;
   int y0 = (int) N / 1000 * 1000;
   long double dx = E - x0;
   long double dy = N - y0;
   long double t = dx / 1000;
   long double u = dy / 1000;
   if (se)
      *se = ((1 - t) * (1 - u) * se0 + t * (1 - u) * se1 + t * u * se2 + (1 - t) * u * se3) / 1000;
   if (sn)
      *sn = ((1 - t) * (1 - u) * sn0 + t * (1 - u) * sn1 + t * u * sn2 + (1 - t) * u * sn3) / 1000;
   if (sg)
      *sg = ((1 - t) * (1 - u) * sg0 + t * (1 - u) * sg1 + t * u * sg2 + (1 - t) * u * sg3) / 1000;
   return datum;
}

int
OSTN02_EN2LL (long double E, long double N, long double *lat, long double *lon, long double *alt)
{                               // convert EN to LL, returns datum (0 or -1 is invalid)
   if (lat)
      *lat = 0;
   if (lon)
      *lon = 0;
   if (alt)
      *alt = 0;
#ifdef	DEBUG
   printf ("E    %30.15Lf\n", E);
   printf ("N    %30.15Lf\n", N);
#endif
   long double se,
     sn;
   int datum = mapping (E, N, &se, &sn, alt);
   if (datum < 0)
      return datum;
   datum = mapping (E - se, N - sn, &se, &sn, alt);
   if (datum < 0)
      return datum;
   E -= se;
   N -= sn;
   const long double e2 = (a * a - b * b) / (a * a);    // ellipsoid squared eccentricity
   const long double n = (a - b) / (a + b);     // B2
   const long double n2 = n * n;
   const long double n3 = n2 * n;
#ifdef	DEBUG
   printf ("E'   %30.15Lf\n", E);
   printf ("N'   %30.15Lf\n", N);
#endif
   long double phi1 = (N - N0) / (a * F0) + phi0;
   long double M;
   int i = 10;
   while (i--)
   {
      M = b * F0 * ((1.0L + n + (5.0L / 4.0L) * n2 + (5.0L / 4.0L) * n3) * (phi1 - phi0)
                    - (3.0L * n + 3.0L * n2 + (21.0L / 8.0L) * n3) * sinl (phi1 - phi0) * cosl (phi1 + phi0)
                    + ((15.0L / 8.0L) * n2 + (15.0L / 8.0L) * n3) * sinl (2.0L * (phi1 - phi0)) * cosl (2.0L * (phi1 + phi0))
                    - (35.0L / 24.0L) * n3 * sinl (3.0L * (phi1 - phi0)) * cosl (3.0L * (phi1 + phi0)));
#ifdef DEBUG
      printf ("φ'   %30.15Lf\n", phi1);
      printf ("M    %30.15Lf\n", M);
#endif
      if (fabsl (N - N0 - M) <= 0.0001)
         break;
      phi1 += (N - N0 - M) / (a * F0);
   }
   long double v = a * F0 * powl (1.0L - e2 * powl (sinl (phi1), 2.0L), -0.5L); // B3
   long double rho = a * F0 * (1.0L - e2) * powl (1 - e2 * powl (sinl (phi1), 2.0L), -1.5L);    // B4
   long double eta2 = (v / rho) - 1.0L; // B5
   long double VII = tanl (phi1) / (2.0L * rho * v);
   long double VIII =
      tanl (phi1) / (24.0L * rho * powl (v, 3.0L)) * (5.0L + 3.0L * powl (tanl (phi1), 2.0L) + eta2 -
                                                      9.0L * powl (tanl (phi1), 2.0L) * eta2);
   long double IX =
      tanl (phi1) / (720.0L * rho * powl (v, 5.0L)) * (61.0L + 90.0L * powl (tanl (phi1), 2.0L) + 45.0L * powl (tanl (phi1), 4.0L));
   long double X = secl (phi1) / v;
   long double XI = secl (phi1) / (6 * powl (v, 3.0L)) * (v / rho + 2.0L * powl (tanl (phi1), 2.0L));
   long double XII =
      secl (phi1) / (120.0L * powl (v, 5.0L)) * (5.0L + 28.0L * powl (tanl (phi1), 2.0L) + 24.0L * powl (tanl (phi1), 4.0));
   long double XIIA =
      secl (phi1) / (5040.0L * powl (v, 7.0L)) * (61.0L + 662.0L * powl (tanl (phi1), 2.0) + 1320.0L * powl (tanl (phi1), 4.0L) +
                                                  720.0L * powl (tanl (phi1), 6.0L));
   long double phi = phi1 - VII * powl (E - E0, 2.0L) + VIII * powl (E - E0, 4.0L) - IX * powl (E - E0, 6.0L);
   long double lambda = lambda0 + X * (E - E0) - XI * powl (E - E0, 3.0L) + XII * powl (E - E0, 5.0L) - XIIA * powl (E - E0, 7.0L);

#ifdef DEBUG
   printf ("v    %30.15Lf\n", v);
   printf ("ρ    %30.15Lf\n", rho);
   printf ("η²   %30.15Lf\n", eta2);
   printf ("VII  %30.15Lg\n", VII);
   printf ("VIII %30.15Lg\n", VIII);
   printf ("IX   %30.15Lg\n", IX);
   printf ("X    %30.15Lg\n", X);
   printf ("XI   %30.15Lg\n", XI);
   printf ("XII  %30.15Lg\n", XII);
   printf ("XIIA %30.15Lg\n", XIIA);
   printf ("φ    %30.15Lg\n", phi);
   printf ("λ    %30.15Lg\n", lambda);
#endif
   if (lat)
      *lat = phi * 180.0L / pi;
   if (lon)
      *lon = lambda * 180.0L / pi;
   return datum;
}

int
OSTN02_LL2EN (long double lat, long double lon, long double *eastingp, long double *northingp, long double *altp)
{                               // convert LL to EN, returns datum (0 or -1 invalid)
   if (eastingp)
      *eastingp = 0;
   if (northingp)
      *northingp = 0;
   if (altp)
      *altp = 0;
   long double phi = lat * pi / 180.0L;
   long double lambda = lon * pi / 180.0L;
   const long double e2 = (a * a - b * b) / (a * a);    // ellipsoid squared eccentricity
   const long double n = (a - b) / (a + b);     // B2
   const long double n2 = n * n;
   const long double n3 = n2 * n;
   long double v = a * F0 * powl (1.0L - e2 * powl (sinl (phi), 2.0L), -0.5L);  // B3
   long double rho = a * F0 * (1.0L - e2) * powl (1 - e2 * powl (sinl (phi), 2.0L), -1.5L);     // B4
   long double eta2 = (v / rho) - 1.0L; // B5
   long double M = b * F0 * ((1.0L + n + (5.0L / 4.0L) * n2 + (5.0L / 4.0L) * n3) * (phi - phi0)
                             - (3.0L * n + 3.0L * n2 + (21.0L / 8.0L) * n3) * sinl (phi - phi0) * cosl (phi + phi0)
                             + ((15.0L / 8.0L) * n2 + (15.0L / 8.0L) * n3) * sinl (2.0L * (phi - phi0)) * cosl (2.0L * (phi + phi0))
                             - (35.0L / 24.0L) * n3 * sinl (3.0L * (phi - phi0)) * cosl (3.0L * (phi + phi0)));
   long double I = M + N0;
   long double II = v / 2.0L * sinl (phi) * cosl (phi);
   long double III = v / 24.0L * sinl (phi) * powl (cosl (phi), 3.0L) * (5.0L - powl (tanl (phi), 2.0L) + 9.0L * eta2);
   long double IIIA =
      v / 720.0L * sinl (phi) * powl (cosl (phi), 5.0L) * (61.0L - 58.0L * powl (tanl (phi), 2.0L) + powl (tanl (phi), 4.0L));
   long double IV = v * cosl (phi);
   long double V = v / 6.0L * powl (cosl (phi), 3.0L) * (v / rho - powl (tanl (phi), 2.0L));
   long double VI = v / 120.0L * powl (cosl (phi), 5.0L) * (5.0L - 18.0L * powl (tanl (phi), 2.0L)
                                                            + powl (tanl (phi), 4.0L) + 14.0L * eta2 - 58.0L * powl (tanl (phi),
                                                                                                                     2.0L) * eta2);
   long double N =
      I + II * powl (lambda - lambda0, 2.0L) + III * powl (lambda - lambda0, 4.0L) + IIIA * powl (lambda - lambda0, 6.0L);
   long double E = E0 + IV * (lambda - lambda0) + V * powl (lambda - lambda0, 3.0L) + VI * powl (lambda - lambda0, 5.0L);

#ifdef	DEBUG
   printf ("N₀   %30.15Lf\n", (long double) N0);
   printf ("E₀   %30.15Lf\n", (long double) E0);
   printf ("F₀   %30.15Lf\n", (long double) F0);
   printf ("φ₀   %30.15Lf\n", phi0);
   printf ("λ₀   %30.15Lf\n", lambda0);
   printf ("e²   %30.15Lf\n", e2);
   printf ("n    %30.15Lf\n", n);
   printf ("n²   %30.15Lf\n", n2);
   printf ("n³   %30.15Lf\n", n3);
   printf ("φ    %30.15Lf (%2.15Lf)\n", lat, phi);
   printf ("λ    %30.15Lf (%2.15Lf)\n", lon, lambda);
   printf ("v    %30.15Lf\n", v);
   printf ("ρ    %30.15Lf\n", rho);
   printf ("η²   %30.15Lf\n", eta2);
   printf ("M    %30.15Lf\n", M);
   printf ("I    %30.15Lf\n", I);
   printf ("II   %30.15Lf\n", II);
   printf ("III  %30.15Lf\n", III);
   printf ("IIIA %30.15Lf\n", IIIA);
   printf ("IV   %30.15Lf\n", IV);
   printf ("V    %30.15Lf\n", V);
   printf ("VI   %30.15Lf\n", VI);
   printf ("E    %30.15Lf\n", E);
   printf ("N    %30.15Lf\n", N);
#endif
   long double se,
     sn;
   int datum = mapping (E, N, &se, &sn, altp);
   if (datum < 0)
      return datum;
   if (eastingp)
      *eastingp = E + se;
   if (northingp)
      *northingp = N + sn;
   return datum;
}
