#ifndef _SIP_HDR_MISC_H
#define _SIP_HDR_MISC_H

#include "osPL.h"
#include "osMBuf.h"
#include "osTypes.h"

#define SIP_MAX_CSEQ_NUM	2147483648
typedef struct sipHdrCSeq {
	osPointerLen_t seq;		//todo, to be replaced by seqNum
	uint32_t seqNum;
	osPointerLen_t method;
} sipHdrCSeq_t;


typedef uint32_t sipHdrLenTime_t;


osStatus_e sipParserHdr_str(osMBuf_t* pSipMsg, size_t hdrEndPos, osPointerLen_t* pCallid);
//this covers the folllowing headers: Expires, Session-Expires, Content-Length, Max-Forwards
osStatus_e sipParserHdr_lenTime(osMBuf_t* pSipMsg, size_t hdrEndPos, uint32_t* pLenTime);
osStatus_e sipParserHdr_cSeq(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrCSeq_t* pCSeq);
/* covers header Allow, Supported, Allow-Events */
osStatus_e sipParserHdr_nameList(osMBuf_t* pSipMsg, size_t hdrEndPos, bool isCaps, osList_t* pList);
osStatus_e sipHdrPL_encodeByName(osMBuf_t* pSipBuf, void* pl, void* other);
//osStatus_e sipHdrCallId_encode(osMBuf_t* pSipBuf, osPointerLen_t* pCallId);
osStatus_e sipHdrCSeq_encode(osMBuf_t* pSipBuf, void* pCSeq, void* pReqTypeDT);
osStatus_e sipHdrLenTime_encode(osMBuf_t* pSipBuf, void* pLenTimeDT, void* pData);
osStatus_e sipHdrLenTime_create(void* pLenTimeDT, void* pHdrData, void* pHdrCodeDT);
osStatus_e sipHdrPL_encode(osMBuf_t* pSipBuf, void* pl, void* other);
//if pHdrInDecoded != NULL, use pExtraInfo as a hdrType to get the PL info from pHdrInDecoded
//otherwise, pExtraInfo is a null terminated string
osStatus_e sipHdrPL_create(void* pl, void* pHdrInDecoded, void* pExtraInfo);
osStatus_e sipHdrCallId_createCallId(osPointerLen_t* pl);


#endif

