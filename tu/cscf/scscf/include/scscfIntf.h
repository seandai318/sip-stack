#ifndef __SCSCF_INTF_H
#define __SCSCF_INTF_H


#include "osPL.h"
#include "osSockAddr.h"

#include "diaMsg.h"

#include "sipTUIntf.h"
#include "sipMsgRequest.h"

#include "scscfIfc.h"


osStatus_e scscfReg_init(uint32_t bucketSize);
//osStatus_e scscfReg_sessInitSAR(osPointerLen_t* pImpu, scscfHssNotify_h scscfSess_onHssMsg, void* pSessData);
osStatus_e scscfReg_onIcscfMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pImpi, osPointerLen_t* pImpu);
osStatus_e scscfReg_onTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
void scscfReg_onDiaMsg(diaMsgDecoded_t* pDiaDecoded, void* pAppData);
bool scscfReg_perform3rdPartyReg(scscfRegInfo_t* pRegInfo);
osStatus_e scscfSess_init(uint32_t bucketSize);
osStatus_e scscfSess_onTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);


#endif

