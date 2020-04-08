#ifndef _SIP_HDR_VIA_H
#define _SIP_HDR_VIA_H


#include "osList.h"
#include "osPL.h"
#include "sipHostport.h"
#include "osMBuf.h"

#include "sipHdrTypes.h"
#include "sipUriparam.h"
#include "sipMsgRequest.h"



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
//extract the peer IP and port from a via header.  If there is received, ip in the received will be used
osStatus_e sipHdrVia_getPeerTransport(sipHdrViaDecoded_t* pVia, sipHostport_t* pHostPort, sipTransport_e* pTransportProtocol);

//extract the peer IP and port from raw via headers, peerViaIdx=0(top), 1(secondly), and SIP_HDR_BOTTOM(bottom)
osStatus_e sipHdrVia_getPeerTransportFromRaw(sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint8_t peerViaIdx, sipHostport_t* pHostPort, sipTransport_e* pTpProtocol);

//this is to encode via for a response, instead of copying all rawVia, add received and rport for the top via if the received top via has rport
osStatus_e sipHdrVia_rspEncode(osMBuf_t* pSipBuf, sipHdrMultiVia_t* pTopMultiVia, sipMsgDecodedRawHdr_t* pRawVia, sipHostport_t* pPeer);

void* sipHdrMultiVia_alloc();


#endif
