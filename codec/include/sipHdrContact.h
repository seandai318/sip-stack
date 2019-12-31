#ifndef _SIP_HDR_CONTACT_H
#define _SIP_HDR_CONTACT_H

#include "osTypes.h"
#include "osMBuf.h"
#include "osList.h"
#include "sipGenericNameParam.h"




typedef struct sipHdr_contact {
	bool isStar;
	osList_t contactList;	//each element contains a sipHdrGenericNameParam_t data;
} sipHdrContact_t;

#define sipHdrContact_build		sipHdrGenericNameParam_build
#define	sipHdrContact_addParam	sipHdrGenericNameParam_addParam


osStatus_e sipParserHdr_contact(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrContact_t* pNameParam);
osStatus_e sipHdrContact_encode(osMBuf_t* pSipBuf, void* pContact, void* other);
void sipHdrContact_cleanup(void* data);
void* sipHdrContact_alloc();


#endif
