#include "osMemory.h"

#include "sipConfig.h"
#include "sipHeader.h"
#include "sipHdrAcceptedContact.h"
#include "sipHdrContact.h"
#include "sipHdrFromto.h"
#include "sipHdrMisc.h"
#include "sipHdrPani.h"
#include "sipHdrRoute.h"
#include "sipHdrVia.h"
#include "sipHdrContentType.h"
#include "sipHdrPani.h"
#include "sipHdrNameaddrAddrspec.h" 
#include "sipHdrDate.h"
#include "sipHeader.h"
#include "sipHeaderPriv.h"


//idx =0, top header of a header name, idx = SIP_MAX_SAME_HDR_NUM, bottom header of a header name, idx= 1 ~ SIP_MAX_SAME_HDR_NUM-1, from top
osStatus_e sipHdrGetPosLen(sipMsgDecoded_t* pSipDecoded, sipHdrName_e hdrNameCode, uint8_t idx, size_t* pPos, size_t* pLen)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pSipDecoded || !pPos || !pLen)
	{
		logError("null pointer, pSipDecoded=%p, pPos=%p, pLen=%p.", pSipDecoded, pPos, pLen);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	sipHdrInfo_t* pHdrInfo = pSipDecoded->msgHdrList[hdrNameCode];
	if(pHdrInfo == NULL)
	{
    	logInfo("the input sip message does not contain the required sip hdr (%d).", hdrNameCode);
    	*pPos = 0;
    	*pLen = 0;
		goto EXIT;
	}

    sipRawHdr_t* pRawHdr = NULL;
	if(idx == 0 ||(idx == SIP_MAX_SAME_HDR_NUM && pHdrInfo->rawHdr.rawHdrNum <= 1))
	{
		pRawHdr = pHdrInfo->rawHdr.pRawHdr;
	}
	else
	{
		osList_t* pRawHdrList = &pHdrInfo->rawHdr.rawHdrList;
		if (idx == SIP_MAX_SAME_HDR_NUM)
		{
			osListElement_t* pRawHdrLE = pRawHdrList->tail;
			pRawHdr = pRawHdrLE->data;
		}
		else
		{
            osListElement_t* pRawHdrLE = pRawHdrList->head;
            if(pRawHdrLE == NULL)
            {
                logError("sipMsgDecoded_t finds a matched hdr name (%d) for idx=%d, but pRawHdrList is empty.", hdrNameCode, idx);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

			bool isMatch = false;
			uint8_t i = 1;
			while(pRawHdrLE)
			{
				if(i++ == idx)
				{
					isMatch = true;
					pRawHdr = pRawHdrLE->data;
					break;
				}
				
				pRawHdrLE = pRawHdrLE->next;
			}

			if(!isMatch)
			{
				logError("hdr idx (%d) exceeds the stored hdr number (%d) for hdr (%d).", idx, i-1, hdrNameCode);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}
		}
	}

    if(pRawHdr == NULL)
    {
        logError("sipMsgDecoded_t finds a matched hdr name (%d), idx=%d, but pRawHdr is null.", hdrNameCode, idx);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    *pPos = pRawHdr->namePos;
   	*pLen = pRawHdr->valuePos - pRawHdr->namePos +  pRawHdr->value.l;	
	goto EXIT;

EXIT:		
	return status;
}


size_t sipHdr_getHdrStartPos(osMBuf_t* pSipMsg)
{
    osStatus_e status = OS_STATUS_OK;
    size_t pos = 0;

    if(!pSipMsg)
    {
        logError("null pointer, pSipMsg.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    while(pos < pSipMsg->end)
    {
        if(pSipMsg->buf[pos++] == '\r')
        {
            if(pSipMsg->buf[pos++] == '\n')
            {
                break;
            }
        }
    }

    pos = (pos == pSipMsg->end) ? 0 : pos;

EXIT:
    return pos;
}


sipHdrEncode_h sipHdrGetEncode( sipHdrName_e hdrName)
{
	return sipHdrCreateArray[hdrName].encodeHandler;
}



osStatus_e sipHdrCreateProxyModifyInfo(sipHdrModifyInfo_t* pModifyInfo, sipMsgDecoded_t* sipMsgInDecoded)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pModifyInfo || !sipMsgInDecoded)
    {
        logError("null pointer, pModifyInfo=%p, sipMsgInDecoded=%p.", pModifyInfo, sipMsgInDecoded);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pModifyInfo->modStatus = SIP_HDR_MODIFY_STATUS_HDR_EXIST;
    status = sipHdrGetPosLen(sipMsgInDecoded, pModifyInfo->nmt.hdrCode, 0, &pModifyInfo->origHdrPos, &pModifyInfo->origHdrLen);
    if(status != OS_STATUS_OK)
    {
        logError("sipHdrGetPosLen() fails.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    if(pModifyInfo->origHdrPos == 0)
    {
        pModifyInfo->modStatus = SIP_HDR_MODIFY_STATUS_HDR_NOT_EXIST;

        //the mandatory header missed
        if(sipHdrCreateArray[pModifyInfo->nmt.hdrCode].isMandatory)
        {
            logError("the input sip message does not have mandatory hdr (%d).", pModifyInfo->nmt.hdrCode);
            status = OS_ERROR_EXT_INVALID_VALUE;
            goto EXIT;
        }
            
		//the required hdr does not exist, make sure no original hdr is removed.
        pModifyInfo->origHdrLen = 0;

        //if  hdr does not exist, add in front of callId
		if (sipHdrCreateArray[pModifyInfo->nmt.hdrCode].isPriority)
		{
           	status = sipHdrGetPosLen(sipMsgInDecoded, SIP_HDR_CALL_ID, 0, &pModifyInfo->origHdrPos, &pModifyInfo->origHdrLen);
           	if(status != OS_STATUS_OK || pModifyInfo->origHdrPos == 0)
           	{
               	logError("sipHdrGetPosLen() fails.");
               	status = !pModifyInfo->origHdrPos ? OS_ERROR_EXT_INVALID_VALUE : status;
               	goto EXIT;
           	}
		}
		else
		{
			pModifyInfo->origHdrPos = SIP_HDR_EOF;
        }
    }

EXIT:
    return status;
}

//get decoded hdr based on hdrCode and idx (to-do, if multiple values for a hdr, which value to get)
osStatus_e sipHdrGetValue(sipMsgDecoded_t* pSipInDecoded, sipHdrName_e hdrCode, uint8_t idx, void* pHdrValue)
{
    osStatus_e status = OS_STATUS_OK;
	sipHdrDecoded_u* pHdr = pHdrValue;

    if(!pSipInDecoded || ! pHdr)
    {
        logError("null pointer, pSipInDecoded=%p, pHdrValue=%p.", pSipInDecoded, pHdr);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	sipHdrInfo_t* pHdrInfo = pSipInDecoded->msgHdrList[hdrCode];
	switch(hdrCode)
	{
		//for TimeLen
		case SIP_HDR_CONTENT_LENGTH:
		case SIP_HDR_EXPIRES:
		case SIP_HDR_MAX_FORWARDS:
		case SIP_HDR_MIN_EXPIRES:
		case SIP_HDR_SESSION_EXPIRES:
			pHdr->decodedIntValue = *(uint32_t*)pHdrInfo->decodedHdr.pDecodedHdr;
			debug("to remove, decodedIntValue=%d", pHdr->decodedIntValue);
			goto EXIT;
			break;
		//for other unique headers
		case SIP_HDR_CALL_ID:
			pHdr->decodedValue = pHdrInfo->decodedHdr.pDecodedHdr;
			goto EXIT;
			break;
		default:
		{
			if(idx == 0 ||(pHdrInfo->decodedHdr.decodedHdrNum == 1 && idx == SIP_MAX_SAME_HDR_NUM))
			{
				pHdr->decodedValue = pHdrInfo->decodedHdr.pDecodedHdr;
				goto EXIT;
				break;
			}
			else
			{
            	osList_t* pDecodedList = &pHdrInfo->decodedHdr.decodedHdrList;
				osListElement_t* pDecodedLE = pDecodedList->head;
				if(idx == SIP_MAX_SAME_HDR_NUM)
				{
					pDecodedLE = pDecodedList->tail;
					pHdr->decodedValue = pDecodedLE->data;
					goto EXIT;
					break;
				}

				int i = 1;
				while(pDecodedLE)
				{
					if(i++ == idx)
					{
						pHdr->decodedValue = pDecodedLE->data;
						break;
					}
			
					pDecodedLE = pDecodedLE->next;
				}

				logError("the idx (%d) exceeds the stored hdr values (%d).", idx, i);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
				break;
			}
		}
	}
	
EXIT:	
	return status;
}


//get a decoded hdr based on hdrCode 
sipHdrInfo_t* sipHdrGetDecodedHdr(sipMsgDecoded_t* pSipInDecoded, sipHdrName_e hdrCode)
{
	sipHdrInfo_t* pHdrInfo = NULL;

    if(!pSipInDecoded)
    {
        logError("null pointer, pSipInDecoded.");
        goto EXIT;
    }

	pHdrInfo = pSipInDecoded->msgHdrList[hdrCode];

EXIT:
	return pHdrInfo;
}


//get raw hdr value for a hdr based on decoded sip Msg
sipRawHdrList_t* sipHdrGetRawValue(sipMsgDecoded_t* pSipInDecoded, sipHdrName_e hdrCode)
{
    osStatus_e status = OS_STATUS_OK;
	sipRawHdrList_t* pHdrRawList = NULL;

    if(!pSipInDecoded)
    {
        logError("null pointer, pSipInDecoded=%p.", pSipInDecoded);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	pHdrRawList = &pSipInDecoded->msgHdrList[hdrCode]->rawHdr;

EXIT:
	return pHdrRawList;			
}


//pPos: points to the beginning of a raw hdr entry 
//pLen: points to the length of a raw hdr entry, including the \r\n
osStatus_e sipHdrGetRawHdrPos(sipRawHdr_t* pRawHdr, size_t* pPos, size_t* pLen)
{	 
	if(!pRawHdr || !pPos || !pLen)
	{
		logError("pRawHdr=%p, pPos=%p, pLen=%p.", pRawHdr, pPos, pLen);
		return OS_ERROR_NULL_POINTER;
	}

	*pPos = pRawHdr->namePos;
	*pLen = pRawHdr->valuePos - pRawHdr->namePos + pRawHdr->value.l +2;

	return OS_STATUS_OK;
}


bool sipHdr_isAllowMultiValue(sipHdrName_e hdrCode)
{
	return hdrCode >= SIP_HDR_COUNT ? false : sipHdrCreateArray[hdrCode].isAllowMultiHdr;
}
