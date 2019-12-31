#include "osDebug.h"
#include "osMemory.h"

#include "sipHeader.h"
#include "sipGenericNameParam.h"
#include "sipParsing.h"
#include "sipHdrMisc.h"
#include "sipConfig.h"

osStatus_e sipParserHdr_str(osMBuf_t* pSipMsg, size_t hdrEndPos, osPointerLen_t* pCallid)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pSipMsg || !pCallid)
	{
		logError("NULL pointer, pSipMsg=%p, pCallid=%p.", pSipMsg, pCallid);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	pCallid->p = &pSipMsg->buf[pSipMsg->pos];
	pCallid->l = hdrEndPos;

EXIT:
	return status;
}


//this covers the folllowing headers: Expires, Session-Expires, Content-Length, Max-Forwards
osStatus_e sipParserHdr_lenTime(osMBuf_t* pSipMsg, size_t hdrEndPos, uint32_t* pLenTime)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pLenTime)
    {
        logError("NULL pointer, pSipMsg=%p, pLenTime=%p.", pSipMsg, pLenTime);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(hdrEndPos - pSipMsg->pos > 10)
	{
		logError("content length is too large, hdrEndPos=%ld.", hdrEndPos);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	*pLenTime = 0;
	while (pSipMsg->pos < hdrEndPos)
	{
		if(!SIP_IS_DIGIT(pSipMsg->buf[pSipMsg->pos]))
		{
			logError("content length header contains no digit character (%c).", pSipMsg->buf[pSipMsg->pos]);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		*pLenTime *= 10;
		*pLenTime += (pSipMsg->buf[pSipMsg->pos] - '0');
		pSipMsg->pos++;
	}

	debug("to remoove, max-forwards value=%d", *pLenTime);
EXIT:
	return status;
}


osStatus_e sipHdrLenTime_encode(osMBuf_t* pSipBuf, void* pLenTimeDT, void* pData)
{
    osStatus_e status = OS_STATUS_OK;
	uint32_t* pLenTime = pLenTimeDT;
	sipHdrName_e* pHdrName = pData;

    if(!pSipBuf || !pLenTime || !pData)
    {
        logError("null pointer, pSipBuf=%p, pLenTime=%p, pData=%p.", pSipBuf, pLenTime, pData);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	switch (*pHdrName)
	{
		case SIP_HDR_EXPIRES:
			osMBuf_writeStr(pSipBuf, "Expires: ", true);
			break;
		case SIP_HDR_MIN_EXPIRES:
			osMBuf_writeStr(pSipBuf, "Min-Expires: ", true);
			break;
		case SIP_HDR_SESSION_EXPIRES:
			osMBuf_writeStr(pSipBuf, "Session-Expires: ", true);
			break;
		case SIP_HDR_CONTENT_LENGTH:
			osMBuf_writeStr(pSipBuf, "Content-Length: ", true);
			break;
		case SIP_HDR_MAX_FORWARDS:
			osMBuf_writeStr(pSipBuf, "Max-Forwards: ", true);
			break;
		default:
			logError("hdr name (%d) is not handled.", *pHdrName);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
	}

	osMBuf_writeU32Str(pSipBuf, *pLenTime, true);
	
	osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
	return status;
}


/* pLenTimeDT contains lenTime itself except when hdrData is not NULL, which is only for MAX-FORWARDS case.
 * pHdrCode carries the hdrCode.
 */
osStatus_e sipHdrLenTime_create(void* pLenTimeDT, void* pHdrInDecoded, void* pHdrCodeDT)
{
	osStatus_e status = OS_STATUS_OK;
	uint32_t* pLenTime = pLenTimeDT;
	sipMsgDecoded_t* pSipInDecoded = pHdrInDecoded;
	sipHdrName_e* pHdrCode = pHdrCodeDT;

	if(!pLenTimeDT || !pHdrCodeDT)
	{
		logError("null pointer, pLenTimeDT=%p, pHdrCodeDT=%p.", pLenTimeDT, pHdrCodeDT); 
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(pSipInDecoded)
	{
		if(*pHdrCode != SIP_HDR_MAX_FORWARDS)
		{
			logError("try to create a hdr based on what is received in the incoming sip message, hdrCode=%d.", *pHdrCode);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		sipHdrDecoded_u hdrDecoded;
        status = sipHdrGetValue(pSipInDecoded, SIP_HDR_MAX_FORWARDS, 0, &hdrDecoded);
        if(status != OS_STATUS_OK)
        {
            logError("sipHdrLenTime_getValue() fails to get Max-Forwards value.");
            goto EXIT;
        }
		*pLenTime = hdrDecoded.decodedIntValue;

        if(*pLenTime == 1)
        {
            logError("Max-Forwards=1, drop the message.");
            status = OS_ERROR_EXT_INVALID_VALUE;
            goto EXIT;
        }

        *pLenTime--;
    }

EXIT:
	return status;
}


osStatus_e sipParserHdr_cSeq(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrCSeq_t* pCSeq)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pCSeq)
    {
        logError("NULL pointer, pSipMsg=%p, pCSeq=%p.", pSipMsg, pCSeq);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	pCSeq->seq.l = 0;
	pCSeq->method.l = 0;

	bool isSeq = true;
	size_t origPos = pSipMsg->pos;
	while (pSipMsg->pos < hdrEndPos)
    {
		if(isSeq)
		{
			if(SIP_IS_DIGIT(pSipMsg->buf[pSipMsg->pos]))
			{
				pSipMsg->pos++;
			}
			else if(SIP_IS_LWS(pSipMsg->buf[pSipMsg->pos]))
			{
				isSeq = false;
				pCSeq->seq.p = &pSipMsg->buf[origPos];
				pCSeq->seq.l = pSipMsg->pos - origPos;
				origPos = ++pSipMsg->pos;
			}
			else
        	{
            	logError("CSeq header contains no digit character (%c).", pSipMsg->buf[pSipMsg->pos]);
            	status = OS_ERROR_INVALID_VALUE;
            	goto EXIT;
        	}
		}
		else
		{
			if(SIP_IS_LWS(pSipMsg->buf[pSipMsg->pos]))
			{
				origPos = ++pSipMsg->pos;
			}
			else if(SIP_IS_ALPHA_CAPS(pSipMsg->buf[pSipMsg->pos]))
			{
				pSipMsg->pos++;
			}
			else
			{
				logError("CSeq Method contains invalid character (%c).", pSipMsg->buf[pSipMsg->pos]);
				status = OS_ERROR_INVALID_VALUE;
 	 	        goto EXIT;
           	}
		}
	}

	if(isSeq)
	{
		logError("the CSeq header does not contain Method.");
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	pCSeq->method.p = &pSipMsg->buf[origPos];
	pCSeq->method.l = hdrEndPos - origPos;

EXIT:
	return status;
}

/* covers header Allow, Supported, Allow-Events */
osStatus_e sipParserHdr_nameList(osMBuf_t* pSipMsg, size_t hdrEndPos, bool isCaps, osList_t* pList)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pList)
    {
        logError("NULL pointer, pSipMsg=%p, pList=%p.", pSipMsg, pList);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	bool isName = true;
	size_t nameLen = 0;
	size_t origPos = pSipMsg->pos;
	while (pSipMsg->pos < hdrEndPos)
	{
		if((isCaps && SIP_IS_ALPHA_CAPS(pSipMsg->buf[pSipMsg->pos])) || (!isCaps && SIP_IS_ALPHA(pSipMsg->buf[pSipMsg->pos])))
		{
			pSipMsg->pos++;
			nameLen++;
			if(!isName)
			{
				origPos = pSipMsg->pos;
				isName = true;
			}
		}
		else if(SIP_IS_LWS(pSipMsg->buf[pSipMsg->pos]))
		{
			pSipMsg->pos++;
		}
		else if(pSipMsg->buf[pSipMsg->pos] == ',')
		{
			isName = false;
			pSipMsg->pos++;
			
			status = osList_addString(pList, &pSipMsg->buf[origPos], nameLen);
			if(status != OS_STATUS_OK)
			{
				logError("add name to the nameList failure.");
				goto EXIT;
			} 
			nameLen = 0;
		}
		else
		{
			logError("invalid character (%c)", pSipMsg->buf[pSipMsg->pos]);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}
	}

	//last parameter name
    status = osList_addString(pList, &pSipMsg->buf[origPos], nameLen);
    if(status != OS_STATUS_OK)
    {
        logError("add name to the nameList failure.");
        goto EXIT;
    }


EXIT:
	if(status != OS_STATUS_OK)
	{
		osList_delete(pList);
	}

	pSipMsg->pos++;
	return status;
}


osStatus_e sipHdrPL_encodeByName(osMBuf_t* pSipBuf, void* pl, void* other)
{
	osStatus_e status = OS_STATUS_OK;
	osPointerLen_t* pPL = pl;
	char* pHdrName = other;

	if(!pSipBuf || !pl || !pHdrName)
	{
		logError("null pointer, pSipBuf=%p, pl=%p, pHdrName=%p.", pSipBuf, pl, pHdrName);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(!pPL->p || pPL->l==0)
	{
		logError("pl is invalid for hdrCode (%d), pl->p=%p, pl->l=%ld.", *pHdrName, pPL->p, pPL->l);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	osMBuf_writeStr(pSipBuf, pHdrName, true);
	osMBuf_writeStr(pSipBuf, ": ", true);
	osMBuf_writePL(pSipBuf, pPL, true);
	osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
	return status;
}


osStatus_e sipHdrPL_encode(osMBuf_t* pSipBuf, void* pl, void* other)
{
    osStatus_e status = OS_STATUS_OK;
    osPointerLen_t* pPL = pl;
    sipHdrName_e* pHdrCode = other;

    if(!pSipBuf || !pl || !pHdrCode)
    {
        logError("null pointer, pSipBuf=%p, pl=%p, pHdrCode=%p.", pSipBuf, pl, pHdrCode);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    if(!pPL->p || pPL->l==0)
    {
        logError("pl is invalid for hdrCode (%d), pl->p=%p, pl->l=%ld.", *pHdrCode, pPL->p, pPL->l);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	const char* pHdrName = sipHdr_getNameByCode(*pHdrCode);
	if(!pHdrName)
	{
		logError("hdr code2name translation fails for hdrcode (%d).", *pHdrCode);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

    osMBuf_writeStr(pSipBuf, pHdrName, true);
    osMBuf_writeStr(pSipBuf, ": ", true);
    osMBuf_writePL(pSipBuf, pPL, true);
    osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
    return status;
}


//if pHdrInDecoded != NULL, use pExtraInfo as a hdrType to get the PL info from pHdrInDecoded
//otherwise, pExtraInfo is a null terminated string
osStatus_e sipHdrPL_create(void* pl, void* pHdrInDecoded, void* pExtraInfo)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pl || !pExtraInfo)
    {
        logError("null pointer, pl=%p, pExtraInfo=%p.", pl, pExtraInfo);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    sipMsgDecoded_t* pSipInDecoded = pHdrInDecoded;
	if(pSipInDecoded)
	{
		sipHdrName_e* pHdrCode = pExtraInfo;
		if(pHdrCode == NULL || *pHdrCode == SIP_HDR_NONE)
		{
			logError("invalid pExtraInfo (pHdrCode), pHdrCode=%p.", pHdrCode);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}
 
		sipHdrDecoded_u hdrDecoded;
		status = sipHdrGetValue(pHdrInDecoded, *pHdrCode, 0, &hdrDecoded);
		if(status != OS_STATUS_OK)
		{
			logError("sipHdrGetValue error.");
			goto EXIT;
		}
		*(osPointerLen_t*)pl = osPL_clone((osPointerLen_t*)hdrDecoded.decodedValue);
	}
	else
	{
		osPL_setStr(pl, (const char*) pExtraInfo, 0);
	}		

EXIT:
	return status;
}


osStatus_e sipHdrCSeq_encode(osMBuf_t* pSipBuf, void* pCSeqDT, void* pReqTypeDT)
{
    osStatus_e status = OS_STATUS_OK;
	uint32_t* pCSeq = pCSeqDT;
	sipRequest_e* pReqType = pReqTypeDT;

    if(!pSipBuf || !pCSeq || !pReqType)
    {
        logError("null pointer, pSipBuf=%p, pCSeq=%p, pReqType=%p.", pSipBuf, pCSeq, pReqType);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(*pCSeq > SIP_MAX_CSEQ_NUM)
	{
		logError("the seq number (%d) is too big.", *pCSeq);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	osPointerLen_t method;
	status = sipMsg_code2Method(*pReqType, &method); 
    if(status != OS_STATUS_OK)
    {
        logError("request code2method translation fails for method code (%d).", *pReqType);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    if(!method.p || method.l==0)
    {
        logError("pCSeq is invalid, method.p=%p, method.l=%ld.", method.p, method.l);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	osMBuf_writeStr(pSipBuf, "CSeq: ", true);
    osMBuf_writeU32Str(pSipBuf, *pCSeq, true);
	osMBuf_writeU8(pSipBuf, ' ', true);
	osMBuf_writePL(pSipBuf, &method, true);
    osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
    return status;
}


osStatus_e sipHdrCallId_createCallId(osPointerLen_t* pl)
{
	osStatus_e status = OS_STATUS_OK;
	char* callIdValue = NULL;
	if(!pl)
	{
		logError("null pointer, pl.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    callIdValue = osMem_alloc(SIP_MAX_CALL_ID_LEN, NULL);
    if(callIdValue == NULL)
    {
        logError("allocate callId memory fails.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }
    pl->p = callIdValue;

    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    srand(time(NULL));
    int randValue=rand();

    pl->l = sprintf(callIdValue, "%lx%lx%x@%s", tp.tv_sec, tp.tv_nsec, randValue, sipConfig_getHostIP());

EXIT:
	if(status != OS_STATUS_OK)
	{
		osMem_deref(callIdValue);
		pl->l = 0;
	}

	return status;
}

#if 0
osStatus_e sipHdrLenTime_encode(osMBuf_t* pSipBuf, sipHdrName_e hdrName, uint32_t value)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipBuf)
    {
        logError("null pointer, pSipBuf.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	switch (hdrName)
	{
		case SIP_HDR_MAX_FORWARDS:
			osMBuf_writeStr(pSipBuf, "Max-Forwards:", true);
			osMBuf_writeU32(pSipBuf, value, true);
			break;
		default:
			logError("invalid hdrName (%d).", hdrName);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
			break;
	}
			
	osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
	return status;
}

#endif	
	
