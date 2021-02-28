/*************************************************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file saProxyMgr.c
 * for standalone proxies.  Standalone means there is no application above the proxy,
 * For applications like CSCF, cscf module is above proxy, saProxyMgr does not apply
 *************************************************************************************/

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


static osStatus_e saProxy_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static void saProxy_statusNtfy(void* pProxyMgrInfo, proxyInfo_t* pProxy, proxyStatus_e proxyStatus);



//right now the creation of hash is called from the main program, the proxyHash can not be per thread.  if want per thread hash, need to call the proxy_init() directly from a thread.
static osHash_t* proxyHash;


osStatus_e saProxy_init(uint32_t bucketSize, proxyReg2RegistrarCB_h proxyReg2Registrar, proxyDelFromRegistrarCB_h proxyDelFromRegistrar)
{
    osStatus_e status = OS_STATUS_OK;

    proxyHash = osHash_create(bucketSize);
    if(!proxyHash)
    {
        logError("fails to create proxyHash.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

	sipTU_attach(SIPTU_APP_TYPE_PROXY, saProxy_onSipTUMsg);

	callProxy_init(saProxy_statusNtfy, proxyReg2Registrar, proxyDelFromRegistrar);
	//callProxy_init(saProxy_statusNtfy, masReg_addAppInfo, masReg_deleteAppInfo);

EXIT:
    return status;
}


static osStatus_e saProxy_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    switch (msgType)
    {
        case SIP_TU_MSG_TYPE_MESSAGE:
		{
		    sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(&pSipTUMsg->sipMsgBuf, NULL, 0);
    		if(pReqDecodedRaw == NULL)
    		{
        		logError("fails to sipDecodeMsgRawHdr. The received sip message may not be correctly encoded.");
		        sipProxy_uasResponse(SIP_RESPONSE_400, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
        		status = OS_ERROR_INVALID_VALUE;
        		goto EXIT;
    		}

    		osPointerLen_t callId;
			status = sipHdrCallId_getValue(pReqDecodedRaw, &callId);

			proxyInfo_t* pProxyInfo = osPlHash_getUserData(proxyHash, &callId, true);
			if(pProxyInfo)
			{
				proxy_onMsgCallback proxyOnMsg = pProxyInfo->proxyOnMsg;
				status = proxyOnMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, NULL, &pProxyInfo, NULL);
			}
			else
			{
				osListElement_t* pProxyHashLE = osPlHash_addEmptyUserData(proxyHash, &callId, true);
				if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_INVITE)
				{
					status = callProxy_onSipTUMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, NULL, &pProxyInfo, pProxyHashLE);
					if(pProxyInfo)
					{
						osPlHash_setEmptyUserData(pProxyHashLE, pProxyInfo);
					}
					else
					{
						osHash_deleteNode(pProxyHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
					}
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
				break;
			}

           	status = ((proxyInfo_t*)pSipTUMsg->pTUId)->proxyOnMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, pSipTUMsg, NULL, NULL, NULL, NULL);
            break;
    }

EXIT:
    DEBUG_END
    return status;
}


//for saProxy, the pProxyMgrInfo is a hashLE that stores the proxyInfo of the saProxy
static void saProxy_statusNtfy(void* pProxyMgrInfo, proxyInfo_t* pProxy, proxyStatus_e proxyStatus)	
{
	if(!pProxyMgrInfo || !pProxy)
	{
		logError("null pointer, pProxyMgrInfo=%p, pProxy=%p.", pProxyMgrInfo, pProxy);
		return;
	}

	switch(proxyStatus)
	{
		case SIP_PROXY_STATUS_DELETE:
			//remove memory allocated for proxyInfo_t
			osHash_deleteNode(pProxyMgrInfo, OS_HASH_DEL_NODE_TYPE_ALL);
			break;
		default:
			break;
	}

	return;
}

