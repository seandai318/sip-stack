#ifndef __CSCF_HELPER_H
#define __CSCF_HELPER_H


#include <netinet/in.h>

#include "osPL.h"

#include "diaMsg.h"

#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"
#include "sipTUIntf.h"


osStatus_e cscf_sendRegResponse(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pRegInfo, uint32_t regExpire, struct sockaddr_in* pPeer, struct sockaddr_in* pLocal, sipResponse_e rspCode);
osStatus_e cscf_getImpiFromSipMsg(sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pImpu, osPointerLen_t* pImpi);
sipResponse_e cscf_cx2SipRspCodeMap(diaResultCode_t resultCode);
//userNum: in/out, the maximum requested user as the input, the user in the sip request message as the output
osStatus_e cscf_getRequestUser(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool isMO, osPointerLen_t* pUser, int* userNum);


#endif
