#ifndef _SIP_HEADER_H
#define _SIP_HEADER_H

#include "osTypes.h"
#include "osMBuf.h"
#include "osPL.h"
#include "osList.h"

#include "sipHeaderData.h"
#include "sipMsgFirstLine.h"
#include "sipGenericNameParam.h"


/* if a hdr has multiple values, like via: via1,via2\r\n. the example via hdr only has one element of this data structure.
 if a msg contains: via: via1, via2\r\nvia: via3, then there will be 2 elements of this data structure for via, one contains via1 and via2, one contains via3. */ 
typedef struct sipHdrRaw {
    sipHdrName_e nameCode;
//	size_t hdrLen;	//the raw hdr length, from the first char of a header until the \n. =valuePos-namePos+value.l 
    osPointerLen_t name;
    size_t namePos;	//The very beginning of hdr, it is expected there is no SP before the real hdr name
    osPointerLen_t value;	//starting from the 1st no SP character of a value until /r/n, /r/n is excluded.  If a hdr name contains multiple hdr values, like Route: ra,rb,rc, they are all included in this value parameter
    size_t valuePos;	//the pos of value, points to the 1st no SP character of a value
} sipRawHdr_t;


typedef union sipHdrDecoded_u {
	uint32_t decodedIntValue;	//integer value
	void* decodedValue;			//pointer to header data structure, like PL, hdr specific data structure, etc.
} sipHdrDecoded_u;
	

//The reason osList_t is used because many headers may have multiple values, each has its own header name/entry, like  hdr-a: aa\r\nhdr-a: bb.
typedef struct sipRawHdrList {
	uint8_t rawHdrNum;
    sipRawHdr_t* pRawHdr;       //for the 1st hdr name, use this one.  Only if more than one hdr name, starting from the 2nd one, use rawHdrListrawHdrList
    osList_t rawHdrList;    //each element contains sipRawHdr_t.  if multiple hdr values shares one hdr name, like hdr-a: a,b,c, all these values share one entry in rawHdrList.
} sipRawHdrList_t;


typedef struct sipDecodedHdrList {
	uint8_t decodedHdrNum;		//the amount of hdr name entries in the SIP MESSAGE for this particular hdr
    void* pDecodedHdr;         //for the 1st hdr name use this one. Only if more than one hdr name, starting from the 2nd one, use decodedHdrList.
//    void* decodedValue;         //for the 1st hdr name use this one. Only if more than one hdr name, starting from the 2nd one, use decodedHdrList.
	osList_t decodedHdrList;	//each element contains a void*, the real data structure depends on the hdr nameCode
} sipDecodedHdrList_t;
	

//to-do, decodedHdrList, decodedIntValue, decodedValue, make it a union
typedef struct sipHeaderList {
    sipHdrName_e nameCode;
	sipRawHdrList_t rawHdr;
//    osList_t rawHdrList;		//each element contains sipRawHdr_t.  The same as decodedHdrList, if a hdr name has multiple values, all these values share one entry in rawHdrList.
	sipDecodedHdrList_t decodedHdr;
//	osList_t decodedHdrList;	//maybe multiple headers for a sip message, each element contains the data structure of individual header. void*. Be noted that if a sip message contains one header name with multiple values, that header itself has a osList for all these values, and only has one entry for that header name in this list.  For example, a sip message contains the following via: [via: a, b, c\\r\nvia: d], then decodedHdrList will have 2 entries for sipHdrVia_t, the 1st entry contains via hdr value a,b,c, the 2nd entry contains via hdr value d
	uint32_t decodedIntValue;	//a unique header for a sip message, contains a uint32 integer
	void* decodedValue;			//a unique header for a sip message, contains string, etc.
} sipHdrInfo_t;


//this data structure contains the raw hdr information and decoded hdr information.  A single call of sipDecodeMsg() returns this data structure
//there is another data structure, sipMsgDecodedRawHdr_t, only contains all the raw hdr information for a sip message, a single call of sipDecodeMsgRawHdr() returns that data structure.  After getting this data structure, user can selective pick any one of the hdrs to decode as needed.
typedef struct sipMsgDecoded {
	osMBuf_t* sipMsg;
	sipFirstline_t sipMsgFirstLine;
	size_t hdrNum;
	sipHdrInfo_t* msgHdrList[SIP_HDR_COUNT];
	osMBuf_t msgContent;
} sipMsgDecoded_t;



//this contains one hdr name-values pair in a sip message.  Be noticed that it is possible that a sip name may have multiple values in a hdr entry
typedef struct sipHdrDecoded {
	sipHdrName_e hdrCode;
	bool isRawHdrCopied;	//if true, rawHdr is copied, otherwise, points to other memory block like the original sip message
	osMBuf_t rawHdr;	//contains the hdr value, depends on isRawHdrCopied, this may or may not be a self-contained structure
	void* decodedHdr;	//points to a hdr data structure.  inside the hdr data structure, osPointerLen_t type points to rawHdr
} sipHdrDecoded_t;


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
//osStatus_e sipGetHdrValue(osMBuf_t* pSipMsg, sipRawHdr_t* pSipHeader, bool* isEOH);
osStatus_e sipDecodeOneHdrRaw(osMBuf_t* pSipMsg, sipRawHdr_t* pSipHeader, bool* isEOH);
osStatus_e sipDecode_getTopRawHdr(osMBuf_t* pSipMsg, sipRawHdr_t* pRawHdr, sipHdrName_e hdrCode);
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
//sipMsgDecodedRawHdr_t* sipDecodeMsgRawHdr(sipHdrBuf_t* pSipHdrBuf, sipHdrName_e sipHdrArray[], int hdrArraySize);
//decode a raw hdr.  isDupRawHdr controls if the raw hdr contents are duplicated
osStatus_e sipDecodeHdr(sipRawHdr_t* sipRawHdr, sipHdrDecoded_t* sipHdrDecoded, bool isDupRawHdr);
sipMsgDecoded_t* sipDecodeMsg(osMBuf_t* pSipMsg, sipHdrName_e sipHdrArray[], int requestedHdrNum);
sipMsgDecoded_t* sipMsg_allocMsgDecoded(osMBuf_t* pSipMsg);
void sipMsg_deallocMsgDecoded(void *pData);
sipHdrEncode_h sipHdrGetEncode(sipHdrName_e hdrName);
size_t sipHdr_getHdrStartPos(osMBuf_t* pSipMsg);
//pPos: points to the beginning of a raw hdr entry
//pLen: points to the length of a raw hdr entry, including the \r\n
osStatus_e sipHdrGetRawHdrPos(sipRawHdr_t* pRawHdr, size_t* pPos, size_t* pLen);
bool sipHdr_isAllowMultiValue(sipHdrName_e hdrCode);
uint8_t sipHdr_getHdrValueNum(sipHdrDecoded_t* pSipHdrDecoded);
osStatus_e sipHdr_getFirstHdrValuePosInfo(sipHdrDecoded_t* pSipHdrDecoded, sipHdr_posInfo_t* pTopHdrValuePos);


#endif
