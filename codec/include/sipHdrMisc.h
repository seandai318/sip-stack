#ifndef _SIP_HDR_MISC_H
#define _SIP_HDR_MISC_H

#include "osPL.h"
#include "osMBuf.h"
#include "osTypes.h"
#include "sipHdrTypes.h"


#define SIP_MAX_CSEQ_NUM	2147483648


osStatus_e sipParserHdr_str(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrStr_t* pCallid);
//this covers the folllowing headers: Expires, Session-Expires, Content-Length, Max-Forwards
osStatus_e sipParserHdr_lenTime(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrInt_t* pLenTime);
osStatus_e sipParserHdr_cSeq(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrCSeq_t* pCSeq);
osStatus_e sipParserHdr_intParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrIntParam_t* pIntParam);
/* covers header Allow, Supported, Allow-Events */
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
//if pCallId=NULL, after adding call-id to the pSipBuf, the call-id memory will be deleted, otherwise, call-id will be stored in pCallId->p for user to use
osStatus_e sipHdrCallId_createAndAdd(osMBuf_t* pSipBuf, osPointerLen_t* pCallId);


#endif

