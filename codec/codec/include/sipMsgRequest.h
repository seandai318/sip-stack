#ifndef _SIP_MSG_REGUEST_H
#define _SIP_MSG_REGUEST_H


#include "osPL.h"
#include "osTypes.h"

#include "sipMsgFirstLine.h"
#include "sipHeader.h"
#include "sipHdrRoute.h"


typedef struct sipMsgRequest {
	sipRequest_e reqType;
	osMBuf_t* sipRequest;
//	sipMsgDecoded_t* pSipMsgDecoded;	//decoded sipRequest
	osPointerLen_t viaBranchId;
	osPointerLen_t fromTag;
//	osPointerLen_t toTag;
	osPointerLen_t callId;
} sipMsgRequest_t;


typedef struct sipMsgResponse {
	sipResponse_e rspCode;
	osMBuf_t* sipMsg;
//	sipMsgDecoded_t* pSipMsgDecoded;	//decoded sipMsg
	sipMsgDecoded_t* pRequest;
	osPointerLen_t toTag;
	sipHdrRouteElement_t* pOwnRR;	//if a node does not want to includ itself into a session path, pOwnRR=NULL
} sipMsgResponse_t;


typedef struct sipHdrAddCtrl {
	bool isRaw;
	bool isOverride2Bytes;		//when adding a hdr to the end of sipMsg, and the end of sipMsg already has new empty line (\r\n), need to override these 2 bytes.  \r\n will be added back after the hdr insertion if it was override.
	bool isRevertHdrValue;	//if a hdr in the original sip message has multiple hdr values, insert into the new message as the same order or reverse order (like Record-Route changes to Route).  This shall only be used when isRaw is false, because otherwise, the hdr in a original message may has multiple values and we can not re-order raw hdr
	char* newHdrName;	//there may have time when a hdr's hdr values are to be kept, but the hdr name shall be replaced, like change from Record-Route to Route.  if this parameter is NULL, no name replacement
} sipHdrAddCtrl_t;
	


//to-do, to be used to replace osMBuf_t pSipBuf in all header functions.  Otherwise, hdrStartPos has to be calculated each time going through hdr
typedef struct sipMsgBuf {
    osMBuf_t* pSipMsg;  //the original sip message
    size_t hdrStartPos; //the beginning of the 1st header in a sip message, right after the 1st line.
	sipRequest_e reqCode;
	bool isRequest;
} sipMsgBuf_t;


//this data structure only contains all the raw hdr information for a sip message, a single call of sipDecodeMsgRawHdr() returns this data structure.  After getting this data structure, user can selective pick any one of the hdrs to decode as needed.
//there is another data structure, sipMsgDecoded_t, contains both the raw hdr information and decoded hdr information.  A single call of sipDecodeMsg() returns that data structure
typedef struct sipMsgDecodedRawHdr {
    size_t hdrNum;
    sipMsgBuf_t sipMsgBuf;
    sipRawHdrList_t* msgHdrList[SIP_HDR_COUNT];
	osMBuf_t msgContent;
} sipMsgDecodedRawHdr_t;



osStatus_e sipMsgAddHdr(osMBuf_t* pSipBuf, sipHdrName_e hdrName, void* pHdr, void* pExtraInfo, sipHdrAddCtrl_t ctrl);
//insert a namevalue pair to the end of the current hdr that is ended by pSipBuf->pos.  The hdr shall already have \r\n added
osStatus_e sipMsgHdrAppend(osMBuf_t* pSipBuf, sipParamNameValue_t* paramPair, char seperator);
sipMsgRequest_t* sipMsgCreateReq(sipRequest_e reqType, sipUri_t* pReqUri);
sipMsgRequest_t* sipMsgCreateProxyReq(sipMsgDecoded_t* sipMsgInDecoded, sipHdrNmT_t nmt[], int n);
sipMsgResponse_t* sipMsgCreateResponse(sipMsgDecoded_t* sipMsgInDecoded, sipResponse_e rspCode, sipHdrName_e addHdrList[], int n);
sipMsgDecodedRawHdr_t* sipDecodeMsgRawHdr(sipMsgBuf_t* pSipMsgBuf, sipHdrName_e sipHdrArray[], int hdrArraySize);


#endif
