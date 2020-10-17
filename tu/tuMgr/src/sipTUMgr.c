/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTUMgr.c
 ********************************************************/

#include "sipTUIntf.h"
#include "sipTUConfig.h"


static sipTUAppOnSipMsg_h sipTU_appOnMsg[SIPTU_APP_TYPE_COUNT];


osStatus_e sipTU_onMsg(sipTUMsgType_e msgType, sipTUMsg_t* pMsg)
{
	if(!pMsg)
	{
		return OS_ERROR_NULL_POINTER;
	}

	switch(SIP_TU_PRODUCT_TYPE)
	{
		case SIPTU_PRODUCT_TYPE_MAS_AND_REG:
			return sipTU_appOnMsg[SIPTU_APP_TYPE_MAS](msgType, pMsg);
			break;
		case SIPTU_PRODUCT_TYPE_PROXY_AND_MAS_AND_REG:
			if(pMsg->sipMsgType == SIP_MSG_REQUEST)
			{
				if(pMsg->pSipMsgBuf->reqCode == SIP_METHOD_REGISTER || pMsg->pSipMsgBuf->reqCode == SIP_METHOD_MESSAGE)
				{
					return sipTU_appOnMsg[SIPTU_APP_TYPE_MAS](msgType, pMsg);
				}
				else
				{
					return sipTU_appOnMsg[SIPTU_APP_TYPE_PROXY](msgType, pMsg);
				}
			}
			else
			{
				if(pMsg->appType == SIPTU_APP_TYPE_PROXY)
				{
					return sipTU_appOnMsg[SIPTU_APP_TYPE_PROXY](msgType, pMsg);
				}
				else if(pMsg->appType == SIPTU_APP_TYPE_REG)
				{
                    return sipTU_appOnMsg[SIPTU_APP_TYPE_REG](msgType, pMsg);
				}
				else if(pMsg->appType == SIPTU_APP_TYPE_MAS)
				{
					return sipTU_appOnMsg[SIPTU_APP_TYPE_MAS](msgType, pMsg);
				}
				else
				{
					logError("try to forward a response to a not supported app(%d).", pMsg->appType);
				}
			}
			break;
		default:
			logError("SIP_TU_PRODUCT_TYPE(%d) is not supported.", SIP_TU_PRODUCT_TYPE);
			break;
	}

	return OS_ERROR_INVALID_VALUE;
}


void sipTU_attach(sipTuAppType_e appType, sipTUAppOnSipMsg_h sipAppOnMsg)
{
	sipTU_appOnMsg[appType] = sipAppOnMsg;
}
