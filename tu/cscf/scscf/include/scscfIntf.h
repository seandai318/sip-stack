#ifndef __SCSCF_INTF_H
#define __SCSCF_INTF_H


#include "osPL.h"
#include "osSockAddr.h"

#include "sipTUIntf.h"
#include "sipMsgRequest.h"



void scscfReg_processRegMsg(osPointerLen_t* pImpi, osPointerLen_t* pImpu, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, struct sockaddr_in* pLocalHost);


#endif

