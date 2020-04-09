#ifndef _SIP_TRANS_HELPER_H
#define _SIP_TRANS_HELPER_H

#include "sipTransHelper.h"


osStatus_e sipTransHelper_reqlineReplaceAndCopy(osMBuf_t* pDestMsg, osMBuf_t* pSipSrcReq, osPointerLen_t* pMethod);
osStatus_e sipTransHelper_cSeqReplaceAndCopy(osMBuf_t* pDestMsg, sipRawHdr_t* pCSeqRawHdr, osPointerLen_t* pMethod);


#endif
