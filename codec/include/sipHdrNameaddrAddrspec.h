#ifndef _SIP_HDR_NAMEADDR_ADDRSPEC_H
#define _SIP_HDR_NAMEADDR_ADDRSPEC_H

#include "osList.h"
#include "osMemory.h"
#include "osMBuf.h"


//PPI and PAI can use this data structure
typedef struct sipHdr_NameaddrAddrspec {
    osList_t addrList;   //each element contains a sipHdrGenericNameParam_t data, osList genericParam shall contain no element
} sipHdrNameaddrAddrspec_t;


osStatus_e sipParserHdr_nameaddrAddrSpec(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrNameaddrAddrspec_t* pNameParam);
void sipHdrNameaddrAddrspec_cleanup(void* data);
void* sipHdrNameaddrAddrspec_alloc();
void sipHdrNameaddrAddrspec_dealloc(void* data);


#endif
