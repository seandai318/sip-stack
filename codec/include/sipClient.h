/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipClient.h
 ********************************************************/

#ifndef _SIP_CLIENT_H
#define _SIP_CLIENT_H 

#include "osTypes.h"
#include "sipMsgRequest.h"

sipMsgRequest_t* sipClientBuildReq(sipRequest_e reqType, char* reqUri, char* fromUri, char* toUri, char* callId, uint32_t cseq);



#endif
