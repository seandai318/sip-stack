#include "osDebug.h"
#include "osMemory.h"

#include "sipHeader.h"
#include "sipGenericNameParam.h"
#include "sipParsing.h"
#include "sipHdrMisc.h"
#include "sipConfig.h"
#include "sipHeader.h"


osStatus_e sipParserHdr_str(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrStr_t* pCallid)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pSipMsg || !pCallid)
	{
		logError("NULL pointer, pSipMsg=%p, pCallid=%p.", pSipMsg, pCallid);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	pCallid->p = &pSipMsg->buf[pSipMsg->pos];
	//to-do, shall not ->l = hdrEndPos - pSipMsg->pos?
	pCallid->l = hdrEndPos - pSipMsg->pos;

EXIT:
	return status;
}


//this covers the folllowing headers: Expires, Session-Expires, Content-Length, Max-Forwards
osStatus_e sipParserHdr_lenTime(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrInt_t* pLenTime)
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

	debug("to remoove, lentime value=%d", *pLenTime);
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
    osPointerLen_t seq={};

    if(!pSipMsg || !pCSeq)
    {
        logError("NULL pointer, pSipMsg=%p, pCSeq=%p.", pSipMsg, pCSeq);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

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
				seq.p = &pSipMsg->buf[origPos];
				seq.l = pSipMsg->pos - origPos;
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
	if(status == OS_STATUS_OK)
	{
		pCSeq->seqNum = osPL_str2u32(&seq);
	}

	return status;
}


osStatus_e sipParserHdr_intParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrIntParam_t* pIntParam)
{
    osStatus_e status = OS_STATUS_OK;
	osPointerLen_t numPL={};

    if(!pSipMsg || !pIntParam)
    {
        logError("NULL pointer, pSipMsg=%p, pIntParam=%p.", pSipMsg, pIntParam);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	enum {
		STATE_NUMBER,
		STATE_COMMENT,
		STATE_PARAM,
	};
	int state = STATE_NUMBER;
    size_t origPos = pSipMsg->pos;
    while (pSipMsg->pos < hdrEndPos)
    {
		switch (state)
		{
			case STATE_NUMBER:
				if(!SIP_IS_DIGIT(pSipMsg->buf[pSipMsg->pos]))
	    		{
          			numPL.p = &pSipMsg->buf[origPos];
           			numPL.l = pSipMsg->pos - origPos;
            		origPos = pSipMsg->pos;
					if(pSipMsg->buf[pSipMsg->pos] == ';')
					{
						state = STATE_PARAM;
						origPos = pSipMsg->pos + 1;
					}
					else
					{
						state = STATE_COMMENT;
						origPos = pSipMsg->pos + 1;
					}  
				}
				break;
			case STATE_COMMENT:
				if(pSipMsg->buf[pSipMsg->pos] == ';')
				{
					state = STATE_PARAM;
					pIntParam->comment.p = &pSipMsg->buf[origPos];
					pIntParam->comment.l = pSipMsg->pos - origPos;
            		origPos = pSipMsg->pos + 1;
				}
				break;
			case STATE_PARAM:
				if(pSipMsg->buf[pSipMsg->pos] == ';')
				{
					if(pIntParam->paramNum++ == 0)
					{
						pIntParam->param.p = &pSipMsg->buf[origPos];
            			pIntParam->param.l = pSipMsg->pos - origPos;
            			origPos = pSipMsg->pos + 1;
					}
					else
					{
						osPointerLen_t* pl = osMem_alloc(sizeof(osPointerLen_t), NULL);
						pl->p = &pSipMsg->buf[origPos];
						pl->l = pSipMsg->pos - origPos;
						osPL_trimLWS(pl, true, true);
						osList_append(&pIntParam->paramList, pl);
					}

                	origPos = pSipMsg->pos + 1;
				}
				break;
			default:
				break;
		}

		pSipMsg->pos++;
	}

	switch (state)
	{
		case STATE_NUMBER:
            numPL.p = &pSipMsg->buf[origPos];
            numPL.l = pSipMsg->pos - origPos;
			break;
		case STATE_COMMENT:
            pIntParam->comment.p = &pSipMsg->buf[origPos];
            pIntParam->comment.l = pSipMsg->pos - origPos;
			osPL_trimLWS(&pIntParam->comment, true, true);
			break;
		case STATE_PARAM:
            if(pIntParam->paramNum++ == 0)
            {
                pIntParam->param.p = &pSipMsg->buf[origPos];
                pIntParam->param.l = pSipMsg->pos - origPos;
				osPL_trimLWS(&pIntParam->param, true, true);
            }
            else
            {
                osPointerLen_t* pl = osMem_alloc(sizeof(osPointerLen_t), NULL);
                pl->p = &pSipMsg->buf[origPos];
                pl->l = pSipMsg->pos - origPos;
                osPL_trimLWS(pl, true, true);
                osList_append(&pIntParam->paramList, pl);
            }
			break;
	}

EXIT:
	if(status == OS_STATUS_OK)
	{
		pIntParam->number = osPL_str2u32(&numPL);	
	}
	else if(status != OS_ERROR_NULL_POINTER)
	{
		osList_clear(&pIntParam->paramList);
	}
		
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


osStatus_e sipHdrCallId_createAndAdd(osMBuf_t* pSipBuf, osPointerLen_t* pCallId)
{
	osStatus_e status = OS_STATUS_OK;
    osPointerLen_t pl={};

	if(!pSipBuf)
	{
		logError("null pointer, pSipBuf.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	status = sipHdrCallId_createCallId(pCallId ? pCallId : &pl);
	if(status != OS_STATUS_OK)
	{
		logError("fails to create call id.");
		goto EXIT;
	}

	osMBuf_writeStr(pSipBuf, "Call-ID: ", true);
	osMBuf_writePL(pSipBuf, pCallId ? pCallId : &pl, true);
	osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
	if(!pCallId && status == OS_STATUS_OK)
	{
		osMem_deref((void*)pl.p);
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
	
