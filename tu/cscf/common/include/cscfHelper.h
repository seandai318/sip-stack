#ifndef __CSCF_HELPER_H
#define __CSCF_HELPER_H


#include <netinet/in.h>

#include "osPL.h"

#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"


osStatus_e cscf_sendRegResponse(sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pRegInfo, uint32_t regExpire, struct sockaddr_in* pPeer, struct sockaddr_in* pLocal, sipResponse_e rspCode);
osStatus_e cscf_getImpiFromSipMsg(sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pImpu, osPointerLen_t* pImpi);


#endif
