/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTU.h
 ********************************************************/

#ifndef _SIP_TU_H
#define _SIP_TU_H


#include <netinet/in.h>

#include "osTypes.h"
#include "osMBuf.h"
#include "osPL.h"

#include "dnsResolverIntf.h"

#include "sipHeader.h"
#include "sipMsgRequest.h"
#include "sipHdrMisc.h"

#include "sipTransIntf.h"



typedef enum {
	SIPTU_RAW_VALUE_TYPE_INT,
	SIPTU_RAW_VALUE_TYPE_STR_PTR,
	SIPTU_RAW_VALUE_TYPE_STR_OSPL,
	SIPTU_RAW_VALUE_TYPE_STR_SIPPL,
} sipTuHdrRawValueType_e;


//the whole value string for a hdr entry in a sip message
typedef struct {
//    bool isIntValue;
	sipTuHdrRawValueType_e rawValueType;
    union {
		osPointerLen_t* strValuePtr;	//rawValueType = SIPTU_RAW_VALUE_TYPE_STR_PTR
        osPointerLen_t strValue;		//rawValueType = SIPTU_RAW_VALUE_TYPE_STR_OSPL
		sipPointerLen_t sipPLValue;		//rawValueType = SIPTU_RAW_VALUE_TYPE_STR_SIPPL	
        int intValue;					//rawValueType = SIPTU_RAW_VALUE_TYPE_INT
    };
} sipTuHdrRawValue_t;


typedef struct {
    sipHdrName_e nameCode;
	sipTuHdrRawValue_t value;
} sipTuHdrRawValueStr_t;


//if there are multiple values for a hdr in a sip message, specify whether the action is towards to the top value or the whole values for the hdr in a sip message
typedef struct sipHdrRawValueId {
    sipHdrName_e nameCode;
    bool isTopOnly;     //if a hdr has multiple value, or a hdr has multipl entries, isTopOnly=true will remove only the top hdr value.  Otherwise, all values for the hdr in a sip message will be removed
} sipHdrRawValueId_t;


typedef struct {
	union {
		sipUri_t sipUri;
		osPointerLen_t rawSipUri;
	};
	bool isRaw;		//intentionally put at the bottom, so that earlier used "sipUri_t* pReqlineUri" only needs to change data type from sipUri_t to sipTuUri_t, no need to create sipTuUri_t to reduce the impact.
} sipTuUri_t;


typedef struct {
	bool isSockAddr;
	transportType_e tpType; 
	union {
		transportIpPort_t ipPort;
		struct sockaddr_in sockAddr;
	};
} sipTuAddr_t;


typedef struct {
	osPointerLen_t nextHopRaw;	//hold the nextHop's raw sip URI (may include uri parameters). Note the valid scope of this variable
	sipTuAddr_t nextHop;
} sipTuNextHop_t;


typedef struct {
	uint32_t minRegTime;
	uint32_t maxRegTime;
	uint32_t defaultRegTime;
} sipTuRegTimeConfig_t;



bool sipTu_getBestNextHop(dnsResResponse_t* pRR, bool isDnsResCached, sipTuAddr_t* pNextHop, osPointerLen_t* pQName);
bool sipTu_getBestNextHopByName(osPointerLen_t* pQName, sipTuAddr_t* pNextHop);
bool sipTu_setDestFailure(osPointerLen_t* dest, struct sockaddr_in* destAddr);
bool sipTu_replaceDest(osPointerLen_t* dest, sipTuAddr_t* pNextHop);
void sipTuDest_localSet(osPointerLen_t* pDestName, struct sockaddr_in destAddr, transportType_e tpType);
bool sipTuDest_isQuarantined(osPointerLen_t* pDestName, struct sockaddr_in destAddr);

bool sipTu_getRegExpireFromMsg(sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint32_t* pRegExpire, sipTuRegTimeConfig_t* pRegTimeConfig, sipResponse_e* pRspCode);

//create a UAC request with req line, via, from, to, callId and max forward.  Other headers needs to be added by user as needed
//be noted this function does not include the extra "\r\n" at the last of header, user needs to add it when completing the creation of a SIP message
osMBuf_t* sipTU_uacBuildRequest(sipRequest_e code, sipTuUri_t* pReqlineUri, osPointerLen_t* called, osPointerLen_t* caller, sipTransViaInfo_t* pTransViaId, osPointerLen_t* pCallId, size_t* pViaProtocolPos);


/* build a proxy request based on the received SIP reuest
 * add a via, remove top Route if there is lr, decrease Max-Forwarded by 1
 * can also add/delete extra hdrs.
 * if isProxy=true, all received via headers will be included, otherwise, they will be removed
 */
osMBuf_t* sipTU_b2bBuildRequest(sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool isProxy, sipHdrRawValueId_t* extraDelHdrList, uint8_t delHdrNum, sipTuHdrRawValueStr_t* extraAddHdrList, uint8_t addHdrNum, sipTransViaInfo_t* pTransViaId, sipTuUri_t* pReqlineUri, size_t* pProtocolViaPos, sipPointerLen_t* pCallId);

/* build a proxy response based on the received SIP response
 * remove top via, and hdr in delHdrList, and add hdr in addHdrList
 * the same hdr shall not show 2 or more times in delHdrList, for each hdr in the delHdrList, it can set isTopOnly.
 * the same hdr can show multiple times in addHdrList, for the same hdr, the order of hdr value in addHdrList will maintain in the sip message
 * the same hdr can show both in delHdrList and addHdrList.
 * the delHdrList must at least contain a Via hdr.
 *
 * the delete hdr only deletes the top hdr value.  if a hdr has multiple values, and you want to delete all the hdr appearance in a sip message, you have to use a second pass to specially delete the hdr.
 */
osMBuf_t* sipTU_buildProxyResponse(sipMsgDecodedRawHdr_t* pRespDecodedRaw, sipHdrRawValueId_t* delHdrList, uint8_t delHdrNum, sipTuHdrRawValueStr_t* addHdrList, uint8_t addHdrNum);


/* build a UAS response based on the received sip Request
 * this must be the first function to call to build a sip response buffer
 * if isAddNullContent = TRUE, it is expected that Content-Length is at the end of the response, and this function is the only function to be called to build the response message
 * pReqDecodedRaw contains all parsed raw headers in a sip message
*/
osMBuf_t* sipTU_buildUasResponse(sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipResponse_e rspCode, sipHdrName_e sipHdrArray[], int arraySize, bool isAddNullContent);


/* copy the specified hdrs from the received SIP message to the new SIP message under building
 * when this function is called, sipTU_buildUasResponse must have been called
 * if sipHdrArray = NULL, or arraySize = 0, no hdr will be added, except for the possible NULL Content
 * if isAddNullContent = TRUE, it is expected this is the last function to build the sip response
 * the hdrs in the sipHdrArray are copied from the src to the dest sip message in the order of they are inputed in the sipHdrArray, and are copied location starts from the current pos in pSipBuf
 */
//static osStatus_e sipTU_buildSipMsgCont(osMBuf_t* pSipBufResp, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipHdrName_e sipHdrArray[], int arraySize, bool isAddNullContent)
osStatus_e sipTU_copySipMsgHdr(osMBuf_t* pSipBuf, sipMsgDecodedRawHdr_t* pMsgDecodedRaw, sipHdrName_e sipHdrArray[], int arraySize, bool isAddNullContent);


/* modify a SIP message.  the existing SIP message will be deleted.
 * return the modified pSipMsgBuf (if the function is executed without error) or the original pSipMsgBuf (if there is error in the execution of the function)
 */
osMBuf_t* sipTU_modifySipMsgHdr(osMBuf_t* pSipBuf, sipHdrRawValueId_t* delHdrList, uint8_t delHdrNum, sipTuHdrRawValueStr_t* addHdrList, uint8_t addHdrNum);


/* add a sip header to the current pos of a SIP message under building. 
 * pDecodedHdrValue is the decoded hdr value data structure used as an  input for the encoding, it may be unit32_t/char string/a hdr data structure, depending the hdr.
 * extraInfo is the extrainfo for encoding that may be used in some hdr.  consult a particular hdr's encoding for the exact meaning of extraInfo
 */
osStatus_e sipTU_addMsgHdr(osMBuf_t* pSipBuf, sipHdrName_e hdrCode, void* pDecodedHdrValue, void* extraInfo);


/* add a contact hdr and expire to the current pos of a SIP message under building based on the received sip message and Expire hdr
 * For Expires, use standalone Expires first if it exists in the SIP REGISTER, otherwise, use expires in contact hdr.
 */
osStatus_e sipTU_addContactHdr(osMBuf_t* pSipBuf, sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint32_t regExpire);

//based on parameter's in the p-served-user or top route, if not find, assume it is orig
osStatus_e sipTU_asGetSescase(sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool* isOrig);

/* extract the AS user from the received SIP message */
osStatus_e sipTU_asGetUser(sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* sipUser, bool isOrigUser, bool isOrigAS);

#if 0
/*
 * branchExtraStr: a string that caller wants to be inserted into branch ID.
 * pParamList: list of sipHdrParamNameValue_t, like: sipHdrParamNameValue_t param1={{"comp", 4}, {"sigcomp", 7}};
 * pParamList: a list of header parameters other than branchId.
 */
osStatus_e sipTU_addOwnVia(osMBuf_t* pMsgBuf, char* branchExtraStr, osList_t* pParamList, osPointerLen_t* pBranchId, osPointerLen_t* pHost, uint32_t* pPort, size_t* pProtocolViaPos);
#endif
/* end the building of a sip message.  This shall be always called as the last method of a sip message building, except when isAddNullContent=true in sipTU_buildUasResponse and sipTU_buildUasRequest
 * isExistContent: true, there is content in the sip message, false: no content in the sip message
 */
osStatus_e sipTU_msgBuildEnd(osMBuf_t* pSipMsg, bool isExistContent);

/* only these req that may involves a session creation/modification/deletion are included.  assume other req are mass through in proxy */
bool sipTU_isProxyCallReq(sipRequest_e reqCode);



#endif
