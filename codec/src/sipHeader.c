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



void* sipHdrParseByName(osMBuf_t* pSipMsgHdr, sipHdrName_e hdrNameCode)
{
	void* pHdr = NULL;
	switch(hdrNameCode)
	{
		case SIP_HDR_ACCEPT:
			break;
    	case SIP_HDR_ACCEPT_CONTACT:
		{
			sipHdrAcceptedContact_t* pRealHdr=sipHdrAC_alloc();
			if(!pRealHdr)
			{
				goto EXIT;
			}

			if(sipHdr_acceptedContact(pSipMsgHdr, pSipMsgHdr->end, pRealHdr) != OS_STATUS_OK)
			{
				osMem_deref(pRealHdr);
				goto EXIT;
			}

			pHdr = pRealHdr;
			break;
		}
        case SIP_HDR_ACCEPT_ENCODING:
			break;
        case SIP_HDR_ACCEPT_LANGUAGE:
			break;
        case SIP_HDR_ACCEPT_RESOURCE_PRIORITY:
			break;
        case SIP_HDR_ALERT_INFO:
			break;
        case SIP_HDR_ALLOW:
        case SIP_HDR_ALLOW_EVENTS:
        case SIP_HDR_SUPPORTED:
		{
			bool isCaps = false;
            osList_t* pRealHdr=osMem_alloc(sizeof(osList_t), NULL);
            if(!pRealHdr)
            {
                goto EXIT;
            }

			if(hdrNameCode == SIP_HDR_ALLOW)
			{
				isCaps = true;
			}
			osList_init(pRealHdr);
            if(sipParserHdr_nameList(pSipMsgHdr, pSipMsgHdr->end, isCaps, pRealHdr) != OS_STATUS_OK)
            {
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
			break;
		}
        case SIP_HDR_ANSWER_MODE:
			break;
        case SIP_HDR_AUTHENTICATION_INFO:
			break;
        case SIP_HDR_AUTHORIZATION:
			break;
        case SIP_HDR_CALL_ID:
        case SIP_HDR_USER_AGENT:
		{
			osPointerLen_t* pRealHdr = osMem_alloc(sizeof(osPointerLen_t), NULL);
			if(!pRealHdr)
            {
                goto EXIT;
            }

			if(sipParserHdr_str(pSipMsgHdr, pSipMsgHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
		}
        case SIP_HDR_CALL_INFO:
			break;
        case SIP_HDR_CONTACT:
		{
            sipHdrContact_t* pRealHdr=sipHdrContact_alloc();
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_contact(pSipMsgHdr, pSipMsgHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
		}
        case SIP_HDR_CONTENT_DISPOSITION:
			break;
        case SIP_HDR_CONTENT_ENCODING:
			break;
        case SIP_HDR_CONTENT_LANGUAGE:
			break;
        case SIP_HDR_CONTENT_LENGTH:
        case SIP_HDR_EXPIRES:
        case SIP_HDR_MAX_FORWARDS:
        case SIP_HDR_SESSION_EXPIRES:
        case SIP_HDR_MIN_EXPIRES:
		{
            uint32_t* pRealHdr=osMem_alloc(sizeof(uint32_t), NULL);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_lenTime(pSipMsgHdr, pSipMsgHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
		}
        case SIP_HDR_CONTENT_TYPE:
		{
            sipHdrContentType_t* pRealHdr=sipHdrContentType_alloc();
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_contentType(pSipMsgHdr, pSipMsgHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }
        case SIP_HDR_CSEQ:
		{
            sipHdrCSeq_t* pRealHdr=osMem_alloc(sizeof(sipHdrCSeq_t), NULL);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_cSeq(pSipMsgHdr, pSipMsgHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
		}
        case SIP_HDR_DATE:
        {
            sipHdrDate_t* pRealHdr=osMem_alloc(sizeof(sipHdrDate_t), NULL);
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_Date(pSipMsgHdr, pSipMsgHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }
        case SIP_HDR_ENCRYPTION:
			break;
        case SIP_HDR_ERROR_INFO:
			break;
        case SIP_HDR_EVENT:
			break;
        //case SIP_HDR_EXPIRES:
        case SIP_HDR_FLOW_TIMER:
			break;
        case SIP_HDR_FROM:
		case SIP_HDR_TO:
		{
            sipHdr_fromto_t* pRealHdr=sipHdrFromto_alloc();
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_fromto(pSipMsgHdr, pSipMsgHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
		}
        case SIP_HDR_HIDE:
			break;
        case SIP_HDR_HISTORY_INFO:
			break;
        case SIP_HDR_IDENTITY:
			break;
        case SIP_HDR_IDENTITY_INFO:
			break;
        case SIP_HDR_IN_REPLY_TO:
			break;
        case SIP_HDR_JOIN:
			break;
        case SIP_HDR_MAX_BREADTH:
			break;
        //case SIP_HDR_MAX_FORWARDS:
        case SIP_HDR_MIME_VERSION:
			break;
        //case SIP_HDR_MIN_EXPIRES:
        case SIP_HDR_MIN_SE:
			break;
        case SIP_HDR_ORGANIZATION:
			break;
        case SIP_HDR_P_ACCESS_NETWORK_INFO:
        {
            sipHdrPani_t* pRealHdr=sipHdrPani_alloc();
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipHdr_pani(pSipMsgHdr, pSipMsgHdr->end, pRealHdr) != OS_STATUS_OK)
            {
				logError("status is not OK");
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }
        case SIP_HDR_P_ANSWER_STATE:
			break;
        case SIP_HDR_P_ASSERTED_IDENTITY:
		case SIP_HDR_P_PREFERRED_IDENTITY:
        {
            sipHdrNameaddrAddrspec_t* pRealHdr = sipHdrNameaddrAddrspec_alloc();
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_nameaddrAddrSpec(pSipMsgHdr, pSipMsgHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
        }
        case SIP_HDR_P_ASSOCIATED_URI:
			break;
        case SIP_HDR_P_CALLED_PARTY_ID:
			break;
        case SIP_HDR_P_CHARGING_FUNCTION_ADDRESSES:
			break;
        case SIP_HDR_P_CHARGING_VECTOR:
        case SIP_HDR_P_DCS_TRACE_PARTY_ID:
        case SIP_HDR_P_DCS_OSPS:
        case SIP_HDR_P_DCS_BILLING_INFO:
        case SIP_HDR_P_DCS_LAES:
        case SIP_HDR_P_DCS_REDIRECT:
        case SIP_HDR_P_EARLY_MEDIA:
        case SIP_HDR_P_MEDIA_AUTHORIZATION:
			break;
       // case SIP_HDR_P_PREFERRED_IDENTITY:
        case SIP_HDR_P_PROFILE_KEY:
        case SIP_HDR_P_REFUSED_URI_LIST:
        case SIP_HDR_P_SERVED_USER:
        case SIP_HDR_P_USER_DATABASE:
        case SIP_HDR_P_VISITED_NETWORK_ID:
        case SIP_HDR_PATH:
        case SIP_HDR_PERMISSION_MISSING:
        case SIP_HDR_PRIORITY:
        case SIP_HDR_PRIV_ANSWER_MODE:
        case SIP_HDR_PRIVACY:
        case SIP_HDR_PROXY_AUTHENTICATE:
        case SIP_HDR_PROXY_AUTHORIZATION:
        case SIP_HDR_PROXY_REQUIRE:
        case SIP_HDR_RACK:
        case SIP_HDR_REASON:
			break;
        case SIP_HDR_RECORD_ROUTE:
        case SIP_HDR_ROUTE:
		{
			bool isNameaddrOnly = false;
            sipHdrRoute_t* pRealHdr=sipHdrRoute_alloc();
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_route(pSipMsgHdr, pSipMsgHdr->end, isNameaddrOnly, &pRealHdr->routeList) != OS_STATUS_OK)
            {
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
		}
        case SIP_HDR_REFER_SUB:
        case SIP_HDR_REFER_TO:
        case SIP_HDR_REFERRED_BY:
        case SIP_HDR_REJECT_CONTACT:
        case SIP_HDR_REPLACES:
        case SIP_HDR_REPLY_TO:
        case SIP_HDR_REQUEST_DISPOSITION:
        case SIP_HDR_REQUIRE:
        case SIP_HDR_RESOURCE_PRIORITY:
        case SIP_HDR_RESPONSE_KEY:
        case SIP_HDR_RETRY_AFTER:
        //case SIP_HDR_ROUTE:
        case SIP_HDR_RSEQ:
        case SIP_HDR_SECURITY_CLIENT:
        case SIP_HDR_SECURITY_SERVER:
        case SIP_HDR_SECURITY_VERIFY:
        case SIP_HDR_SERVER:
        case SIP_HDR_SERVICE_ROUTE:
        //case SIP_HDR_SESSION_EXPIRES:
        case SIP_HDR_SIP_ETAG:
        case SIP_HDR_SIP_IF_MATCH:
        case SIP_HDR_SUBJECT:
        case SIP_HDR_SUBSCRIPTION_STATE:
        //case SIP_HDR_SUPPORTED:
        case SIP_HDR_TARGET_DIALOG:
        case SIP_HDR_TIMESTAMP:
        //case SIP_HDR_TO:
        case SIP_HDR_TRIGGER_CONSENT:
        case SIP_HDR_UNSUPPORTED:
        //case SIP_HDR_USER_AGENT:
			break;
        case SIP_HDR_VIA:
		{
            sipHdrVia_t* pRealHdr=sipHdrVia_alloc();
            if(!pRealHdr)
            {
                goto EXIT;
            }

            if(sipParserHdr_via(pSipMsgHdr, pSipMsgHdr->end, pRealHdr) != OS_STATUS_OK)
            {
                osMem_deref(pRealHdr);
                goto EXIT;
            }

            pHdr = pRealHdr;
            break;
		}
        case SIP_HDR_WARNING:
        case SIP_HDR_WWW_AUTHENTICATE:
			break;
		default:
			break;
	}
EXIT:
	return pHdr;
}


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

	osList_t* pMsgHdrList = &pSipDecoded->msgHdrList;
	osListElement_t* pMsgHdrLE = pMsgHdrList->head;
	while(pMsgHdrLE)
	{
		sipHdrInfo_t* pHdrInfo = pMsgHdrLE->data;

		if(hdrNameCode != pHdrInfo->nameCode)
		{
			pMsgHdrLE = pMsgHdrLE->next;
			continue;
		}

		//find a match header
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
	}

	logInfo("the input sip message does not contain the required sip hdr (%d).", hdrNameCode);
	*pPos = 0;
	*pLen = 0;

EXIT:		
	return status;
}


sipHdrEncode_h sipHdrGetEncode( sipHdrName_e hdrName)
{
	return sipHdrCreateArray[hdrName].encodeHandler;
}


#if 0
/* isHdrOneValue=1, a hdr only has one value
 * isPreExist=1, a hdr is mandatory for a sip Message, like max-forwards, from, etc., thus it has to exist in the incoming sip message
 * pHdrData is the input to generate hdr
 * pExtraData is overloaded.  It is a extra input to generate hdr, it is also to be added in the pModifyInfo->pdata
 */
osStatus_e sipHdrCreateModifyInfo(sipHdrModifyInfo_t* pModifyInfo, sipMsgDecoded_t* sipMsgInDecoded, sipHdrName_e hdrName, sipHdrModifyCtrl_t modifyCtrl, void* pHdrData, void* pExtraData)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pModifyInfo || !pHdrData)
    {
        logError("null pointer, pModifyInfo=%p, pHdrData=%p.", pModifyInfo, pHdrData);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    if(modifyCtrl.isProxy)
	{
		if(!pModifyInfo->pOrigSipDecoded)
    	{
        	logError("null pointer, pModifyInfo->pOrigSipDecoded.");
        	status = OS_ERROR_NULL_POINTER;
        	goto EXIT;
    	}

		//apply basic sanity check.
		if((pModifyInfo->modType == SIP_HDR_MODIFY_TYPE_ADD || pModifyInfo->modType == SIP_HDR_MODIFY_TYPE_REMOVE) && isPreExist && isHdrOneValue)
		{
			logError("incorrect input, modType=%d && isPreExist && isHdrOneValue.", pModifyInfo->modType);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

    	pModifyInfo->modStatus = SIP_HDR_MODIFY_STATUS_HDR_EXIST;
    	status = sipHdrGetPosLen(pModifyInfo->pOrigSipDecoded, hdrName, modifyCtrl.idx, &pModifyInfo->origHdrPos, &pModifyInfo->origHdrLen);
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
        	if(isPreExist)
        	{
            	logError("the input sip message does not have mandatory hdr (%d).", hdrName);
            	status = OS_ERROR_EXT_INVALID_VALUE;
            	goto EXIT;
        	}
        	else if (pModifyInfo->modType != SIP_HDR_MODIFY_TYPE_REMOVE)
        	{
            	//if  hdr does not exist, add in front of callId
            	status = sipHdrGetPosLen(pModifyInfo->pOrigSipDecoded, SIP_HDR_CALL_ID, modifyCtrl.idx, &pModifyInfo->origHdrPos, &pModifyInfo->origHdrLen);
            	if(status != OS_STATUS_OK || pModifyInfo->origHdrPos == 0)
            	{
                	logError("sipHdrGetPosLen() fails.");
                	status = !pModifyInfo->origHdrPos ? OS_ERROR_EXT_INVALID_VALUE : status;
                	goto EXIT;
            	}
        	}
    	}
	}

    pModifyInfo->pData = pExtraData;

    switch(pModifyInfo->modType)
    {
        case SIP_HDR_MODIFY_TYPE_ADD:
            if(modifyCtrl.isProxy && modifyCtrl.isHdrOneValue && pModifyInfo->modStatus == SIP_HDR_MODIFY_STATUS_HDR_EXIST)
            {
                logInfo("try to add hdr (%d) while the same header already exists, isHdrOneValue=1.", hdrName);
                goto EXIT;
            }

		case SIP_HDR_MODIFY_TYPE_REPLACE:
		{
			sipHdrCreate_h hdrCreate = sipHdrCreateArray[hdrName].createHandler;
			if(hdrCreate)
			{
				status = hdrCreate(pModifyInfo->pHdr, pHdrData, pExtraData);
				if(status != OS_STATUS_OK)
				{
					logError("SIP_HDR_MODIFY_TYPE_ADD/REPLACE, hdrCreate() fails. hdrName(%d).", hdrName);
					status = OS_ERROR_INVALID_VALUE;
                	goto EXIT;
            	}
			}

			if(sipHdrCreateArray[hdrName].encodeHandler == NULL)
			{
				logError("SIP_HDR_MODIFY_TYPE_ADD, encodeHandler is NULL. hdrName(%d).", hdrName);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            pModifyInfo->sipHdrEncode_handler = sipHdrCreateArray[hdrName].encodeHandler;
            break;
		}
        case SIP_HDR_MODIFY_TYPE_REMOVE:
            if(pModifyInfo->modStatus == SIP_HDR_MODIFY_STATUS_HDR_NOT_EXIST)
            {
                logInfo("try to remove hdrName(%d), but it does not exist.", hdrName);
                goto EXIT;
            }

            pModifyInfo->sipHdrEncode_handler = NULL;
            break;
        default:
            logError("pModifyInfo->modType(%d) is not handled.", pModifyInfo->modType);
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
    }

EXIT:
	return status;
}
#endif


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

//get decoded hdr based on hdrCode and idx (if multiple values for a hdr, which value to get)
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

    osList_t* pMsgHdrList = &pSipInDecoded->msgHdrList;
	sipHdrInfo_t* pHdrInfo = NULL;
    osListElement_t* pMsgHdrLE = pMsgHdrList->head;
    while(pMsgHdrLE)
    {
        pHdrInfo = pMsgHdrLE->data;
        if(!pHdrInfo)
        {
            logError("a list element data is null in msgHdrList.");
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }

        if(pHdrInfo->nameCode == hdrCode)
        {
			switch(hdrCode)
			{
				//for TimeLen
				case SIP_HDR_CONTENT_LENGTH:
				case SIP_HDR_EXPIRES:
				case SIP_HDR_MAX_FORWARDS:
				case SIP_HDR_MIN_EXPIRES:
				case SIP_HDR_SESSION_EXPIRES:
					pHdr->decodedIntValue = pHdrInfo->decodedIntValue;
					debug("to remove, decodedIntValue=%d", pHdr->decodedIntValue);
					goto EXIT;
					break;
				//for other unique headers
				case SIP_HDR_CALL_ID:
					pHdr->decodedValue = pHdrInfo->decodedValue;
					goto EXIT;
					break;
				default:
				{
            		osList_t* pDecodedList = &pHdrInfo->decodedHdrList;
            		osListElement_t* pDecodedLE = pDecodedList->head;

					if(!pDecodedLE)
					{
						logError("decodedHdrList has no decocee value for nameCode=%d.", hdrCode);
						status = OS_ERROR_INVALID_VALUE;
						goto EXIT;
					}

            		if(idx == 0)
            		{
						osList_t* pList = pDecodedLE->data;
						osListElement_t* pLE = pList->head;
						pHdr->decodedValue = pLE->data;
						goto EXIT;
					}
					else if(idx == SIP_MAX_SAME_HDR_NUM)
					{
						osList_t* pList = pDecodedList->tail->data;
						osListElement_t* pLE = pList->tail;
						pHdr->decodedValue = pLE->data;
						goto EXIT;
					}
					else
					{
						int i=0;
						while(pDecodedLE)
						{
							osList_t* pList = pDecodedLE->data;
							osListElement_t* pLE = pList->head;
							while(pLE)
							{
								if (i++ == idx)
								{
									pHdr->decodedValue = pLE->data;
									goto EXIT;
								}

								pLE = pLE->next;
							}

							pDecodedLE = pDecodedLE->next;
						}

						logError("the idx (%d) exceeds the stored hdr values (%d).", idx, i);
						status = OS_ERROR_INVALID_VALUE;
						goto EXIT;
					}
				}
				break;
			}
		}
		pMsgHdrLE = pMsgHdrLE->next;
	}

	logInfo("the hdr (%d) is not found in the decodedMsg.", hdrCode);
	
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

    osList_t* pMsgHdrList = &pSipInDecoded->msgHdrList;
    osListElement_t* pMsgHdrLE = pMsgHdrList->head;
    while(pMsgHdrLE)
    {
        pHdrInfo = pMsgHdrLE->data;
        if(!pHdrInfo)
        {
            logError("a list element data is null in msgHdrList.");
            goto EXIT;
        }

        if(pHdrInfo->nameCode == hdrCode)
        {
			break;
		}

		pMsgHdrLE = pMsgHdrLE->next;
		pHdrInfo = NULL;
	}

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

    osList_t* pMsgHdrList = &pSipInDecoded->msgHdrList;
    sipHdrInfo_t* pHdrInfo = NULL;
    osListElement_t* pMsgHdrLE = pMsgHdrList->head;
    while(pMsgHdrLE)
    {
        pHdrInfo = pMsgHdrLE->data;
        if(!pHdrInfo)
        {
            logError("a list element data is null in msgHdrList.");
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }

        if(pHdrInfo->nameCode == hdrCode)
		{
			pHdrRawList = &pHdrInfo->rawHdr;
			break;
		}

		pMsgHdrLE = pMsgHdrLE->next;
	}

EXIT:
	return pHdrRawList;			
}	 
