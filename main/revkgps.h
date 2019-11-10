// RevKGPS Protocol

#define VERSION 0x2A
#define TSCALE  100             // Per second
#define TPART   "%02u"
#define DSCALE  100000          // Per angle minute
#define	ASCALE	10		// Per metre
#define	HSCALE	10		// HDOP scale
#define	ALT_BALLOON	2.5	// m per unit in balloon mode
#define	ALT_FLIGHT	1.0	// m per unit in flight mode

#define TAGF_PAD		0x00	// Padding
#define	TAGF_BALLOON		0x01	// Alt is in balloon mode (ALT_BALLOON metres per unit)
#define	TAGF_FLIGHT		0x02	// Alt is in flight mode (ALT_FLIGHT metres per unit)
#define	TAGF_PERIOD		0x40	// Period covered
#define	TAGF_MARGIN		0x41	// Defines maximum distance of discarded packets (cm)
#define	TAGF_FIRST		0x60	// defines first UTC reference available

#define	TAGT_PAD		0x00	// Padding
#define	TAGT_FIX		0x01	// Fix request
#define	TAGT_RESEND		0x60	// Resend from specified UTC reference

#define	TAGF_FIX		0x80	// Base fix
#define	TAGF_FIX_ALT		0x01	// Alt included
#define	TAGF_FIX_SATS		0x02	// Sats and DGPS status (top bit)
#define	TAGF_FIX_HDOP		0x04	// HDOP (HSCALE)
const uint8_t tagf_fix[7]={2,1,1,0,0,0,0};	// length of tag fix bits

