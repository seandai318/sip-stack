#ifndef _SIP_HDR_VIA_H
#define _SIP_HDR_VIA_H


#include "osList.h"
#include "osPL.h"
#include "sipHostport.h"
#include "osMBuf.h"

#include "sipHdrTypes.h"

#if 0	
typedef struct sipHdrViaElement {
	osPointerLen_t sentProtocol[3];
    sipHostport_t hostport;
	osList_t viaParamList;	//for decode, the element includes all parameters, including branch, 
							//for encode, the branch may or may not included in the list, depends 
							//if pBranch function argument in sipHdrVia_encode is NULL
							//the element data structure for both encode and decode is sipHdrParamNameValue_t.
} sipHdrViaElement_t;
#endif
#if 0
typedef struct sipHdrVia {
    osList_t viaList;   //each element contains a sipHdrViaElement_t data;
} sipHdrVia_t;
#endif
	

osStatus_e sipParserHdr_viaElement(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrVia_t* pVia);
osStatus_e sipParserHdr_via(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMultiVia_t* pVia);
osStatus_e sipHdrVia_encode(osMBuf_t* pSipBuf, void* pViaDT, void* pBranchDT);
osStatus_e sipHdrVia_create(void* pViaDT, void* pBranchId, void* other);

osStatus_e sipHdrVia_createEncode(osMBuf_t* pSipBuf, osPointerLen_t* pBranchId, char* pExtraInfo);
//osStatus_e sipHdrVia_createTopViaModifyInfo(sipHdrModifyInfo_t* pViaModify, osPointer_t* pBranchId, char* pExtraInfo);
osStatus_e sipHdrVia_generateBranchId(osPointerLen_t* pBranch, char* pExtraInfo);
//sipHdrViaElement_t* sipHdrVia_getTopBottomVia(sipHdrVia_t* pHdrViaList, bool isTop);
osPointerLen_t* sipHdrVia_getTopBranchId(sipHdrMultiVia_t* pHdrVia);

void* sipHdrMultiVia_alloc();


#endif
