//for sipRegistrar, accepts ALL SIP REGISTER, does not do any subscription check, or authentication check.  Basically, a IMS AS registratar functionality

#include "osHash.h"


static osHash_t* siRegHash;


osStatus_e sipTURegInit(uint32_t bucketSize)
{
    osStatus_e status = OS_STATUS_OK;

    sipTURegHash = osHash_create(bucketSize);
    if(!sipTURegHash)
    {
        logError("fails to create sipTURegHash.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

EXIT:
    return status;
}


osStatus_e sipTUReg_onMsg(sipTransMsgType_e msgType, void* pData, uint64_t timerId)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pData)
    {
        logError("null pointer, pData.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }


typedef struct sipTUMsg {
    sipTUMsgType_e sipMsgType;
    osMBuf_t* pSipMsg;
    sipTransInfo_t* pTransInfo;
    sipTransaction_t* pTrans;
} sipTUMsg_t;

osStatus_e sipTUReg_rcvRegister(osMBuf_t* pSipMsg)
{
    osStatus_e status = OS_STATUS_OK;

	
	    osPointerLen_t sipUri;
    osStatus_e status = sipParamUri_getUriFromRawHdrValue(&fromhdr, &sipUri);
int osPL_dup(osPointerLen_t *dst, const osPointerLen_t *src)



/* this must be the first function to call to build a sip response buffer
 * if isAddNullContent = TRUE, it is expected that Content-Length is at the end of the response, and this function is the only function to be called to build the response message
 * ppReqDecodedRaw: a list of raw headers.  if isAddNullContent=TRUE, only the hdrs with  name in sipHdrArray are extracted.  if isAddNullContent=FALSE, all hdrs in the SIP message will be extracted. 
 */
static osMBuf_t* sipTUNIS_buildResponse(osMBuf_t* pSipBufReq, sipResponse_e rspCode, sipMsgDecodedRawHdr_t** ppReqDecodedRaw, sipHdrName_e sipHdrArray[], int arraySize, bool isAddNullContent)
{
    osStatus_e status = OS_STATUS_OK;

    sipMsgResponse_t* pResp = NULL;
    sipMsgDecodedRawHdr_t* pReqDecodedRaw = NULL;

    if(!pSipBufReq || !sipHdrArray)
    {
        logError("null pointer, pSipBufReq=%p, sipHdrArray=%p.", pSipBufReq, sipHdrArray);
		status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    osMBuf_t* pSipBufResp = osMBuf_alloc(SIP_MAX_MSG_SIZE);
    if(!pSipBufResp)
    {
        logError("fails to allocate pSipBufResp.");
		status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

//    sipHdrName_e sipHdrArray[] = {SIP_HDR_VIA, SIP_HDR_FROM, SIP_HDR_TO, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
//    int arraySize = sizeof(sipHdrArray) / sizeof(sipHdrArray[0]);
	if(isAddNullContent)
	{
    	pReqDecodedRaw = sipDecodeMsgRawHdr(pSipBufReq, sipHdrArray, arraySize);
	}
	else
	{
		pReqDecodedRaw = sipDecodeMsgRawHdr(pSipBufReq, NULL, 0);
	}

    if(!pReqDecodedRaw)
    {
        logError("fails to sipDecodeMsgRawHdr.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    //encode status line
    status = sipHdrFirstline_respEncode(pSipBufResp, &rspCode, NULL);
    if(status != OS_STATUS_OK)
    {
        logError("fails to encode the response, respCode=%d.", rspCode);
        goto EXIT;
    }

    sipHdrAddCtrl_t ctrl = {true, false, false, NULL};
	osPointerLen_t extraInfo;
    for(int i=0; i<arraySize; i++)
    {
		if(sipHdrArray[i] == SIP_HDR_TO)
		{
			osPointerLen_t toTag;
			status = sipHdrFromto_generateTagId(&toTag);
			if(status != OS_STATUS_OK)
			{
				logError("fails to generateTagId.");
				goto EXIT;
			}

        	status = sipMsgAddHdr(pSipBufResp, sipHdrArray[i], &pReqDecodedRaw->msgHdrList[sipHdrArray[i]]->rawHdrList, &toTag, ctrl);
			osPL_dealloc(&toTag);
		}
		else
		{
			status = sipMsgAddHdr(pSipBufResp, sipHdrArray[i], &pReqDecodedRaw->msgHdrList[sipHdrArray[i]]->rawHdrList, NULL, ctrl);
		}

        if(status != OS_STATUS_OK)
        {
            logError("fails to sipMsgAddHdr for respCode (%d).", sipHdrArray[i]);
            goto EXIT;
        }
    }

	if(isAddNullContent)
	{
    	uint32_t contentLen = 0;
    	sipHdrAddCtrl_t ctrlCL = {false, true, false, NULL};
    	status = sipMsgAddHdr(pResp->sipMsg, SIP_HDR_CONTENT_LENGTH, &contentLen, NULL, ctrlCL);
    	if(status != OS_STATUS_OK)
    	{
        	logError("fails to sipMsgAddHdr for SIP_HDR_CONTENT_LENGTH.");
        	status = OS_ERROR_INVALID_VALUE;
        	goto EXIT;
    	}
	}

EXIT:
	if(isAddNullContent || status != OS_STATUS_OK)
	{
    	osMem_deref(pReqDecodedRaw);
		pReqDecodedRaw = NULL;
	}

	*ppReqDecodedRaw = pReqDecodedRaw;

    return pSipBufResp;
}

  
/* when this function is called, sipTUNIS_buildResponse must have been called 
 * if isAddNullContent = TRUE, it is expected this is the last function to build the sip response
 */
static osStatus_e sipTUNIS_buildResponseCont(osMBuf_t* pSipBufReq, osMBuf_t* pSipBufResp, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipHdrName_e sipHdrArray[], int arraySize, bool isAddNullContent)
{
    osStatus_e status = OS_STATUS_OK;

    sipMsgResponse_t* pResp = NULL;

    if(!pSipBufReq || !sipHdrArray || !pSipBufResp)
    {
        logError("null pointer, pSipBufReq=%p, sipHdrArray=%p, pSipBufResp= %p.", pSipBufReq, sipHdrArray, pSipBufResp);
		status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(pReqDecodedRaw == NULL)
	{
    	pReqDecodedRaw = sipDecodeMsgRawHdr(pSipBufReq, sipHdrArray, arraySize);
    	if(!pReqDecodedRaw)
    	{
        	logError("fails to sipDecodeMsgRawHdr.");
			status = OS_ERROR_INVALID_VALUE;
        	goto EXIT;
    	}
	}

    sipHdrAddCtrl_t ctrl = {true, false, false, NULL};
    osPointerLen_t extraInfo;
    for(int i=0; i<arraySize; i++)
    {
        if(sipHdrArray[i] == SIP_HDR_TO)
        {
            osPointerLen_t toTag;
            status = sipHdrFromto_generateTagId(&toTag);
            if(status != OS_STATUS_OK)
            {
                logError("fails to generateTagId.");
                goto EXIT;
            }

            status = sipMsgAddHdr(pSipBufResp, sipHdrArray[i], &pReqDecodedRaw->msgHdrList[sipHdrArray[i]]->rawHdrList, &toTag, ctrl);
            osPL_dealloc(&toTag);
        }
        else
        {
            status = sipMsgAddHdr(pSipBufResp, sipHdrArray[i], &pReqDecodedRaw->msgHdrList[sipHdrArray[i]]->rawHdrList, NULL, ctrl);
        }

        if(status != OS_STATUS_OK)
        {
            logError("fails to sipMsgAddHdr for respCode (%d).", sipHdrArray[i]);
            goto EXIT;
        }
    }

    if(isAddNullContent)
    {
        uint32_t contentLen = 0;
        sipHdrAddCtrl_t ctrlCL = {false, true, false, NULL};
        status = sipMsgAddHdr(pResp->sipMsg, SIP_HDR_CONTENT_LENGTH, &contentLen, NULL, ctrlCL);
        if(status != OS_STATUS_OK)
        {
            logError("fails to sipMsgAddHdr for SIP_HDR_CONTENT_LENGTH.");
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }
    }

EXIT:
	if(isAddNullContent || status != OS_STATUS_OK)
	{
    	osMem_deref(pReqDecodedRaw);
	}

    return status;
}
	
