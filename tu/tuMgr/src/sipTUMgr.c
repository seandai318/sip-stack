/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTUMgr.c
 ********************************************************/

#include "sipTUIntf.h"
#include "sipTUConfig.h"
#include "cscfIntf.h"


static sipTUAppOnSipMsg_h gSipTU_appOnMsg[SIPTU_APP_TYPE_COUNT];



/* about the distribution of tu messages to app.
 * primary based on the production type(SIP_TU_PRODUCT_TYPE).  This is hard defined in sipTUConfig.h.  This will be changed to user defined variable
 * For request, SIP_TU_PRODUCT_TYPE is the only criteria used to distribute messages to app.
 * for response, when system starts, each app attach its appType with a onMsg function towards TU.  When an app sends out a request, it will assign 
 * an appType in the request, which will be passed back by the transaction for the response message. TU can use the appType to determine where to pass 
 * to the application based on the appType attach() in the system start up.
 */ 
osStatus_e sipTU_onMsg(sipTUMsgType_e msgType, sipTUMsg_t* pMsg)
{
	if(!pMsg)
	{
		return OS_ERROR_NULL_POINTER;
	}

	switch(SIP_TU_PRODUCT_TYPE)
	{
		case SIPTU_PRODUCT_TYPE_MAS_AND_REG:
			return gSipTU_appOnMsg[SIPTU_APP_TYPE_MAS](msgType, pMsg);
			break;
		case SIPTU_PRODUCT_TYPE_PROXY_AND_MAS_AND_REG:
			if(pMsg->sipMsgType == SIP_MSG_REQUEST)
			{
				if(pMsg->sipMsgBuf.reqCode == SIP_METHOD_REGISTER || pMsg->sipMsgBuf.reqCode == SIP_METHOD_MESSAGE)
				{
					return gSipTU_appOnMsg[SIPTU_APP_TYPE_MAS](msgType, pMsg);
				}
				else
				{
					return gSipTU_appOnMsg[SIPTU_APP_TYPE_PROXY](msgType, pMsg);
				}
			}
			else
			{
				if(pMsg->appType == SIPTU_APP_TYPE_PROXY)
				{
					return gSipTU_appOnMsg[SIPTU_APP_TYPE_PROXY](msgType, pMsg);
				}
				else if(pMsg->appType == SIPTU_APP_TYPE_REG)
				{
                    return gSipTU_appOnMsg[SIPTU_APP_TYPE_REG](msgType, pMsg);
				}
				else if(pMsg->appType == SIPTU_APP_TYPE_MAS)
				{
					return gSipTU_appOnMsg[SIPTU_APP_TYPE_MAS](msgType, pMsg);
				}
				else
				{
					logError("try to forward a response to a not supported app(%d).", pMsg->appType);
				}
			}
			break;
		case SIPTU_PRODUCT_TYPE_CSCF:
			if(pMsg->sipTuMsgType != SIP_TU_MSG_TYPE_TRANSACTION_ERROR && pMsg->sipMsgType == SIP_MSG_REQUEST)
			{
				return gSipTU_appOnMsg[SIPTU_APP_TYPE_CSCF](msgType, pMsg);
			}
			else
			{
				switch(pMsg->appType)
				{
					case SIPTU_APP_TYPE_ICSCF:
						return gSipTU_appOnMsg[SIPTU_APP_TYPE_ICSCF](msgType, pMsg);
						break;
					case SIPTU_APP_TYPE_SCSCF_REG:
						return gSipTU_appOnMsg[SIPTU_APP_TYPE_SCSCF_REG](msgType, pMsg);
						break;
					case SIPTU_APP_TYPE_SCSCF_SESSION:
						return gSipTU_appOnMsg[SIPTU_APP_TYPE_SCSCF_SESSION](msgType, pMsg);
                        break;
					default:
						logError("appType(%d) is not supported for SIPTU_PRODUCT_TYPE_CSCF.", pMsg->appType);
						break;
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
	gSipTU_appOnMsg[appType] = sipAppOnMsg;
}


void sipTU_init(char* configDir)
{
    switch(SIP_TU_PRODUCT_TYPE)
	{
		case SIPTU_PRODUCT_TYPE_CSCF:
			return cscf_init(configDir);
			break;
		default:
			logInfo("product type(%d) does not need init.", SIP_TU_PRODUCT_TYPE);
	}
}
