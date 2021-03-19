/************************************************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file proxyMgr.c
 * for proxy that has higher layers, like CSCF.  For scenarios that there is no higher
 * layers above proxy, the saProxyMgr shall be used
 ************************************************************************************/

#include "osTypes.h"
#include "osHash.h"
#include "osDebug.h"
#include "osPL.h"

#include "sipMsgFirstLine.h"
#include "sipMsgRequest.h"
#include "sipHdrMisc.h"
#include "sipTUIntf.h"
#include "proxyMgr.h"
#include "proxyHelper.h"
#include "callProxy.h"



osStatus_e proxy_init(proxyStatusNtfyCB_h proxyStatusNtfy)
{
    osStatus_e status = OS_STATUS_OK;

	sipTU_attach(SIPTU_APP_TYPE_PROXY, proxy_onSipTUMsg);
	callProxy_init(proxyStatusNtfy, NULL, NULL);

EXIT:
    return status;
}


//this function receives messages directly from TU
osStatus_e proxy_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg=%p", pSipTUMsg);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(pSipTUMsg->sipMsgBuf.isRequest)
	{
		logError("directly receive request from TU, expect request from app using proxy.");
        sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(&pSipTUMsg->sipMsgBuf, NULL, 0);
		sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
		goto EXIT;
	}

    switch (msgType)
    {
        case SIP_TU_MSG_TYPE_MESSAGE:
		{
		    sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(&pSipTUMsg->sipMsgBuf, NULL, 0);
    		if(pReqDecodedRaw == NULL)
    		{
        		logError("fails to sipDecodeMsgRawHdr. The received sip message may not be correctly encoded.");
        		status = OS_ERROR_EXT_INVALID_VALUE;
        		goto EXIT;
    		}

			//only handle response.  The request is expected from app above proxy
		    if(pSipTUMsg->sipMsgBuf.isRequest)
    		{
        		logError("directly receive request from TU, expect request from app using proxy.");
        		sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
        		goto EXIT;
    		}


#if 0
    		osPointerLen_t callId;
			status = sipHdrCallId_getValue(pReqDecodedRaw, &callId);

    		uint32_t key = osHash_getKeyPL(&callId, true);
    		osListElement_t* pHashLE = osHash_lookupByKey(proxyHash, &key, OSHASHKEY_INT);

			if(pHashLE)
			{
				proxy_onMsgCallback proxyOnMsg = ((proxyInfo_t*)((osHashData_t*)pHashLE->data)->pData)->proxyOnMsg;
				status = proxyOnMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, pHashLE);
			}
#endif
			//only handle response
			if(!pSipTUMsg->pTUId)
            {
                logError("a response message does not have TUid.");
				status = OS_ERROR_EXT_INVALID_VALUE;
                goto EXIT;
            }

			if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_INVITE)
			{
				status = callProxy_onSipTUMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, NULL, NULL, NULL);
			}
			else
			{
				//status = saProxy_onSipMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, NULL);
			}
            break;
		}
        case SIP_TU_MSG_TYPE_TRANSACTION_ERROR:
        default:
			if(!pSipTUMsg->pTUId)
			{
				logInfo("null TUId, ignore the message.");
                status = OS_ERROR_INVALID_VALUE;
				break;
			}

           	status = ((proxyInfo_t*)pSipTUMsg->pTUId)->proxyOnMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, pSipTUMsg, NULL, NULL, NULL, NULL);
            break;
    }

EXIT:
    DEBUG_END
    return status;
}



//this function receives messages from TU via proxy app
osStatus_e proxy_onSipTUMsgViaApp(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipProxyRouteModCtl_t* pRouteCtl, proxyInfo_t** ppProxy, void* pProxyMgrInfo)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    if(!pSipTUMsg || !ppProxy || !pProxyMgrInfo)
    {
        logError("null pointer, pSipTUMsg=%p, ppProxy=%p, pProxyMgrInfo=%p", pSipTUMsg, ppProxy, pProxyMgrInfo);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    switch (msgType)
    {
        case SIP_TU_MSG_TYPE_MESSAGE:
        {
            if(pReqDecodedRaw == NULL)
			{
                logError("unexpected NULL sipDecodeMsgRawHdr.");
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}

            if(*ppProxy)
            {
                proxy_onMsgCallback proxyOnMsg = (*ppProxy)->proxyOnMsg;
                status = proxyOnMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, pRouteCtl, ppProxy, pProxyMgrInfo);
            }
            else
            {
				//this only happens for the initial request.  For the subsequent request, proxy app shall already have pProxy.
				//the response is normally expected to directly come from TU via proxy_onSipTUMsgViaApp.  But if proxy app wants to
				//have response detour via it, proxyMgr is also OK
                if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_INVITE)
                {
                    status = callProxy_onSipTUMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, pRouteCtl, ppProxy, pProxyMgrInfo);
                }
                else
                {
                    //status = saProxy_onSipMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, NULL);
                }
            }
            break;
        }
        case SIP_TU_MSG_TYPE_TRANSACTION_ERROR:
        default:
            if(!pSipTUMsg->pTUId)
            {
                logInfo("null TUId, ignore the message.");
				status = OS_ERROR_INVALID_VALUE;
                break;
            }

            status = ((proxyInfo_t*)pSipTUMsg->pTUId)->proxyOnMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, pSipTUMsg, NULL, NULL, NULL, NULL);
            break;
    }

EXIT:
    DEBUG_END
    return status;
}
