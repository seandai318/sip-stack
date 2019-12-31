#ifndef _SIP_HDR_DATE_H
#define _SIP_HDR_DATE_H

#include "osMBuf.h"
#include "osPL.h"


typedef struct sipHdrDate {
	osPointerLen_t wkday;
	osPointerLen_t day;
	osPointerLen_t month;
	osPointerLen_t year;
	osPointerLen_t hour;
	osPointerLen_t min;
	osPointerLen_t sec;
} sipHdrDate_t;


osStatus_e sipParserHdr_Date(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrDate_t* pDate);


#endif
