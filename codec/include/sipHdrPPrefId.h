#ifndef _SIP_HDR_PPREFID_H
#define _SIP_HDR_PPREFID_H

#include "osList.h"
#include "sipGenericNameParam.h"



typedef struct sipHdr_ppi {
    osList_t ppiList;   //each element contains a sipHdrGenericNameParam_t data.  The genericParam shall bo null
} sipHdrPpi_t;




osStatus_e sipParserHdr_ppi(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrPpi_t* pPpi);
void sipHdrPpi_cleanup(void* data);
void* sipHdrPpi_alloc();


#endif
