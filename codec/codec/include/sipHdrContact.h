#ifndef _SIP_HDR_CONTACT_H
#define _SIP_HDR_CONTACT_H

#include "osTypes.h"
#include "osMBuf.h"
#include "osList.h"
#include "sipGenericNameParam.h"
#include "sipHdrTypes.h"


#if 0
typedef struct sipHdr_contact {
	bool isStar;
	uint8_t hdrNum;
	sipHdrGenericNameParam_t* pContact;
	osList_t contactList;	//if there is more than one contact values in a contact hdr entry, starting from the 2nd one, the contact values are added here.  each element contains a sipHdrGenericNameParam_t data;
} sipHdrContact_t;
#endif


#define sipHdrContact_build		sipHdrGenericNameParam_build
#define	sipHdrContact_addParam	sipHdrGenericNameParam_addParam


osStatus_e sipParserHdr_contact(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMultiContact_t* pNameParam);
osStatus_e sipHdrContact_encode(osMBuf_t* pSipBuf, void* pContact, void* other);
//void sipHdrMultiContact_cleanup(void* data);
void* sipHdrMultiContact_alloc();


#endif
