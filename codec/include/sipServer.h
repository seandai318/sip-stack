/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipServer.h
 ********************************************************/

#ifndef _SIP_SERVER_H
#define _SIP_SERVER_H

#include "osTypes.h"
#include "sipHeader.h"
#include "sipMsgRequest.h"


sipMsgResponse_t* sipServerBuild100Response(sipMsgDecoded_t* sipMsgInDecoded);
sipMsgResponse_t* sipServerBuild18x200Response(sipMsgDecoded_t* sipMsgInDecoded, sipResponse_e rspCode);
sipMsgResponse_t* sipServerBuild4xx5xx6xxResponse(sipMsgDecoded_t* sipMsgInDecoded, sipResponse_e rspCode);
osStatus_e sipServerCommonHdrBuildEncode(sipMsgDecoded_t* sipMsgInDecoded, sipMsgResponse_t* pResp, sipHdrName_e hdrCode);


#endif
