/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file proxyMgr.c
 ********************************************************/

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


//right now the creation of hash is called from the main program, the proxyHash can not be per thread.  if want per thread hash, need to call the proxy_init() directly from a thread.
static osHash_t* proxyHash;


osStatus_e proxy_init(uint32_t bucketSize)
{
    osStatus_e status = OS_STATUS_OK;

    proxyHash = osHash_create(bucketSize);
    if(!proxyHash)
    {
        logError("fails to create proxyHash.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

	sipTU_attach(SIPTU_APP_TYPE_PROXY, proxy_onSipTUMsg);

	callProxy_init();
	//saProxy_init();

EXIT:
    return status;
}


osStatus_e proxy_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
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

    		uint32_t key = osHash_getKeyPL(&callId, true);
    		osListElement_t* pHashLE = osHash_lookupByKey(proxyHash, &key, OSHASHKEY_INT);

			if(pHashLE)
			{
				proxy_onMsgCallback proxyOnMsg = ((proxyInfo_t*)((osHashData_t*)pHashLE->data)->pData)->proxyOnMsg;
				status = proxyOnMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, pHashLE);
			}
			else
			{
				if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_INVITE)
				{
					status = callProxy_onSipTUMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, NULL);
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
            status = ((proxyInfo_t*)pSipTUMsg->pTUId)->proxyOnMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, pSipTUMsg, NULL, NULL);
            break;
    }

EXIT:
    DEBUG_END
    return status;
}



osHash_t* proxy_getHash()
{
	return proxyHash;
}

