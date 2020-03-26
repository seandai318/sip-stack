#ifndef _SIP_PROXY_H
#define _SIP_PROXY_H

#include "osTypes.h"
#include "sipHeader.h"
#include "sipMsgRequest.h"


sipMsgRequest_t* sipProxyBuildReq(sipMsgDecoded_t* sipMsgInDecoded);

osStatus_e sipProxyHdrBuildEncode(sipMsgDecoded_t* sipMsgInDecoded, sipMsgRequest_t* pReqMsg, sipHdrNmT_t* pNMT);


#endif
