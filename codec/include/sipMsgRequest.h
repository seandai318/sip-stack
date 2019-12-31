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
	


osStatus_e sipMsgAddHdr(osMBuf_t* pSipBuf, sipHdrName_e hdrName, void* pHdr, void* pExtraInfo, sipHdrAddCtrl_t ctrl);
//insert a namevalue pair to the end of the current hdr that is ended by pSipBuf->pos.  The hdr shall already have \r\n added
osStatus_e sipMsgHdrAppend(osMBuf_t* pSipBuf, sipHdrParamNameValue_t* paramPair, char seperator);
sipMsgRequest_t* sipMsgCreateReq(sipRequest_e reqType, sipUri_t* pReqUri);
sipMsgRequest_t* sipMsgCreateProxyReq(sipMsgDecoded_t* sipMsgInDecoded, sipHdrNmT_t nmt[], int n);
sipMsgResponse_t* sipMsgCreateResponse(sipMsgDecoded_t* sipMsgInDecoded, sipResponse_e rspCode, sipHdrName_e addHdrList[], int n);


#endif
