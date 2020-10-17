/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipSMS.h
 ********************************************************/

#ifndef _SIP_SMS_H
#define _SIP_SMS_H

#include "sipTUIntf.h"


osStatus_e masSMS_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
osStatus_e masSMS_onTimeout(uint64_t timerId, void* data);


#endif
