#ifndef _SIP_HEADER_H
#define _SIP_HEADER_H

#include "osTypes.h"
#include "osMBuf.h"
#include "osPL.h"
#include "osList.h"

#include "sipMsgFirstLine.h"


typedef enum sipHdrName {
	SIP_HDR_NONE,
    SIP_HDR_ACCEPT,
    SIP_HDR_ACCEPT_CONTACT,
    SIP_HDR_ACCEPT_ENCODING,
    SIP_HDR_ACCEPT_LANGUAGE,
    SIP_HDR_ACCEPT_RESOURCE_PRIORITY,
    SIP_HDR_ALERT_INFO,
    SIP_HDR_ALLOW,
    SIP_HDR_ALLOW_EVENTS,
    SIP_HDR_ANSWER_MODE,
    SIP_HDR_AUTHENTICATION_INFO,
    SIP_HDR_AUTHORIZATION,
    SIP_HDR_CALL_ID,
    SIP_HDR_CALL_INFO,
    SIP_HDR_CONTACT,
    SIP_HDR_CONTENT_DISPOSITION,
    SIP_HDR_CONTENT_ENCODING,
    SIP_HDR_CONTENT_LANGUAGE,
    SIP_HDR_CONTENT_LENGTH,
    SIP_HDR_CONTENT_TYPE,
    SIP_HDR_CSEQ,
    SIP_HDR_DATE,
    SIP_HDR_ENCRYPTION,
    SIP_HDR_ERROR_INFO,
    SIP_HDR_EVENT,
    SIP_HDR_EXPIRES,
    SIP_HDR_FLOW_TIMER,
    SIP_HDR_FROM,
    SIP_HDR_HIDE,
    SIP_HDR_HISTORY_INFO,
    SIP_HDR_IDENTITY,
    SIP_HDR_IDENTITY_INFO,
    SIP_HDR_IN_REPLY_TO,
    SIP_HDR_JOIN,
    SIP_HDR_MAX_BREADTH,
    SIP_HDR_MAX_FORWARDS,
    SIP_HDR_MIME_VERSION,
    SIP_HDR_MIN_EXPIRES,
    SIP_HDR_MIN_SE,
    SIP_HDR_ORGANIZATION,
    SIP_HDR_P_ACCESS_NETWORK_INFO,
    SIP_HDR_P_ANSWER_STATE,
    SIP_HDR_P_ASSERTED_IDENTITY,
    SIP_HDR_P_ASSOCIATED_URI,
    SIP_HDR_P_CALLED_PARTY_ID,
    SIP_HDR_P_CHARGING_FUNCTION_ADDRESSES,
    SIP_HDR_P_CHARGING_VECTOR,
    SIP_HDR_P_DCS_TRACE_PARTY_ID,
    SIP_HDR_P_DCS_OSPS,
    SIP_HDR_P_DCS_BILLING_INFO,
    SIP_HDR_P_DCS_LAES,
    SIP_HDR_P_DCS_REDIRECT,
    SIP_HDR_P_EARLY_MEDIA,
    SIP_HDR_P_MEDIA_AUTHORIZATION,
    SIP_HDR_P_PREFERRED_IDENTITY,
    SIP_HDR_P_PROFILE_KEY,
    SIP_HDR_P_REFUSED_URI_LIST,
    SIP_HDR_P_SERVED_USER,
    SIP_HDR_P_USER_DATABASE,
    SIP_HDR_P_VISITED_NETWORK_ID,
    SIP_HDR_PATH,
    SIP_HDR_PERMISSION_MISSING,
    SIP_HDR_PRIORITY,
    SIP_HDR_PRIV_ANSWER_MODE,
    SIP_HDR_PRIVACY,
    SIP_HDR_PROXY_AUTHENTICATE,
    SIP_HDR_PROXY_AUTHORIZATION,
    SIP_HDR_PROXY_REQUIRE,
    SIP_HDR_RACK,
    SIP_HDR_REASON,
    SIP_HDR_RECORD_ROUTE,
    SIP_HDR_REFER_SUB,
    SIP_HDR_REFER_TO,
    SIP_HDR_REFERRED_BY,
    SIP_HDR_REJECT_CONTACT,
    SIP_HDR_REPLACES,
    SIP_HDR_REPLY_TO,
    SIP_HDR_REQUEST_DISPOSITION,
    SIP_HDR_REQUIRE,
    SIP_HDR_RESOURCE_PRIORITY,
    SIP_HDR_RESPONSE_KEY,
    SIP_HDR_RETRY_AFTER,
    SIP_HDR_ROUTE,
    SIP_HDR_RSEQ,
    SIP_HDR_SECURITY_CLIENT,
    SIP_HDR_SECURITY_SERVER,
    SIP_HDR_SECURITY_VERIFY,
    SIP_HDR_SERVER,
    SIP_HDR_SERVICE_ROUTE,
    SIP_HDR_SESSION_EXPIRES,
    SIP_HDR_SIP_ETAG,
    SIP_HDR_SIP_IF_MATCH,
    SIP_HDR_SUBJECT,
    SIP_HDR_SUBSCRIPTION_STATE,
    SIP_HDR_SUPPORTED,
    SIP_HDR_TARGET_DIALOG,
    SIP_HDR_TIMESTAMP,
    SIP_HDR_TO,
    SIP_HDR_TRIGGER_CONSENT,
    SIP_HDR_UNSUPPORTED,
    SIP_HDR_USER_AGENT,
    SIP_HDR_VIA,
    SIP_HDR_WARNING,
    SIP_HDR_WWW_AUTHENTICATE,
	SIP_HDR_X,
	SIP_HDR_COUNT,
} sipHdrName_e;


typedef struct sipHdrStr {
    sipHdrName_e nameCode;
	osPointerLen_t value;
} sipHdrStr_t;


/* if a hdr has multiple values, like via: via1,via2\r\n. the example via hdr only has one element of this data structure.
 if a msg contains: via: via1, via2\r\nvia: via3, then there will be 2 elements of this data structure for via, one contains via1 and via2, one contains via3. */ 
typedef struct sipHdrRaw {
    sipHdrName_e nameCode;
//	size_t hdrLen;	//the raw hdr length, from the first char of a header until the \n. =valuePos-namePos+value.l 
    osPointerLen_t name;
    size_t namePos;
    osPointerLen_t value;
    size_t valuePos;
} sipRawHdr_t;


typedef union sipHdrDecoded {
	uint32_t decodedIntValue;	//integer value
	void* decodedValue;			//pointer to header data structure, like PL, hdr specific data structure, etc.
} sipHdrDecoded_u;
	

//The reason osList_t is used because many headers may have multiple values, each has its own header name/entry, like  hdr-a: aa\r\nhdr-a: bb.
typedef struct sipRawHdrList {
	uint8_t rawHdrNum;
    sipRawHdr_t* pRawHdr;       //to do. for now, it is not used.  use it so that for the 1st hdr name, use this one.  Only if more than one hdr name, starting from the 2nd one, use rawHdrListrawHdrList
    osList_t rawHdrList;    //each element contains sipRawHdr_t.  if multiple hdr values shares one hdr name, like hdr-a: a,b,c, all these values share one entry in rawHdrList.
} sipRawHdrList_t;
	

//to-do, decodedHdrList, decodedIntValue, decodedValue, make it a union
typedef struct sipHeaderList {
    sipHdrName_e nameCode;
	sipRawHdrList_t rawHdr;
//    osList_t rawHdrList;		//each element contains sipRawHdr_t.  The same as decodedHdrList, if a hdr name has multiple values, all these values share one entry in rawHdrList.
	osList_t decodedHdrList;	//maybe multiple headers for a sip message, each element contains the data structure of individual header. void*. Be noted that if a sip message contains one header name with multiple values, that header itself has a osList for all these values, and only has one entry for that header name in this list.  For example, a sip message contains the following via: [via: a, b, c\\r\nvia: d], then decodedHdrList will have 2 entries for sipHdrVia_t, the 1st entry contains via hdr value a,b,c, the 2nd entry contains via hdr value d
	uint32_t decodedIntValue;	//a unique header for a sip message, contains a uint32 integer
	void* decodedValue;			//a unique header for a sip message, contains string, etc.
} sipHdrInfo_t;


typedef struct sipMsgDecoded {
	osMBuf_t* sipMsg;
	sipFirstline_t sipMsgFirstLine;
	size_t hdrNum;
	osList_t msgHdrList;		//each element contains sipHdrInfo_t for a header
} sipMsgDecoded_t;


typedef struct sipMsgDecodedRawHdr {
    size_t hdrNum;
	sipRawHdrList_t* msgHdrList[SIP_HDR_COUNT];
//    osList_t msgHdrList;        //each element contains sipRawHdrList_t for a header
} sipMsgDecodedRawHdr_t;



typedef enum {
	SIP_HDR_MODIFY_TYPE_ADD,
	SIP_HDR_MODIFY_TYPE_REMOVE,
	SIP_HDR_MODIFY_TYPE_REPLACE,
} sipHdrModType_e;


typedef enum {
	SIP_HDR_MODIFY_STATUS_HDR_NOT_EXIST,
	SIP_HDR_MODIFY_STATUS_HDR_EXIST,
} sipHdrModStatus_e;


typedef osStatus_e (*sipHdrCreate_h) (void* pHdr, void* hdrDataInput, void* otherInput);
typedef osStatus_e (*sipHdrEncode_h) (osMBuf_t* pSipBuf, void* hdr, void* other);


typedef struct sipHdrNmT {
	sipHdrName_e hdrCode;
	sipHdrModType_e modType;
} sipHdrNmT_t;


typedef struct sipHdrModifyInfo {
    sipHdrNmT_t nmt;
//	uint8_t idx;
    sipHdrModStatus_e modStatus;    //this is only used when modeType==SIP_HDR_MODIFY_TYPE_REMOVE to tell if the incomping sip req contains the hdr that is requested to be removed
    size_t origHdrPos;  //this field is overloaded. first it indicates the hdr position in the incoming sip req, 2nd it tells the expected new hdr position if it is put in the original sip message (so that we can decide the order of hdr insertion)
    size_t origHdrLen;
} sipHdrModifyInfo_t;



osStatus_e sipGetHdrName(osMBuf_t* pSipMsg, sipRawHdr_t* pSipHeader);
osStatus_e sipGetHdrValue(osMBuf_t* pSipMsg, sipRawHdr_t* pSipHeader, bool* isEOH);
osStatus_e sipDecodeOneHdrRaw(osMBuf_t* pSipMsg, sipRawHdr_t* pSipHeader, bool* isEOH);
osStatus_e sipFindHdrs(osMBuf_t* pSipMsg, sipHdrInfo_t* sipHdrArray, uint8_t headerNum);
size_t sipHdr_getLen(sipRawHdr_t* pSipHdr);
const char* sipHdr_getFullName(char cName);
const char* sipHdr_getNameByCode(sipHdrName_e nameCode);
sipHdrName_e sipHdr_getNameCode(osPointerLen_t* hdrName);
osStatus_e sipHdrCreateProxyModifyInfo(sipHdrModifyInfo_t* pModifyInfo, sipMsgDecoded_t* sipMsgInDecoded);
//osStatus_e sipHdrCreateModifyInfo(sipHdrModifyInfo_t* pModifyInfo, sipMsgDecoded_t* pOrigSipDecoded, sipHdrName_e hdrName, sipHdrModifyCtrl_t ctrl; void* pHdrData, void* pExtraData);
//idx =0, top header of a header name, idx = SIP_MAX_SAME_HDR_NUM, bottom header of a header name, idx= 1 ~ SIP_MAX_SAME_HDR_NUM-1, from top
osStatus_e sipHdrGetPosLen(sipMsgDecoded_t* pSipDecoded, sipHdrName_e hdrNameCode, uint8_t idx, size_t* pPos, size_t* pLen);
//pHdrValue is type of sipHdrDecoded_u, depends on the hdr type, can be decodedIntValue, *decodedValue, or individual hdr structure from decodedHdrList
osStatus_e sipHdrGetValue(sipMsgDecoded_t* pSipInDecoded, sipHdrName_e hdrCode, uint8_t idx, void* pHdrValue);
//get a decoded hdr based on hdrCode
sipHdrInfo_t* sipHdrGetDecodedHdr(sipMsgDecoded_t* pSipInDecoded, sipHdrName_e hdrCode);
//get the "osList_t rawHdrList" from the sipHdrInfo_t for a specified hdr
sipRawHdrList_t* sipHdrGetRawValue(sipMsgDecoded_t* pSipInDecoded, sipHdrName_e hdrCode);
void* sipHdrParseByName(osMBuf_t* pSipMsgHdr, sipHdrName_e hdrNameCode);
sipMsgDecodedRawHdr_t* sipDecodeMsgRawHdr(osMBuf_t* pSipMsg, sipHdrName_e sipHdrArray[], int hdrArraySize);
sipMsgDecoded_t* sipDecodeMsg(osMBuf_t* pSipMsg, sipHdrName_e sipHdrArray[], int requestedHdrNum);
sipMsgDecoded_t* sipMsg_allocMsgDecoded(osMBuf_t* pSipMsg);
void sipMsg_deallocMsgDecoded(void *pData);
sipHdrEncode_h sipHdrGetEncode(sipHdrName_e hdrName);

#endif
