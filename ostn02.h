// OSGM02 algorithm
// Conversion for OSGB grid reference

int OSTN02_EN2LL (long double easting, long double northing, long double *lat, long double *lon, long double *alt);     // return <=0 for error
int OSTN02_LL2EN (long double lat, long double lon, long double *easting, long double *northing,long double*height);       // return <=0 for error;
