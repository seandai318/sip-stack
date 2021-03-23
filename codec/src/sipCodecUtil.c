/*******************************************************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipCodecUtil.c
 * this function provides some util functions that the sip codec can provide for application
 *******************************************************************************************/


#include "osMemory.h"

#include "sipMsgRequest.h"
#include "sipCodecUtil.h"


static osStatus_e sipDecodeOneMGNPHdrURIs(sipHdrMultiGenericNameParam_t* pDecodedHdr, sipIdentity_t* pUser, int* userNum, int maxUserNum);


//decode top MultiGenericNameParam hdr, like Route header, etc.
sipHdrGenericNameParam_t* sipDecodeMGNPHdrTopValue(sipHdrName_e hdrCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipHdrDecoded_t* sipHdrDecoded, bool isDupRawHdr)
{
	sipHdrGenericNameParam_t* pGNP = NULL;

	if(!sipHdr_isAllowMultiValue(hdrCode))
	{
		logError("hdrCode(%d) does not allow multi hdr value.", hdrCode);
		goto EXIT;
	}

	if(sipDecodeHdr(pReqDecodedRaw->msgHdrList[hdrCode]->pRawHdr, sipHdrDecoded, isDupRawHdr) != OS_STATUS_OK)
	{
		logError("fails to decode the top hdr value for hdrcode(%d).", hdrCode);
		goto EXIT;
	}

	pGNP = &((sipHdrMultiGenericNameParam_t*)sipHdrDecoded->decodedHdr)->pGNP->hdrValue;	

EXIT:
	return pGNP;
}


//decocde all MGNP hdrs for a hdrCode to find all URIs, restricted by the maximum requested userNum
//pUriType, optional.  If not NULL, the uri type will be passed out
//userNum is IN/OUT.  When IN, the maximum number of users the caller is interested, when out, the number of users been parsed
osStatus_e sipDecode_getMGNPHdrURIs(sipHdrName_e hdrCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipIdentity_t* pUser, int* userNum)
{
	osStatus_e status = OS_STATUS_OK;
    sipHdrDecoded_t sipHdrDecoded = {};
    int maxUserNum = *userNum;
	*userNum = 0;

	if(*userNum == 0)
	{
		logInfo("requested sip user parsing number is 0, do nothing.");
		goto EXIT;
	}

    if(!sipHdr_isAllowMultiValue(hdrCode))
    {
        logInfo("hdrCode(%d) does not allow multi hdr value.", hdrCode);
        goto EXIT;
    }

	if(pReqDecodedRaw->msgHdrList[hdrCode] == NULL)
	{
		logInfo("the sip message does not contain hdr(%d).", hdrCode);
		goto EXIT;
	}

    //decode the first hdr line
    if(sipDecodeHdr(pReqDecodedRaw->msgHdrList[hdrCode]->pRawHdr, &sipHdrDecoded, false) != OS_STATUS_OK)
    {
        logError("fails to decode the top hdr value for hdrcode(%d).", hdrCode);
        goto EXIT;
    }

    status = sipDecodeOneMGNPHdrURIs(sipHdrDecoded.decodedHdr, pUser, userNum, maxUserNum);
    if(*userNum >= maxUserNum)
    {
        goto EXIT;
    }

	//required to parse more hdr values, parse one hdr line at a time
    maxUserNum -= *userNum;
    osListElement_t* pLE = pReqDecodedRaw->msgHdrList[hdrCode]->rawHdrList.head;
    while(pLE)
    {
        //free the previous allocated sipHdrDecoded.decodedHdr
        sipHdrDecoded.decodedHdr = osfree(sipHdrDecoded.decodedHdr);

        sipRawHdr_t* pRawHdr = pLE->data;
        if(sipDecodeHdr(pRawHdr, &sipHdrDecoded, false) != OS_STATUS_OK)
        {
            logError("fails to decode the top hdr value for hdrcode(%d).", hdrCode);
			*userNum = 0;
            goto EXIT;
        }

        status = sipDecodeOneMGNPHdrURIs(sipHdrDecoded.decodedHdr, pUser, userNum, maxUserNum);
        if(*userNum >= maxUserNum)
        {
            goto EXIT;
        }

        maxUserNum -= *userNum;

        pLE = pLE->next;
    }

EXIT:
    osfree(sipHdrDecoded.decodedHdr);
    return status;
}


//decode one MGNP hdr, note one hdr (a hdr with its own hdr name) may have multiple values
static osStatus_e sipDecodeOneMGNPHdrURIs(sipHdrMultiGenericNameParam_t* pDecodedHdr, sipIdentity_t* pUser, int* userNum, int maxUserNum)
{
	osStatus_e status = OS_STATUS_OK;
	
	if(*userNum >= maxUserNum || !pDecodedHdr)
	{
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	*userNum = 0;
    pUser[*userNum].sipUser = pDecodedHdr->pGNP->hdrValue.uri.sipUser;
	pUser[*userNum].sipUriType = pDecodedHdr->pGNP->hdrValue.uri.sipUriType;
	(*userNum)++;

    osListElement_t* pLE = pDecodedHdr->gnpList.head;
    while(pLE)
    {
        if(*userNum >= maxUserNum)
        {
            break;
        }

        pUser[*userNum].sipUser = ((sipHdrGenericNameParamDecoded_t*)pLE->data)->hdrValue.uri.sipUser;
		pUser[*userNum].sipUriType = ((sipHdrGenericNameParamDecoded_t*)pLE->data)->hdrValue.uri.sipUriType;
		(*userNum)++;

        pLE = pLE->next;
    }

EXIT:
	return status;
}
	

//copy src sipMsgBuf_t to the dest.  sipMsgBuf_t.pSipMsg is referred
void sipMsgBuf_copy(sipMsgBuf_t* dest, sipMsgBuf_t* src)
{
	if(!dest || !src)
	{
		return;
	}

	*dest = *src;
	dest->pSipMsg = osmemref(src->pSipMsg);
}
