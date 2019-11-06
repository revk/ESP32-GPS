// RevKGPS Protocol

#define VERSION 0x2A
#define TSCALE  100             // Per second
#define TPART   "%02u"
#define DSCALE  100000          // Per angle minute

#define TAGF_PAD		0x00	// Padding
#define	TAGF_MARGIN		0x40	// Defines maximum distance of discarded packets (cm)
#define	TAGF_FIRST		0x60	// defines first UTC reference available

#define	TAGT_PAD		0x00	// Padding
#define	TAGT_FIX		0x01	// Fix request
#define	TAGT_RESEND		0x60	// Resend from specified UTC reference

#define	TAGF_FIX		0x80	// Base fix
#define	TAGF_FIX_ALT		0x01	// Alt included
const uint8_t tagf_fix[7]={2,0,0,0,0,0,0};	// length of tag fix bits

