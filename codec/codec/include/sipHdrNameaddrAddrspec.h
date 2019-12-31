#ifndef _SIP_HDR_NAMEADDR_ADDRSPEC_H
#define _SIP_HDR_NAMEADDR_ADDRSPEC_H

#include "osList.h"
#include "osMemory.h"
#include "osMBuf.h"
#include "sipHdrTypes.h"


//PPI and PAI can use this data structure, generic param shall contain no content
#define  sipHdrNameaddrAddrspec_t sipHdrMultiGenericNameParam_t
#define sipHdrNameaddrAddrspec_cleanup sipHdrMultiGenericNameParam_cleanup

osStatus_e sipParserHdr_nameaddrAddrSpec(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrNameaddrAddrspec_t* pNameParam);
void sipHdrNameaddrAddrspec_cleanup(void* data);
void sipHdrNameaddrAddrspec_dealloc(void* data);


#endif
