/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrHelper.c
 ********************************************************/

#include "osMemory.h"

#include "sipHdrTypes.h"
#include "sipHeaderData.h"
#include "sipHeader.h"
#include "sipHdrNameValue.h"
#include "sipHdrMisc.h"
#include "sipHdrContact.h"
#include "sipHdrVia.h"


void* sipHdrParse(osMBuf_t* pSipMsg, sipHdrName_e hdrNameCode, size_t hdrValuePos, size_t hdrValueLen)
{
	if(!pSipMsg)
	{
		return NULL;
	}

	osMBuf_t pRawHdr;
    osMBuf_allocRef1(&pRawHdr, pSipMsg, hdrValuePos, hdrValueLen);
	return sipHdrParseByName(&pRawHdr, hdrNameCode);
}


//assume the pSipRawHdr->pos points to the beginning of a hdr value, and pSipRawHdr->end points to the ending of a hdr value ('\r\n' not included)
void* sipHdrParseByName(osMBuf_t* pSipRawHdr, sipHdrName_e hdrNameCode)
{
    void* pHdr = NULL;

    if(!pSipRawHdr)
    {
        return NULL;
    }

    switch(hdrNameCode)
    {
        //sipHdrMultiSlashValueParam_t
        case SIP_HDR_ACCEPT:
        {
            sipHdrMultiSlashValueParam_t* pRealHdr = oszalloc(sizeof(sipHdrMultiSlashValueParam_t), sipHdrMultiSlashValueParam_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_multiSlashValueParam(pSipRawHdr, pSipRawHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrMultiGenericNameParam_t
        case SIP_HDR_ACCEPT_CONTACT:
        case SIP_HDR_ALERT_INFO:
        case SIP_HDR_CALL_INFO:
        case SIP_HDR_ERROR_INFO:
        case SIP_HDR_HISTORY_INFO:
        case SIP_HDR_P_ASSERTED_IDENTITY:
        case SIP_HDR_P_ASSOCIATED_URI:
        case SIP_HDR_P_PREFERRED_IDENTITY:
        case SIP_HDR_PATH:
        case SIP_HDR_PERMISSION_MISSING:
        case SIP_HDR_RECORD_ROUTE:
        case SIP_HDR_ROUTE:
        case SIP_HDR_SERVICE_ROUTE:
        case SIP_HDR_TRIGGER_CONSENT:
		{
			bool isNameaddrOnly = false;
			if(hdrNameCode == SIP_HDR_RECORD_ROUTE || hdrNameCode == SIP_HDR_ROUTE)
			{
				isNameaddrOnly = true;
			}

			sipHdrMultiGenericNameParam_t* pRealHdr = oszalloc(sizeof(sipHdrMultiGenericNameParam_t), sipHdrMultiGenericNameParam_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_multiNameParam(pSipRawHdr, pSipRawHdr->end, isNameaddrOnly, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrMultiValueParam_t
        case SIP_HDR_ACCEPT_ENCODING:
        case SIP_HDR_ACCEPT_LANGUAGE:
        case SIP_HDR_P_ACCESS_NETWORK_INFO:
        case SIP_HDR_P_VISITED_NETWORK_ID:
        case SIP_HDR_REASON:
        case SIP_HDR_REJECT_CONTACT:
        case SIP_HDR_SECURITY_CLIENT:
        case SIP_HDR_SECURITY_SERVER:
        case SIP_HDR_SECURITY_VERIFY:
        {
            sipHdrMultiValueParam_t* pRealHdr = oszalloc(sizeof(sipHdrMultiValueParam_t), sipHdrMultiValueParam_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_multiValueParam(pSipRawHdr, pSipRawHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrNameList_t
        case SIP_HDR_ACCEPT_RESOURCE_PRIORITY:
        case SIP_HDR_ALLOW:
        case SIP_HDR_ALLOW_EVENTS:
        case SIP_HDR_CONTENT_ENCODING:
        case SIP_HDR_CONTENT_LANGUAGE:
        case SIP_HDR_IN_REPLY_TO:
        case SIP_HDR_P_EARLY_MEDIA:
        case SIP_HDR_P_MEDIA_AUTHORIZATION:
        case SIP_HDR_PROXY_REQUIRE:
        case SIP_HDR_REQUEST_DISPOSITION:
        case SIP_HDR_REQUIRE:
        case SIP_HDR_RESOURCE_PRIORITY:
        case SIP_HDR_SUPPORTED:
        case SIP_HDR_UNSUPPORTED:
        {
			bool isCaps = false;
            sipHdrNameList_t* pRealHdr = oszalloc(sizeof(sipHdrNameList_t), sipHdrNameList_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(hdrNameCode == SIP_HDR_ALLOW)
            {
                isCaps = true;
            }

            if(sipParserHdr_nameList(pSipRawHdr, pSipRawHdr->end, isCaps, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrValueParam_t
        case SIP_HDR_ANSWER_MODE:
        case SIP_HDR_CONTENT_DISPOSITION:
        case SIP_HDR_ENCRYPTION:
        case SIP_HDR_EVENT:
        case SIP_HDR_IDENTITY:
        case SIP_HDR_JOIN:
        case SIP_HDR_P_ANSWER_STATE:
        case SIP_HDR_PRIV_ANSWER_MODE:
        case SIP_HDR_REFER_SUB:
        case SIP_HDR_REPLACES:
        case SIP_HDR_SUBSCRIPTION_STATE:
        case SIP_HDR_TARGET_DIALOG:
        {
            sipHdrValueParam_t* pRealHdr = oszalloc(sizeof(sipHdrValueParam_t), sipHdrValueParam_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_valueParam(pSipRawHdr, pSipRawHdr->end, false, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrNameValueList_t
        case SIP_HDR_AUTHENTICATION_INFO:
        case SIP_HDR_P_CHARGING_VECTOR:
        case SIP_HDR_PRIVACY:
        {
            sipHdrNameValueList_t* pRealHdr = oszalloc(sizeof(sipHdrNameValueList_t), sipHdrNameValueList_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_nameValueList(pSipRawHdr, pSipRawHdr->end, false, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrMethodParam_t
        case SIP_HDR_AUTHORIZATION:
        case SIP_HDR_PROXY_AUTHENTICATE:
        case SIP_HDR_PROXY_AUTHORIZATION:
        case SIP_HDR_WWW_AUTHENTICATE:
        {
            sipHdrMethodParam_t* pRealHdr = oszalloc(sizeof(sipHdrMethodParam_t), sipHdrMethodParam_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_MethodParam(pSipRawHdr, pSipRawHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrStr_t
        case SIP_HDR_CALL_ID:
        case SIP_HDR_DATE:
        case SIP_HDR_MIME_VERSION:
        case SIP_HDR_ORGANIZATION:
        case SIP_HDR_PRIORITY:
        case SIP_HDR_RACK:
        case SIP_HDR_SERVER:
        case SIP_HDR_SIP_ETAG:
        case SIP_HDR_SIP_IF_MATCH:
        case SIP_HDR_SUBJECT:
        case SIP_HDR_TIMESTAMP:
        case SIP_HDR_USER_AGENT:
        {
            sipHdrStr_t* pRealHdr = oszalloc(sizeof(sipHdrStr_t), NULL);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_str(pSipRawHdr, pSipRawHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        case SIP_HDR_CONTACT:
        {
            sipHdrMultiContact_t* pRealHdr = oszalloc(sizeof(sipHdrMultiContact_t), sipHdrMultiContact_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_contact(pSipRawHdr, pSipRawHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrInt_t
        case SIP_HDR_CONTENT_LENGTH:
        case SIP_HDR_EXPIRES:
        case SIP_HDR_FLOW_TIMER:
        case SIP_HDR_MAX_BREADTH:
        case SIP_HDR_MAX_FORWARDS:
        case SIP_HDR_MIN_EXPIRES:
        case SIP_HDR_MIN_SE:
        case SIP_HDR_RSEQ:
        {
            sipHdrInt_t* pRealHdr = oszalloc(sizeof(sipHdrInt_t), NULL);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_lenTime(pSipRawHdr, pSipRawHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrSlashValueParam_t
        case SIP_HDR_CONTENT_TYPE:
        {
            sipHdrSlashValueParam_t* pRealHdr = oszalloc(sizeof(sipHdrSlashValueParam_t), sipHdrSlashValueParam_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_slashValueParam(pSipRawHdr, pSipRawHdr->end, false, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        case SIP_HDR_CSEQ:
        {
			sipHdrCSeq_t* pRealHdr = oszalloc(sizeof(sipHdrCSeq_t), NULL);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_cSeq(pSipRawHdr, pSipRawHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrGenericNameParam_t
        case SIP_HDR_FROM:
        case SIP_HDR_P_CALLED_PARTY_ID:
        case SIP_HDR_P_PROFILE_KEY:
        case SIP_HDR_P_REFUSED_URI_LIST:
        case SIP_HDR_P_SERVED_USER:
        case SIP_HDR_P_USER_DATABASE:
        case SIP_HDR_REFER_TO:
        case SIP_HDR_REFERRED_BY:
        case SIP_HDR_REPLY_TO:
        case SIP_HDR_TO:
        {
            sipHdrGenericNameParam_t* pRealHdr = oszalloc(sizeof(sipHdrGenericNameParam_t), sipHdrGenericNameParam_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_genericNameParam(pSipRawHdr, pSipRawHdr->end, false, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrOther_t
        case SIP_HDR_HIDE:
        case SIP_HDR_IDENTITY_INFO:
        case SIP_HDR_P_DCS_TRACE_PARTY_ID:
        case SIP_HDR_P_DCS_OSPS:
        case SIP_HDR_P_DCS_BILLING_INFO:
        case SIP_HDR_P_DCS_LAES:
        case SIP_HDR_P_DCS_REDIRECT:
        case SIP_HDR_RESPONSE_KEY:
        case SIP_HDR_WARNING:
			break;

        //sipHdrMultiNameValueList_t
        case SIP_HDR_P_CHARGING_FUNCTION_ADDRESSES:
        {
            sipHdrMultiNameValueList_t* pRealHdr = oszalloc(sizeof(sipHdrMultiNameValueList_t), sipHdrMultiNameValueList_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_multiNameValueList(pSipRawHdr, pSipRawHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrIntParam_t
        case SIP_HDR_RETRY_AFTER:
        case SIP_HDR_SESSION_EXPIRES:
        {
            sipHdrIntParam_t* pRealHdr = oszalloc(sizeof(sipHdrIntParam_t), sipHdrIntParam_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_intParam(pSipRawHdr, pSipRawHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        //sipHdrMultiVia_t
        case SIP_HDR_VIA:
        {
            sipHdrMultiVia_t* pRealHdr = oszalloc(sizeof(sipHdrMultiVia_t), sipHdrMultiVia_cleanup);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_via(pSipRawHdr, pSipRawHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osfree(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }

        default:
			break;
	}

EXIT:
	return pHdr;
}


//get the number of hdr values for a hdr line (one entry in a sip message)
uint8_t sipHdr_getHdrValueNum(sipHdrDecoded_t* pSipHdrDecoded)
{
	uint8_t hdrValueNum = 0;
 
	if(!pSipHdrDecoded)
	{
		return 0;
	}

	switch (pSipHdrDecoded->hdrCode)
	{
		//sipHdrMultiSlashValueParam_t
    	case SIP_HDR_ACCEPT:
			hdrValueNum =  pSipHdrDecoded->decodedHdr ? ((sipHdrMultiSlashValueParam_t*)pSipHdrDecoded->decodedHdr)->svpNum : 0;
			break;

		//sipHdrMultiGenericNameParam_t
    	case SIP_HDR_ACCEPT_CONTACT:
		case SIP_HDR_ALERT_INFO:
    	case SIP_HDR_CALL_INFO:
    	case SIP_HDR_ERROR_INFO:
    	case SIP_HDR_HISTORY_INFO:
    	case SIP_HDR_P_ASSERTED_IDENTITY:
    	case SIP_HDR_P_ASSOCIATED_URI:
    	case SIP_HDR_P_PREFERRED_IDENTITY:
    	case SIP_HDR_PATH:
    	case SIP_HDR_PERMISSION_MISSING:
    	case SIP_HDR_RECORD_ROUTE:
    	case SIP_HDR_ROUTE:
    	case SIP_HDR_SERVICE_ROUTE:
    	case SIP_HDR_TRIGGER_CONSENT:
			hdrValueNum = pSipHdrDecoded->decodedHdr ? ((sipHdrMultiGenericNameParam_t*)pSipHdrDecoded->decodedHdr)->gnpNum : 0;
			break;

		//sipHdrMultiValueParam_t
    	case SIP_HDR_ACCEPT_ENCODING:
    	case SIP_HDR_ACCEPT_LANGUAGE:
    	case SIP_HDR_P_ACCESS_NETWORK_INFO:
    	case SIP_HDR_P_VISITED_NETWORK_ID:
    	case SIP_HDR_REASON:
		case SIP_HDR_REJECT_CONTACT:
    	case SIP_HDR_SECURITY_CLIENT:
    	case SIP_HDR_SECURITY_SERVER:
    	case SIP_HDR_SECURITY_VERIFY:
			hdrValueNum = pSipHdrDecoded->decodedHdr ? ((sipHdrMultiValueParam_t*)pSipHdrDecoded->decodedHdr)->vpNum : 0;
            break;

		//sipHdrNameList_t
    	case SIP_HDR_ACCEPT_RESOURCE_PRIORITY:
    	case SIP_HDR_ALLOW:
    	case SIP_HDR_ALLOW_EVENTS:
    	case SIP_HDR_CONTENT_ENCODING:
    	case SIP_HDR_CONTENT_LANGUAGE:
    	case SIP_HDR_IN_REPLY_TO:
    	case SIP_HDR_P_EARLY_MEDIA:
    	case SIP_HDR_P_MEDIA_AUTHORIZATION:
    	case SIP_HDR_PROXY_REQUIRE:
    	case SIP_HDR_REQUEST_DISPOSITION:
    	case SIP_HDR_REQUIRE:
    	case SIP_HDR_RESOURCE_PRIORITY:
    	case SIP_HDR_SUPPORTED:
    	case SIP_HDR_UNSUPPORTED:
            hdrValueNum = pSipHdrDecoded->decodedHdr ? 1 : 0;
			break;

		//sipHdrValueParam_t
    	case SIP_HDR_ANSWER_MODE:
    	case SIP_HDR_CONTENT_DISPOSITION:
    	case SIP_HDR_ENCRYPTION:
    	case SIP_HDR_EVENT:
    	case SIP_HDR_IDENTITY:
    	case SIP_HDR_JOIN:
    	case SIP_HDR_P_ANSWER_STATE:
    	case SIP_HDR_PRIV_ANSWER_MODE:
    	case SIP_HDR_REFER_SUB:
    	case SIP_HDR_REPLACES:
    	case SIP_HDR_SUBSCRIPTION_STATE:
    	case SIP_HDR_TARGET_DIALOG:
			hdrValueNum = pSipHdrDecoded->decodedHdr ? 1 : 0;
			break;

		//sipHdrNameValueList_t
    	case SIP_HDR_AUTHENTICATION_INFO:
    	case SIP_HDR_P_CHARGING_VECTOR:
    	case SIP_HDR_PRIVACY:
            hdrValueNum = pSipHdrDecoded->decodedHdr ? 1 : 0;
            break;

		//sipHdrMethodParam_t
    	case SIP_HDR_AUTHORIZATION:
    	case SIP_HDR_PROXY_AUTHENTICATE:
    	case SIP_HDR_PROXY_AUTHORIZATION:
    	case SIP_HDR_WWW_AUTHENTICATE:
            hdrValueNum = pSipHdrDecoded->decodedHdr ? 1 : 0;
            break;

		//sipHdrStr_t
    	case SIP_HDR_CALL_ID:
    	case SIP_HDR_DATE:
    	case SIP_HDR_MIME_VERSION:
    	case SIP_HDR_ORGANIZATION:
    	case SIP_HDR_PRIORITY:
    	case SIP_HDR_RACK:
    	case SIP_HDR_SERVER:
    	case SIP_HDR_SIP_ETAG:
    	case SIP_HDR_SIP_IF_MATCH:
    	case SIP_HDR_SUBJECT:
    	case SIP_HDR_TIMESTAMP:
    	case SIP_HDR_USER_AGENT:
            hdrValueNum = pSipHdrDecoded->decodedHdr ? 1 : 0;
            break;

    	case SIP_HDR_CONTACT:
		{
			sipHdrMultiContact_t* pHdr = pSipHdrDecoded->decodedHdr;
			if(!pHdr)
			{
				hdrValueNum = 0;
			}
			else if(pHdr->isStar)
			{
				hdrValueNum = 1;
			}
			else
			{ 
            	hdrValueNum = pHdr->contactList.gnpNum;
			}
            break;
		}

		//sipHdrInt_t
    	case SIP_HDR_CONTENT_LENGTH:
    	case SIP_HDR_CSEQ:
    	case SIP_HDR_EXPIRES:
    	case SIP_HDR_FLOW_TIMER:
    	case SIP_HDR_MAX_BREADTH:
    	case SIP_HDR_MAX_FORWARDS:
    	case SIP_HDR_MIN_EXPIRES:
    	case SIP_HDR_MIN_SE:
    	case SIP_HDR_RSEQ:
            hdrValueNum = pSipHdrDecoded->decodedHdr ? 1 : 0;
            break;

		//sipHdrSlashValueParam_t
    	case SIP_HDR_CONTENT_TYPE:
            hdrValueNum = pSipHdrDecoded->decodedHdr ? 1 : 0;
            break;

		//sipHdrGenericNameParam_t
    	case SIP_HDR_FROM:
    	case SIP_HDR_P_CALLED_PARTY_ID:
    	case SIP_HDR_P_PROFILE_KEY:
    	case SIP_HDR_P_REFUSED_URI_LIST:
    	case SIP_HDR_P_SERVED_USER:
    	case SIP_HDR_P_USER_DATABASE:
    	case SIP_HDR_REFER_TO:
    	case SIP_HDR_REFERRED_BY:
    	case SIP_HDR_REPLY_TO:
    	case SIP_HDR_TO:
            hdrValueNum = pSipHdrDecoded->decodedHdr ? 1 : 0;
            break;

		//sipHdrOther_t
    	case SIP_HDR_HIDE:
    	case SIP_HDR_IDENTITY_INFO:
    	case SIP_HDR_P_DCS_TRACE_PARTY_ID:
    	case SIP_HDR_P_DCS_OSPS:
    	case SIP_HDR_P_DCS_BILLING_INFO:
    	case SIP_HDR_P_DCS_LAES:
    	case SIP_HDR_P_DCS_REDIRECT:
    	case SIP_HDR_RESPONSE_KEY:
    	case SIP_HDR_WARNING:
            hdrValueNum = pSipHdrDecoded->decodedHdr ? 1 : 0;
            break;

		//sipHdrMultiNameValueList_t
    	case SIP_HDR_P_CHARGING_FUNCTION_ADDRESSES:
            hdrValueNum = pSipHdrDecoded->decodedHdr ? ((sipHdrMultiNameValueList_t*)pSipHdrDecoded->decodedHdr)->nvNum : 0;
            break;

		//sipHdrIntParam_t
    	case SIP_HDR_RETRY_AFTER:
    	case SIP_HDR_SESSION_EXPIRES:
            hdrValueNum = pSipHdrDecoded->decodedHdr ? 1 : 0;
            break;

		//sipHdrMultiVia_t
    	case SIP_HDR_VIA:
            hdrValueNum = pSipHdrDecoded->decodedHdr ? ((sipHdrMultiVia_t*)pSipHdrDecoded->decodedHdr)->viaNum : 0;
            break;

		default:
			hdrValueNum = 0;
			break;
	}

	return hdrValueNum;
}


//get the length of the first hdr value.  See the sipHdr_posInfo_t for the definition of the first hdr value.  If a hdr can only have one value, or if is a no-decoded hdr (sipHdrType_other), return 0.  For these hdrs, can use the raw decoded hdr info to get the hdr length
//be noted the startPos in the sipHdr_posInfo_t gotten from this function may always be zero if the pSipHdrDecoded is decoded from sipMsgDecodedRawHdr_t.
osStatus_e sipHdr_getFirstHdrValuePosInfo(sipHdrDecoded_t* pSipHdrDecoded, sipHdr_posInfo_t* pTopPosInfo)
{
	osStatus_e status = OS_STATUS_OK;

    if(!pSipHdrDecoded || !pTopPosInfo)
    {
		logError("null pointer, pSipHdrDecoded=%p, pTopPosInfo=%p.", pSipHdrDecoded, pTopPosInfo);
        status = OS_ERROR_NULL_POINTER;
		goto EXIT;
    }

	pTopPosInfo->startPos = 0;
	pTopPosInfo->totalLen = 0;

    switch (pSipHdrDecoded->hdrCode)
    {
        //sipHdrMultiSlashValueParam_t
        case SIP_HDR_ACCEPT:
		{
			sipHdrMultiSlashValueParam_t* pHdr = pSipHdrDecoded->decodedHdr;
			if(pHdr && pHdr->pSVP)
			{
				*pTopPosInfo = pHdr->pSVP->hdrPos;
			}
            break;
		}
        //sipHdrMultiGenericNameParam_t
        case SIP_HDR_ACCEPT_CONTACT:
        case SIP_HDR_ALERT_INFO:
        case SIP_HDR_CALL_INFO:
        case SIP_HDR_ERROR_INFO:
        case SIP_HDR_HISTORY_INFO:
        case SIP_HDR_P_ASSERTED_IDENTITY:
        case SIP_HDR_P_ASSOCIATED_URI:
        case SIP_HDR_P_PREFERRED_IDENTITY:
        case SIP_HDR_PATH:
        case SIP_HDR_PERMISSION_MISSING:
        case SIP_HDR_RECORD_ROUTE:
        case SIP_HDR_ROUTE:
        case SIP_HDR_SERVICE_ROUTE:
        case SIP_HDR_TRIGGER_CONSENT:
		{
			sipHdrMultiGenericNameParam_t* pHdr = pSipHdrDecoded->decodedHdr;
            if(pHdr && pHdr->pGNP)
            {
                *pTopPosInfo = pHdr->pGNP->hdrPos;
            }
            break;
        }
        //sipHdrMultiValueParam_t
        case SIP_HDR_ACCEPT_ENCODING:
        case SIP_HDR_ACCEPT_LANGUAGE:
        case SIP_HDR_P_ACCESS_NETWORK_INFO:
        case SIP_HDR_P_VISITED_NETWORK_ID:
        case SIP_HDR_REASON:
        case SIP_HDR_REJECT_CONTACT:
        case SIP_HDR_SECURITY_CLIENT:
        case SIP_HDR_SECURITY_SERVER:
        case SIP_HDR_SECURITY_VERIFY:
		{
			sipHdrMultiValueParam_t* pHdr = pSipHdrDecoded->decodedHdr;
            if(pHdr && pHdr->pVP)
            {
                *pTopPosInfo = pHdr->pVP->hdrPos;
            }
            break;
        }
        //sipHdrNameList_t
        case SIP_HDR_ACCEPT_RESOURCE_PRIORITY:
        case SIP_HDR_ALLOW:
        case SIP_HDR_ALLOW_EVENTS:
        case SIP_HDR_CONTENT_ENCODING:
        case SIP_HDR_CONTENT_LANGUAGE:
        case SIP_HDR_IN_REPLY_TO:
        case SIP_HDR_P_EARLY_MEDIA:
        case SIP_HDR_P_MEDIA_AUTHORIZATION:
        case SIP_HDR_PROXY_REQUIRE:
        case SIP_HDR_REQUEST_DISPOSITION:
        case SIP_HDR_REQUIRE:
        case SIP_HDR_RESOURCE_PRIORITY:
        case SIP_HDR_SUPPORTED:
        case SIP_HDR_UNSUPPORTED:
            break;

        //sipHdrValueParam_t
        case SIP_HDR_ANSWER_MODE:
        case SIP_HDR_CONTENT_DISPOSITION:
        case SIP_HDR_ENCRYPTION:
        case SIP_HDR_EVENT:
        case SIP_HDR_IDENTITY:
        case SIP_HDR_JOIN:
        case SIP_HDR_P_ANSWER_STATE:
        case SIP_HDR_PRIV_ANSWER_MODE:
        case SIP_HDR_REFER_SUB:
        case SIP_HDR_REPLACES:
        case SIP_HDR_SUBSCRIPTION_STATE:
        case SIP_HDR_TARGET_DIALOG:
            break;

        //sipHdrNameValueList_t
        case SIP_HDR_AUTHENTICATION_INFO:
        case SIP_HDR_P_CHARGING_VECTOR:
        case SIP_HDR_PRIVACY:
            break;

        //sipHdrMethodParam_t
        case SIP_HDR_AUTHORIZATION:
        case SIP_HDR_PROXY_AUTHENTICATE:
        case SIP_HDR_PROXY_AUTHORIZATION:
        case SIP_HDR_WWW_AUTHENTICATE:
            break;

        //sipHdrStr_t
        case SIP_HDR_CALL_ID:
        case SIP_HDR_DATE:
        case SIP_HDR_MIME_VERSION:
        case SIP_HDR_ORGANIZATION:
        case SIP_HDR_PRIORITY:
        case SIP_HDR_RACK:
        case SIP_HDR_SERVER:
        case SIP_HDR_SIP_ETAG:
        case SIP_HDR_SIP_IF_MATCH:
        case SIP_HDR_SUBJECT:
        case SIP_HDR_TIMESTAMP:
        case SIP_HDR_USER_AGENT:
            break;

        case SIP_HDR_CONTACT:
        {
            sipHdrMultiContact_t* pHdr = pSipHdrDecoded->decodedHdr;
            if(pHdr && !pHdr->isStar && pHdr->contactList.pGNP)
			{
				*pTopPosInfo = pHdr->contactList.pGNP->hdrPos;
            }
            break;
        }
        //sipHdrInt_t
        case SIP_HDR_CONTENT_LENGTH:
        case SIP_HDR_CSEQ:
        case SIP_HDR_EXPIRES:
        case SIP_HDR_FLOW_TIMER:
        case SIP_HDR_MAX_BREADTH:
        case SIP_HDR_MAX_FORWARDS:
        case SIP_HDR_MIN_EXPIRES:
        case SIP_HDR_MIN_SE:
        case SIP_HDR_RSEQ:
            break;

        //sipHdrSlashValueParam_t
        case SIP_HDR_CONTENT_TYPE:
            break;

        //sipHdrGenericNameParam_t
        case SIP_HDR_FROM:
        case SIP_HDR_P_CALLED_PARTY_ID:
        case SIP_HDR_P_PROFILE_KEY:
        case SIP_HDR_P_REFUSED_URI_LIST:
        case SIP_HDR_P_SERVED_USER:
        case SIP_HDR_P_USER_DATABASE:
        case SIP_HDR_REFER_TO:
        case SIP_HDR_REFERRED_BY:
        case SIP_HDR_REPLY_TO:
        case SIP_HDR_TO:
            break;

        //sipHdrOther_t
        case SIP_HDR_HIDE:
        case SIP_HDR_IDENTITY_INFO:
        case SIP_HDR_P_DCS_TRACE_PARTY_ID:
        case SIP_HDR_P_DCS_OSPS:
        case SIP_HDR_P_DCS_BILLING_INFO:
        case SIP_HDR_P_DCS_LAES:
        case SIP_HDR_P_DCS_REDIRECT:
        case SIP_HDR_RESPONSE_KEY:
        case SIP_HDR_WARNING:
            break;

        //sipHdrMultiNameValueList_t
        case SIP_HDR_P_CHARGING_FUNCTION_ADDRESSES:
		{
            sipHdrMultiNameValueList_t* pHdr = pSipHdrDecoded->decodedHdr;
            if(pHdr && pHdr->pNV)
            {
                *pTopPosInfo = pHdr->pNV->hdrPos;
            }
            break;
        }

        //sipHdrIntParam_t
        case SIP_HDR_RETRY_AFTER:
        case SIP_HDR_SESSION_EXPIRES:
            break;

        //sipHdrMultiVia_t
        case SIP_HDR_VIA:
        {
            sipHdrMultiVia_t* pHdr = pSipHdrDecoded->decodedHdr;
            if(pHdr && pHdr->pVia)
            {
                *pTopPosInfo = pHdr->pVia->hdrPos;
            }
            break;
        }

        default:
            break;
    }

EXIT:
    return status;
}


void sipHdrIntParam_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	osList_delete(&((sipHdrIntParam_t*)data)->paramList);
}


void sipHdrNameList_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	osList_delete(&((sipHdrNameList_t*)data)->nameList);
}


void sipHdrNameValueList_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	osfree(((sipHdrNameValueList_t*)data)->pNVP);
	osList_delete(&((sipHdrNameValueList_t*)data)->nvpList);
}


void sipHdrNameValueListDecoded_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	sipHdrNameValueList_cleanup(&((sipHdrNameValueListDecoded_t*)data)->hdrValue);
}


void sipHdrMultiNameValueList_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	osfree(((sipHdrMultiNameValueList_t*)data)->pNV);
	osList_delete(&((sipHdrMultiNameValueList_t*)data)->nvList);
}


void sipHdrValueParam_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	sipHdrNameValueList_cleanup(&((sipHdrValueParam_t*)data)->nvParamList);
}


void sipHdrValueParamDecoded_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	sipHdrValueParam_cleanup(&((sipHdrValueParamDecoded_t*)data)->hdrValue);
}


void sipHdrMethodParam_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

    sipHdrNameValueList_cleanup(&((sipHdrMethodParam_t*)data)->nvParamList);
}


void sipHdrSlashValueParam_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

    sipHdrNameValueList_cleanup(&((sipHdrSlashValueParam_t*)data)->paramList);
}	


void sipHdrSlashValueParamDecoded_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	sipHdrSlashValueParam_cleanup(&((sipHdrSlashValueParamDecoded_t*)data)->hdrValue);
}
	

void sipHdrMultiSlashValueParam_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	osfree(&((sipHdrMultiSlashValueParam_t*)data)->pSVP);
	osList_delete(&((sipHdrMultiSlashValueParam_t*)data)->svpList);
}


void sipHdrGenericNameParam_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

    sipHdrGenericNameParam_t* pGNP = data;

    sipUri_cleanup(&pGNP->uri);
    osList_delete(&pGNP->genericParam);
}


void sipHdrGenericNameParamDecoded_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

    sipHdrGenericNameParamDecoded_t* pGNP = data;
    sipUri_cleanup(&pGNP->hdrValue.uri);
    osList_delete(&pGNP->hdrValue.genericParam);
}

void sipHdrMultiGenericNameParam_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

    sipHdrMultiGenericNameParam_t* pMGNP = data;
    osfree(pMGNP->pGNP);

	osList_delete(&pMGNP->gnpList);
}


void sipHdrMultiValueParam_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	osfree(((sipHdrMultiValueParam_t*)data)->pVP);
	osList_delete(&((sipHdrMultiValueParam_t*)data)->vpList);
}


void sipHdrMultiContact_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	sipHdrMultiGenericNameParam_cleanup(&((sipHdrMultiContact_t*)data)->contactList);
}


void sipHdrVia_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	sipHdrVia_t* pHdr = data;

	osfree(pHdr->pBranch);
logError("to-remove, VIA-MEMORY, 5, pHdr->viaParamList.head=%p", pHdr->viaParamList.head);
	osList_delete(&pHdr->viaParamList);
logError("to-remove, VIA-MEMORY, 6");
}


void sipHdrViaDecoded_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	sipHdrVia_cleanup(&((sipHdrViaDecoded_t*)data)->hdrValue);
}


void sipHdrMultiVia_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	sipHdrMultiVia_t* pHdr = data;
	osfree(pHdr->pVia);
	osList_delete(&pHdr->viaList);
}
