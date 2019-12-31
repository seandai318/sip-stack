#include "osHash.h"
#include "osTimer.h"

#include "sipHeaderMisc.h"
#include "sipTUIntf.h"
#include "sipTU.h"
#include "sipTransIntf.h"
#include "masMgr.h"
#include "sipRegistrar.h"


static osStatus_e masSMS_onSipMsg(sipTUMsg_t* pSipTUMsg);
static osStatus_e masSMS_onSipTransError(sipTUMsg_t* pSipTUMsg);
static void masSMS_onTimeout(uint64_t timerId, void* data);


osStatus_e masSMS_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	osStatus_e status = OS_STATUS_OK;

	switch (msgType)
	{
		case SIP_TU_MSG_TYPE_MESSAGE:
			status = masSMS_onSipMsg(pSipTUMsg);
			break;
		case SIP_TU_MSG_TYPE_TRANSACTION_ERROR:
		default:
			status =  masSMS_onSipTransError(pSipTUMsg);
			break;
	}

	return status;
}


//for now there is no timeout event for masSMS.  It may be added later when syniverse is hooked
void masSMS_onTimeout(uint64_t timerId, void* data)
{
    return;
}


static osStatus_e masSMS_onSipMsg(sipTUMsg_t* pSipTUMsg)
{
	osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_INVALID;

    sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(pSipTUMsg->pSipMsgBuf, NULL, 0);
    if(pReqDecodedRaw == NULL)
    {
        logError("fails to sipDecodeMsgRawHdr.  Since the received SIP message was not decoded, there will not be any sip response.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	switch(pSipTUMsg->sipMsgType)
	{
		case SIP_MSG_REQUEST:
		{
    		if(pSipTUMsg->pSipMsgBuf->reqCode != SIP_METHOD_MESSAGE)
    		{
        		logError("receives unexpected sip message type (%d).", pSipTUMsg->pSipMsgBuf->reqCode);
				rspCode = SIP_RESPONSE_503;
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
    		}

			//for now, use proxy mode
		    //get the user's sip URI
    		osPointerLen_t sipUri;
			bool isOrig;
			status = sipTU_asGetUser(pReqDecodedRaw, &sipUri, &isOrig);
    		if(status != OS_STATUS_OK)
    		{
        		logError("fails to sipParamUri_getUriFromRawHdrValue.");
        		rspCode = SIP_RESPONSE_500;
        		status = OS_ERROR_INVALID_VALUE;
        		goto EXIT;
    		}

			//do not accept terminating SIP MESSAGE
			if(!isOrig)
			{
				rspCode = SIP_RESPONSE_503;
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}

			//forward the SIP MESSAGE back to CSCF, add top via, remove top Route, reduce the max-forarded by 1, 
			sipTransInfo_t sipTransInfo;
			osMBuf_t* pReq = sipTU_buildProxyRequest(pReqDecodedRaw, NULL, 0, NULL, 0, &sipTransInfo.transId.viaId);
			if(!pReq)
			{
				logError("fails to create proxy request.");
				rspCode = SIP_RESPONSE_503;
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
			}

			masInfo_t* pMasInfo = osMem_alloc(sizeof(masInfo_t), NULL);
//			masInfo.pSipReqBuf = osMBuf_allocRef(pSipTUMsg->sipMsgBuf);
//			masInfo.viaId = sipTransInfo.transId.viaId;
			pMasInfo->pSrcTransId = pSipTUMsg->pTransId;
			void* pTUId = masReg_addAppInfo(&sipUri, pMasInfo);
			if(!pTUId)
			{
				osMBuf_dealloc(pReq);
				rspCode = SIP_RESPONSE_500;
				status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

			sipTransMsg_t sipTransMsg;
			sipTransMsg.sipMsgType = SIP_MSG_REQUEST;
			sipTransMsg.sipMsgBuf.pSipMsg = pReq;
			sipTransMsg.sipMsgBuf.reqCode = SIP_METHOD_MESSAGE;
			sipTransMsg.sipMsgBuf.isRequest = true;
			sipTransMsg.sipMsgBuf.hdrStartPos = 0;
			sipTransInfo.transId.reqCode = SIP_METHOD_MESSAGE;
			sipTransInfo.isRequest = true;
			sipTransMsg.pTransInfo = &sipTransInfo;
			sipTransMsg.pTransId = NULL;
			sipTransMsg.pSenderId = pTUId;
					
            status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);
			break;	
		}
		case SIP_MSG_RESPONSE:
		{
            if(pSipTUMsg->pSipMsgBuf->reqCode != SIP_METHOD_MESSAGE)
            {
                logError("receives unexpected sip message type (%d).", pSipTUMsg->pSipMsgBuf->reqCode);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}

            osMBuf_t* pResp = sipTU_buildProxyResponse(pReqDecodedRaw, NULL, 0, NULL, 0);
            if(!pResp)
            {
                logError("fails to create proxy reponse.");
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

			void* pTransId = masReg_getTransId(pSipTUMsg->pTUId, pSipTUMsg->pTransId, false);
            if(!pTransId)
            {
                osMBuf_dealloc(pResp);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            sipTransMsg_t sipTransMsg;
            sipTransMsg.sipMsgType = SIP_MSG_RESPONSE;
            sipTransMsg.sipMsgBuf.pSipMsg = pResp;
			sipTransMsg.sipMsgBuf.reqCode = SIP_METHOD_MESSAGE;
			sipTransMsg.sipMsgBuf.isRequest = false;
			sipTransMsg.sipMsgBuf.hdrStartPos = 0;
            sipTransMsg.pTransInfo = NULL;
            sipTransMsg.pTransId = pTransId;
            sipTransMsg.pSenderId = pSipTUMsg->pTUId;

            status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

			if(status != OS_STATUS_OK)
			{
    			masReg_deleteAppInfo(pSipTUMsg->pTUId, pSipTUMsg->pTransId);
			}
			break;
		}
		default:
			logError("receives unexpected TUMSG (%d).", pSipTUMsg->sipMsgType);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
	}

EXIT:
	if(status != OS_STATUS_OK && pSipTUMsg->sipMsgType==SIP_MSG_REQUEST && rspCode != SIP_RESPONSE_INVALID)
	{
        osMBuf_t* pSipResp;
        sipHdrName_e sipHdrArray[] = {SIP_HDR_VIA, SIP_HDR_FROM, SIP_HDR_TO, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
        int arraySize = sizeof(sipHdrArray) / sizeof(sipHdrArray[0]);
        pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, SIP_RESPONSE_503, sipHdrArray, arraySize, true);

        if(pSipResp)
        {
            sipTransMsg_t sipTransMsg = {};
            sipTransMsg.sipMsgType = SIP_MSG_RESPONSE;
            sipTransMsg.sipMsgBuf.pSipMsg = pSipResp;
            sipTransMsg.sipMsgBuf.reqCode = SIP_METHOD_MESSAGE;
            sipTransMsg.sipMsgBuf.isRequest = false;
            sipTransMsg.sipMsgBuf.hdrStartPos = 0;
            sipTransMsg.pTransId = pSipTUMsg->pTransId;

            status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);
        }
        else
        {
            logError("fails to sipTU_buildResponse.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        }
	}

	return status;
}


uint64_t masSMSStartTimer(time_t msec, void* pData)
{
    return osStartTimer(msec, masSMS_onTimeout, pData);
}


static osStatus_e masSMS_onSipTransError(sipTUMsg_t* pSipTUMsg)
{
    osStatus_e status = OS_STATUS_OK;
	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	masReg_deleteAppInfo(pSipTUMsg->pTUId, pSipTUMsg->pTransId);

EXIT:
	return status;
}
