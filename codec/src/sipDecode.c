#include <string.h>
#include "sipHeader.h"
#include "osDebug.h"
#include"osMemory.h"
#include "osMBuf.h"
#include "osPL.h"
#include "sipHdrGperfHash.h"
#include "sipHeader.h"
//#include "sipHdrVia.h"		
//#include "sipHdrMisc.h"


//static sipHdrInfo_t* sipMsg_getHdrInfo(osList_t* pMsgHdrList, sipHdrName_e hdrNameCode);
//static void sipMsgDecodedRawHdr_delete(void* data);
static void sipMsgDecoded_delete(void *data);
static void sipHdrListMemDestroy(void* data);
static osStatus_e sipGetHdrRawValue(osMBuf_t* pSipMsg, sipRawHdr_t* pSipHeader, bool* isEOH);


sipMsgDecoded_t* sipMsg_allocMsgDecoded(osMBuf_t* pSipMsg)
{
	sipMsgDecoded_t* pSipMsgDecoded = osMem_zalloc(sizeof(sipMsgDecoded_t), sipMsgDecoded_delete);

	if(!pSipMsgDecoded)
	{
		return NULL;
	}

//	pSipMsgDecoded->sipMsg = osMBuf_allocRef(pSipMsg);
	pSipMsgDecoded->hdrNum = 0;

	return pSipMsgDecoded;
}


void sipMsg_deallocMsgDecoded(void *pData)
{
	osMem_deref(pData);
}


osStatus_e sipGetHdrName(osMBuf_t* pSipMsg, sipRawHdr_t* pSipHeader)
{
    if(!pSipMsg || !pSipHeader)
    {
        logError("Null pointer, pSipMsg=%p, pSipHeader=%p", pSipMsg, pSipHeader);
        return OS_ERROR_NULL_POINTER;
    }

    char* pSipHeaderText = (char*) &pSipMsg->buf[pSipMsg->pos];
    size_t origPos = pSipMsg->pos;
	size_t nSP=0;

    for(pSipHeaderText; pSipMsg->pos < pSipMsg->end; ++pSipHeaderText, ++pSipMsg->pos)
    {
		mdebug(LM_SIPP, "debug, *pSipHeaderText=%c", *pSipHeaderText); 
		if(*pSipHeaderText == ':')
		{
			if(pSipMsg->pos == origPos)
			{
				logError("empty header name");
                return OS_ERROR_INVALID_VALUE;
            }
            else
            {
                osPL_setStr(&pSipHeader->name, (const char*) &pSipMsg->buf[origPos], pSipMsg->pos - origPos - nSP);
                pSipHeader->nameCode = sipHdr_getNameCode(&pSipHeader->name);
				pSipHeader->namePos = origPos;
                if(pSipHeader->nameCode == SIP_HDR_NONE)
                {
                    logError("invalid sip header name, name=%r", &pSipHeader->name);
                    return OS_ERROR_INVALID_VALUE;
                }

				++pSipMsg->pos;
			
				mdebug(LM_SIPP, "hdr name=%r, namePos=%ld", &pSipHeader->name, pSipHeader->namePos);
                return OS_STATUS_OK;
            }
        }
		else if(*pSipHeaderText == ' ' || *pSipHeaderText == '\t')
		{
			nSP++;
		}
		else if(nSP !=0)
		{
			logError("Find empty spaces in the hader name");
			return OS_ERROR_INVALID_VALUE;
		}	
	}

	logError("Do not find header name");
	return OS_ERROR_INVALID_VALUE;
}


osStatus_e sipGetHdrRawValue(osMBuf_t* pSipMsg, sipRawHdr_t* pSipHeader, bool* isEOH)
{
    if(!pSipMsg || !pSipHeader)
    {
        logError("Null pointer, pSipMsg=%p, pSipHeader=%p", pSipMsg, pSipHeader);
        return OS_ERROR_NULL_POINTER;
    }

    char* pSipHeaderText = (char*) &pSipMsg->buf[pSipMsg->pos];
    size_t origPos = pSipMsg->pos;
    bool isHeaderValueTop = true;
	size_t nSP=0;

	*isEOH = false;

    for(pSipHeaderText; pSipMsg->pos < pSipMsg->end; ++pSipHeaderText, ++pSipMsg->pos)
    {
        if(isHeaderValueTop && (*pSipHeaderText == ' ' || *pSipHeaderText == '\t'))
        {
            nSP++;
            continue;
        }

        isHeaderValueTop = false;
        if(*pSipHeaderText == '\r')
        {
            if(++pSipMsg->pos < pSipMsg->end && *(++pSipHeaderText) != '\n')
            {
                logError("\\r in a SIP header is not followed by \\n, header=%r", &pSipHeader->name);
                return OS_ERROR_INVALID_VALUE;
            }
            else
            {
				++pSipMsg->pos;
				++pSipHeaderText;
				if((pSipMsg->pos +2) <= pSipMsg->end)
				{
					if(*pSipHeaderText == ' ' || *pSipHeaderText == '\t')
					{
						//it is OK that a header value is spreaded into multiple lines, with each line starts with at least a empty space or tab
						continue;
					}

                    osPL_setStr(&pSipHeader->value, (const char*) &pSipMsg->buf[origPos + nSP], pSipMsg->pos - origPos - nSP - 2);
                    pSipHeader->valuePos = origPos + nSP;
                    mdebug(LM_SIPP, "hdr value=%r, valuePos=%ld", &pSipHeader->value, pSipHeader->valuePos);

					if(!strncmp(pSipHeaderText, "\r\n", 2))
					{
						pSipMsg->pos +=2;
						pSipHeaderText +=2;
						*isEOH = true;
					}
					return OS_STATUS_OK;
				}
				else
				{
					logError("sip message does not end with \\r\\n");
                    return OS_ERROR_INVALID_VALUE;
                }
            }
        }
    }

    logError("You shall not be here, something is wrong.");
    return OS_ERROR_INVALID_VALUE;
}




/*
 * decode one header
 * pSipHeader: output, filled with the header's name and value information
 */
osStatus_e sipDecodeOneHdrRaw(osMBuf_t* pSipMsg, sipRawHdr_t* pSipHeader, bool* isEOH)
{
	mDEBUG_BEGIN(LM_SIPP)
	osStatus_e status;

	if(!pSipMsg || !pSipHeader)
	{
		logError("Null pointer, pSipMsg=%p, pSipHeader=%p", pSipMsg, pSipHeader);
		return OS_ERROR_NULL_POINTER;
	}

	status = sipGetHdrName(pSipMsg, pSipHeader);
	if(status != OS_STATUS_OK)
	{
		goto EXIT;
	}

	status = sipGetHdrRawValue(pSipMsg, pSipHeader, isEOH);

EXIT:
	mDEBUG_END(LM_SIPP)
	return status;
}


/*
 * find multiple headers in the sip message buffer
 * pSipHeader: input/output, the header name has been pre-filled to specify the headers to be found.
 * when the function return, the pSipMsg mbuf pos is set back to the original place
 * the original pos shall always be the starting of a sip message's header list
 */
osStatus_e sipFindHdrs(osMBuf_t* pSipMsg, sipHdrInfo_t* sipHdrArray, uint8_t headerNum)
{
	osStatus_e status = OS_STATUS_OK;
	
	if(!pSipMsg || !sipHdrArray || !headerNum)
    {
        logError("Null pointer, pSipMsg=%p, sipHdrArray=%p, headerNum=%d", pSipMsg, sipHdrArray, headerNum);
        status = OS_ERROR_NULL_POINTER;
		goto EXIT;
    }

    for(int i=0; i< headerNum; i++)
    {
		if(sipHdrArray[i].nameCode == SIP_HDR_NONE)
        {
        	logError("the header namecode is not set, i=%d, headerNum=%d", i, headerNum);
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }
	}


	size_t origPos = pSipMsg->pos;
	bool isEOH = false;
	sipRawHdr_t sipHdr;

	while (!isEOH)
	{
		if(sipDecodeOneHdrRaw(pSipMsg, &sipHdr, &isEOH) != OS_STATUS_OK)
		{
			logError("SIP HDR decode error.");
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		for(int i=0; i<headerNum; i++)
		{
    		if( sipHdr.nameCode == sipHdrArray[i].nameCode)
        	{
				mdebug(LM_SIPP, "nameCode=%d, isEOH=%d", sipHdr.nameCode, isEOH);
				sipRawHdr_t* pMatchedSipHdr = osMem_dalloc(&sipHdr, sizeof(sipRawHdr_t), NULL);
				if(sipHdrArray[i].rawHdr.pRawHdr == NULL)
				{
					sipHdrArray[i].rawHdr.pRawHdr = pMatchedSipHdr;
					sipHdrArray[i].rawHdr.rawHdrNum = 1;
				}
				else
				{
            		osList_append(&sipHdrArray[i].rawHdr.rawHdrList, pMatchedSipHdr);
					++sipHdrArray[i].rawHdr.rawHdrNum;
				}
				break;
			}
		}
	}

EXIT:
	if(status != OS_STATUS_OK)
	{
		for(int i=0; i<headerNum; i++)
		{
			osMem_deref(sipHdrArray[i].rawHdr.pRawHdr);
			osList_delete(&sipHdrArray[i].rawHdr.rawHdrList);
			sipHdrArray[i].rawHdr.rawHdrNum = 0;
		}
	}
	
	pSipMsg->pos = origPos;
	return status;
}


osStatus_e sipDecode_getTopRawHdr(osMBuf_t* pSipMsg, sipRawHdr_t* pRawHdr, sipHdrName_e hdrCode)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pRawHdr)
    {
        logError("Null pointer, pSipMsg=%p, pRawHdr=%p", pSipMsg, pRawHdr);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    size_t origPos = pSipMsg->pos;
    bool isEOH = false;
//    sipRawHdr_t sipHdr;
    while (!isEOH)
    {
        if(sipDecodeOneHdrRaw(pSipMsg, pRawHdr, &isEOH) != OS_STATUS_OK)
        {
            logError("SIP HDR decode error.");
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }

        if( pRawHdr->nameCode == hdrCode)
        {
			goto EXIT;
		}
    }

	status = OS_ERROR_INVALID_VALUE;

EXIT:
	if(status != OS_ERROR_NULL_POINTER)
	{
    	pSipMsg->pos = origPos;
	}

    return status;
}



size_t sipHdr_getLen(sipRawHdr_t* pSipHdr)
{
	if(!pSipHdr)
	{
		logError("pSipHdr is NULL");
		return 0;
	}

	return (pSipHdr->valuePos - pSipHdr->namePos + pSipHdr->value.l +2);
}


sipHdrName_e sipHdr_getNameCode(osPointerLen_t* hdrName)
{
	if(!hdrName)
	{
		logError("hdrName is NULL");
		return SIP_HDR_NONE;
	}

	const char* fullName = hdrName->p;
	size_t fullNameLen = hdrName->l;
	if(hdrName->l == 1)
	{
		fullName = sipHdr_getFullName(*hdrName->p);
		fullNameLen = strlen(fullName);
	}

	struct gperfSipHdrName* pName = gperfSipHdrLookup(fullName, fullNameLen);
	if(pName == NULL)
	{
		logError("could not find nameCode for hdrName=%r, nameLen=%ld", hdrName, fullNameLen);
		return SIP_HDR_X;
	}
	else
	{
		return pName->nameCode;
	}
}

const char* sipHdr_getFullName(char cName)
{
	switch(cName)
	{
		case 'a':
			return "Accept-Contact";
			break;
		case 'b':
			return "Referred-By";
			break;
		case 'c':
			return "Content-Type";
			break;
		case 'e':
			return "Content-Encoding";
			break;
		case 'f':
			return "From";
			break;
		case 'i':
			return "Call-ID";
			break;
		case 'k':
			return "Supportd";
			break;
		case 'l':
			return "Content-Length";
			break;
		case 'm':
			return "Contact";
			break;
		case 'o':
			return "Event";
			break;
		case 'r':
			return "Refer-To";
			break;
		case 's':
			return "Subject";
			break;
		case 't':
			return "To";
			break;
		case 'u':
			return "Allow-Events";
			break;
		case 'v':
			return "Via";
			break;
		default:
			return NULL;
	}
}

const char* sipHdr_getNameByCode(sipHdrName_e nameCode)
{
	switch(nameCode)
	{
		case SIP_HDR_NONE:
			return NULL;
			break;
    	case SIP_HDR_ACCEPT:
			return "Accept";
			break;
    	case SIP_HDR_ACCEPT_CONTACT:
			return "Accept-Contact";
			break;
    	case SIP_HDR_ACCEPT_ENCODING:
			return "Accept-Encoding";
			break;
    	case SIP_HDR_ACCEPT_LANGUAGE:
			return "Accept-Language";
			break;
    	case SIP_HDR_ACCEPT_RESOURCE_PRIORITY:
			return "Accept-Resource-Priority";
			break;
    	case SIP_HDR_ALERT_INFO:
			return "Alert-Info";
			break;
    	case SIP_HDR_ALLOW:
			return "Allow";
			break;
    	case SIP_HDR_ALLOW_EVENTS:
			return "Allow-Events";
			break;
    	case SIP_HDR_ANSWER_MODE:
			return "Answer-Mode";
			break;
    	case SIP_HDR_AUTHENTICATION_INFO:
			return "Authentication-Info";
			break;
    	case SIP_HDR_AUTHORIZATION:
			return "Authentication";
			break;
    	case SIP_HDR_CALL_ID:
			return "Call-Id";
			break;
    	case SIP_HDR_CALL_INFO:
			return "Call-Info";
			break;
    	case SIP_HDR_CONTACT:
			return "Contact";
			break;
    	case SIP_HDR_CONTENT_DISPOSITION:
			return "Content-Disposition";
			break;
    	case SIP_HDR_CONTENT_ENCODING:
			return "Content-Encoding";
			break;
    	case SIP_HDR_CONTENT_LANGUAGE:
			return "Content-Language";
			break;
    	case SIP_HDR_CONTENT_LENGTH:
			return "Content-Length";
			break;
    	case SIP_HDR_CONTENT_TYPE:
			return "Cotent-Type";
			break;
    	case SIP_HDR_CSEQ:
			return "CSeq";
			break;
    	case SIP_HDR_DATE:
			return "Date";
			break;
    	case SIP_HDR_ENCRYPTION:
			return "Encryption";
			break;
    	case SIP_HDR_ERROR_INFO:
			return "Error-Info";
			break;
    	case SIP_HDR_EVENT:
			return "Event";
			break;
    	case SIP_HDR_EXPIRES:
			return "Expires";
			break;
    	case SIP_HDR_FLOW_TIMER:
			return "Flow-Timer";
			break;
    	case SIP_HDR_FROM:
			return "From";
			break;
    	case SIP_HDR_HIDE:
			return "Hide";
			break;
    	case SIP_HDR_HISTORY_INFO:
			return "History-Info";
			break;
    	case SIP_HDR_IDENTITY:
			return "Identity";
			break;
    	case SIP_HDR_IDENTITY_INFO:
			return "Identity-Info";
			break;
    	case SIP_HDR_IN_REPLY_TO:
			return "In-Reply-To";
			break;
    	case SIP_HDR_JOIN:
			return "Join";
			break;
    	case SIP_HDR_MAX_BREADTH:
			return "Max-Breadth";
			break;
    	case SIP_HDR_MAX_FORWARDS:
			return "Max-Forwards";
			break;
    	case SIP_HDR_MIME_VERSION:
			return "MIME-Version";
			break;
    	case SIP_HDR_MIN_EXPIRES:
			return "Min-Expires";
			break;
    	case SIP_HDR_MIN_SE:
			return "Min-SE";
			break;
    	case SIP_HDR_ORGANIZATION:
			return "Organization";
			break;
    	case SIP_HDR_P_ACCESS_NETWORK_INFO:
			return "P-Access-Network-Info";
			break;
    	case SIP_HDR_P_ANSWER_STATE:
			return "P-Answer-State";
			break;
    	case SIP_HDR_P_ASSERTED_IDENTITY:
			return "P-Asserted-Identity";
			break;
    	case SIP_HDR_P_ASSOCIATED_URI:
			return "P-Associated-URI";
			break;
    	case SIP_HDR_P_CALLED_PARTY_ID:
			return "P-Called-Party-ID";
			break;
    	case SIP_HDR_P_CHARGING_FUNCTION_ADDRESSES:
			return "P-Charging-Function-Addresses";
			break;
    	case SIP_HDR_P_CHARGING_VECTOR:
			return "P-Charging-Vector";
			break;
    	case SIP_HDR_P_DCS_TRACE_PARTY_ID:
			return "P-DCS-Trace-Party-ID";
			break;
    	case SIP_HDR_P_DCS_OSPS:
			return "P-DCS-OSPS";
			break;
    	case SIP_HDR_P_DCS_BILLING_INFO:
			return "P-DCS-Billing-Info";
			break;
    	case SIP_HDR_P_DCS_LAES:
			return "P-DCS-LAES";
			break;
    	case SIP_HDR_P_DCS_REDIRECT:
			return "P-DCS-Redirect";
			break;
    	case SIP_HDR_P_EARLY_MEDIA:
			return "P-Early-Media";
			break;
    	case SIP_HDR_P_MEDIA_AUTHORIZATION:
			return "P-Media-Authentication";
			break;
    	case SIP_HDR_P_PREFERRED_IDENTITY:
			return "P-Preferred-Identity";
			break;
    	case SIP_HDR_P_PROFILE_KEY:
			return "P-Profile-Key";
			break;
    	case SIP_HDR_P_REFUSED_URI_LIST:
			return "P-Refused-URI-List";
			break;
    	case SIP_HDR_P_SERVED_USER:
			return "P-Served-User";
			break;
    	case SIP_HDR_P_USER_DATABASE:
			return "P-User-Database";
			break;
    	case SIP_HDR_P_VISITED_NETWORK_ID:
			return "P-Visited-Network-ID";
			break;
    	case SIP_HDR_PATH:
			return "Path";
			break;
    	case SIP_HDR_PERMISSION_MISSING:
			return "Permission-Missing";
			break;
    	case SIP_HDR_PRIORITY:
			return "Priority";
			break;
    	case SIP_HDR_PRIV_ANSWER_MODE:
			return "Priv-Answer-Mode";
			break;
    	case SIP_HDR_PRIVACY:
			return "Privacy";
			break;
    	case SIP_HDR_PROXY_AUTHENTICATE:
			return "Proxy-Authenticate";
			break;
    	case SIP_HDR_PROXY_AUTHORIZATION:
			return "proxy-Authentication";
			break;
    	case SIP_HDR_PROXY_REQUIRE:
			return "Proxy-Require";
			break;
    	case SIP_HDR_RACK:
			return "Rack";
			break;
    	case SIP_HDR_REASON:
			return "Reason";
			break;
    	case SIP_HDR_RECORD_ROUTE:
			return "Record-Route";
			break;
    	case SIP_HDR_REFER_SUB:
			return "Refer-sub";
			break;
    	case SIP_HDR_REFER_TO:
			return "Refer-to";
			break;
    	case SIP_HDR_REFERRED_BY:
			return "Referred-By";
			break;
    	case SIP_HDR_REJECT_CONTACT:
			return "Reject-Contact";
			break;
    	case SIP_HDR_REPLACES:
			return "Replaces";
			break;
    	case SIP_HDR_REPLY_TO:
			return "Reply-To";
			break;
    	case SIP_HDR_REQUEST_DISPOSITION:
			return "Request-Disposition";
			break;
    	case SIP_HDR_REQUIRE:
			return "Require";
			break;
    	case SIP_HDR_RESOURCE_PRIORITY:
			return "Resource-Priority";
			break;
    	case SIP_HDR_RESPONSE_KEY:
			return "Response-Key";
			break;
    	case SIP_HDR_RETRY_AFTER:
			return "Retry-After";
			break;
    	case SIP_HDR_ROUTE:
			return "Route";
			break;
    	case SIP_HDR_RSEQ:
			return "RSeq";
			break;
    	case SIP_HDR_SECURITY_CLIENT:
			return "Security-Client";
			break;
    	case SIP_HDR_SECURITY_SERVER:
			return "Security-Server";
			break;
    	case SIP_HDR_SECURITY_VERIFY:
			return "Security-Verify";
			break;
    	case SIP_HDR_SERVER:
			return "Server";
			break;
    	case SIP_HDR_SERVICE_ROUTE:
			return "Service-Route";
			break;
    	case SIP_HDR_SESSION_EXPIRES:
			return "Session-Expires";
			break;
    	case SIP_HDR_SIP_ETAG:
			return "SIP-ETag";
			break;
    	case SIP_HDR_SIP_IF_MATCH:
			return "SIP-If-Match";
			break;
    	case SIP_HDR_SUBJECT:
			return "Subject";
			break;
    	case SIP_HDR_SUBSCRIPTION_STATE:
			return "Subscription-State";
			break;
    	case SIP_HDR_SUPPORTED:
			return "Supported";
			break;
    	case SIP_HDR_TARGET_DIALOG:
			return "Target-Dialog";
			break;
    	case SIP_HDR_TIMESTAMP:
			return "Timestamp";
			break;
    	case SIP_HDR_TO:
			return "To";
			break;
    	case SIP_HDR_TRIGGER_CONSENT:
			return "Trigger-Consent";
			break;
    	case SIP_HDR_UNSUPPORTED:
			return "Unsupported";
			break;
    	case SIP_HDR_USER_AGENT:
			return "User-Agent";
			break;
    	case SIP_HDR_VIA:
			return "Via";
			break;
    	case SIP_HDR_WARNING:
			return "Warning";
			break;
    	case SIP_HDR_WWW_AUTHENTICATE:
			return "WWW-Authenticate";
			break;
		default:
			break;
	}

	return NULL;
}


/* decode a raw hdr (one raw value corresponding to one hdr name, not to decode all raw hdr values under a hdr name).
 * if isDupRawHdr = true, the raw hdr contents are duplicated
 */
osStatus_e sipDecodeHdr(sipRawHdr_t* sipRawHdr, sipHdrDecoded_t* sipHdrDecoded, bool isDupRawHdr)
{
	mDEBUG_BEGIN(LM_SIPP)

	osStatus_e status = OS_STATUS_OK;

	if(!sipRawHdr || !sipHdrDecoded)
	{
		logError("null pointer, sipRawHdr=%p, sipHdrDecoded=%p.", sipRawHdr, sipHdrDecoded);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	sipHdrDecoded->isRawHdrCopied = isDupRawHdr;
	//create sipHdrDecoded->rawHdr
	sipHdrDecoded->rawHdr.size = sipRawHdr->value.l;
	sipHdrDecoded->rawHdr.pos = 0;
	sipHdrDecoded->rawHdr.end = sipHdrDecoded->rawHdr.size;
	if(isDupRawHdr)
	{
		sipHdrDecoded->rawHdr.buf = osMem_alloc(sipHdrDecoded->rawHdr.size, NULL);
		memcpy(sipHdrDecoded->rawHdr.buf, sipRawHdr->value.p, sipRawHdr->value.l);
	}
	else
	{
		sipHdrDecoded->rawHdr.buf = (uint8_t*) sipRawHdr->value.p;
	} 
	sipHdrDecoded->hdrCode = sipRawHdr->nameCode;

	sipHdrDecoded->decodedHdr = sipHdrParseByName(&sipHdrDecoded->rawHdr, sipRawHdr->nameCode);
    if(sipHdrDecoded->decodedHdr == NULL)
    {
    	logError("decode hdr (%d) error.", sipRawHdr->nameCode);
        status = OS_ERROR_INVALID_VALUE;
		if(isDupRawHdr)
		{
			osMem_deref(sipHdrDecoded->rawHdr.buf);
		}
        goto EXIT;
    }

EXIT:
	mDEBUG_END(LM_SIPP)
	return status;
}



/*
 * requestedHdrNum=-1, decode all hdrs in the SIP message
 */
sipMsgDecoded_t* sipDecodeMsg(osMBuf_t* pSipMsg, sipHdrName_e sipHdrArray[], int requestedHdrNum)
{
	mDEBUG_BEGIN(LM_SIPP)

	osStatus_e status = OS_STATUS_OK;
    sipMsgDecoded_t* pSipMsgDecoded = sipMsg_allocMsgDecoded(pSipMsg);

    if(pSipMsgDecoded == NULL)
    {
        logError("sipMsg_allocMsgDecoded returns NULL.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(!pSipMsg)
	{
		logError("pSipMsg is NULL.", pSipMsg);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(requestedHdrNum != -1  && sipHdrArray == NULL)
	{
		logError("sipHdrArray is NULL while requestedHdrNum =%d.", requestedHdrNum);
		status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	status = sipParser_firstLine(pSipMsg, &pSipMsgDecoded->sipMsgFirstLine, true);
	if(status != OS_STATUS_OK)
	{
		logError("could not parse the first line of sip message properly.");
		goto EXIT;
	}

    size_t origPos = pSipMsg->pos;
	bool isEOH= false;
	while(!isEOH)
	{
		sipRawHdr_t* pSipRawHdr = osMem_alloc(sizeof(sipRawHdr_t), NULL);

		//decode rawHdr for each hdr in the SIP message
		if(sipDecodeOneHdrRaw(pSipMsg, pSipRawHdr, &isEOH) != OS_STATUS_OK)
		{
			logError("sipDecodeOneHdrRaw error.");
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		//check if the decoded raw hdr already in the rawHdrList, if not, add a new element to the msgHdrList
		sipHdrInfo_t* pHdrInfo = pSipMsgDecoded->msgHdrList[pSipRawHdr->nameCode];
		if(pHdrInfo == NULL)
		{
			pHdrInfo = osMem_zalloc(sizeof(sipHdrInfo_t), sipHdrListMemDestroy);
			pHdrInfo->nameCode = pSipRawHdr->nameCode;
//			osList_init(&pHdr->rawHdrList);
//			osList_init(&pHdr->decodedHdrList);
			pSipMsgDecoded->msgHdrList[pSipRawHdr->nameCode] = pHdrInfo;
			pSipMsgDecoded->hdrNum++;
		}

		if(pHdrInfo->rawHdr.pRawHdr == NULL)
		{
			pHdrInfo->rawHdr.pRawHdr == pSipRawHdr;
			pHdrInfo->rawHdr.rawHdrNum = 1;
		}
		else
		{
			osList_append(&pHdrInfo->rawHdr.rawHdrList, pSipRawHdr);
			++pHdrInfo->rawHdr.rawHdrNum;
		}

		bool isDecode = false;
		if(requestedHdrNum == -1)
		{
			isDecode = true;
		}
		else
		{
        	for(int i=0; i<requestedHdrNum; i++)
        	{
            	if( pSipRawHdr->nameCode == sipHdrArray[i])
            	{
					isDecode = true;
					break;
				}
			}
		}

		if(isDecode)
		{
        	mdebug(LM_SIPP, "nameCode=%d, isEOH=%d", pSipRawHdr->nameCode, isEOH);
			void* pDecodedHdr = sipHdrParse(pSipMsg, pSipRawHdr->nameCode, pSipRawHdr->valuePos, pSipRawHdr->value.l);
#if 0
			osMBuf_t pSipMsgHdr;
            osMBuf_allocRef1(&pSipMsgHdr, pSipMsg, pSipRawHdr->valuePos, pSipRawHdr->value.l);

	    	void* pDecodedHdr = sipHdrParseByName(&pSipMsgHdr, pSipRawHdr->nameCode);
#endif
			if(pDecodedHdr == NULL)
			{
				logError("decode hdr (%d) error.", pSipRawHdr->nameCode);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}

			switch(pSipRawHdr->nameCode)
			{
                //for TimeLen
                case SIP_HDR_CONTENT_LENGTH:
                case SIP_HDR_EXPIRES:
                case SIP_HDR_MAX_FORWARDS:
                case SIP_HDR_MIN_EXPIRES:
                case SIP_HDR_SESSION_EXPIRES:
					pHdrInfo->decodedHdr.pDecodedHdr = pDecodedHdr;
					break;
                //for other unique headers
                case SIP_HDR_CALL_ID:
					pHdrInfo->decodedHdr.pDecodedHdr = pDecodedHdr;
					break;
				default:
        			if(pHdrInfo->decodedHdr.pDecodedHdr == NULL)
        			{
            			pHdrInfo->decodedHdr.pDecodedHdr == pDecodedHdr;
            			pHdrInfo->decodedHdr.decodedHdrNum = 1;
        			}
        			else
        			{
            			osList_append(&pHdrInfo->decodedHdr.decodedHdrList, pDecodedHdr);
            			++pHdrInfo->decodedHdr.decodedHdrNum;
        			}
					break;
			}
		}
    }

    osMBuf_allocRef2(&pSipMsgDecoded->msgContent, pSipMsg, pSipMsg->pos, pSipMsg->end - pSipMsg->pos);

EXIT:
	if(status != OS_STATUS_OK)
	{
		sipMsg_deallocMsgDecoded(pSipMsgDecoded);
		pSipMsgDecoded = NULL;
	}

    pSipMsg->pos = origPos;

	mDEBUG_END(LM_SIPP)
    return pSipMsgDecoded;
}


#if 0
static sipHdrInfo_t* sipMsg_getHdrInfo(osList_t* pMsgHdrList, sipHdrName_e hdrNameCode)
{
	if(!pMsgHdrList)
	{
		logError("pMsgHdrList is NULL.");
		return NULL;
	}

	osListElement_t* pLE = pMsgHdrList->head;
	while(pLE)
	{
		sipHdrInfo_t* pInfo = pLE->data;
		if(!pInfo)
		{
			logError("a pMsgHdrList element has a NULL data.");
			return NULL;
		}

		if(hdrNameCode == pInfo->nameCode)
		{
			return pInfo;
		}

		pLE = pLE->next;
	}

	return NULL;
}
#endif

#if 0
static void sipMsgDecodedRawHdr_delete(void* data)
{
	if(!data)
    {
        return;
    }

	sipMsgDecodedRawHdr_t* pMsgDecoded = data;

	for (int i=0; i<SIP_HDR_COUNT; i++)
	{
		if(pMsgDecoded->msgHdrList[i] == NULL)
		{
			continue;
		}

		osMem_deref(pMsgDecoded->msgHdrList[i]->pRawHdr);
		osList_delete(&pMsgDecoded->msgHdrList[i]->rawHdrList);
	}

	osMem_deref(pMsgDecoded);
}
#endif


//clean up for data structure sipHdrDecoded_t
void sipHdrDecoded_cleanup(void* pData)
{
	if(!pData)
	{
		return;
	}

	sipHdrDecoded_t* pHdrDecoded = pData;

	osMem_deref(pHdrDecoded->decodedHdr);
	if(pHdrDecoded->isRawHdrCopied)
	{
		osMem_deref(pHdrDecoded->rawHdr.buf);
	}
}


static void sipMsgDecoded_delete(void *data)
{
	if(!data)
	{
		return;
	}

	sipMsgDecoded_t* pMsgDecoded = data;
	sipFirstLine_cleanup(&pMsgDecoded->sipMsgFirstLine);

	for(int i=0; i<SIP_HDR_COUNT; i++)
	{
		if(pMsgDecoded->msgHdrList[i]->decodedHdr.decodedHdrNum == 0)
		{
			continue;
		}

		osMem_deref(pMsgDecoded->msgHdrList[i]->decodedHdr.pDecodedHdr);
        osMem_deref(pMsgDecoded->msgHdrList[i]->rawHdr.pRawHdr);

		if(pMsgDecoded->msgHdrList[i]->decodedHdr.decodedHdrNum > 1)
		{
			osList_delete(&pMsgDecoded->msgHdrList[i]->decodedHdr.decodedHdrList);
		}

		if(pMsgDecoded->msgHdrList[i]->rawHdr.rawHdrNum > 1)
		{
    		osList_delete(&pMsgDecoded->msgHdrList[i]->rawHdr.rawHdrList);
		}
    //to-do, double check if this line shall be called. decodeValue may refer field in sipBuf    osMem_deref(pHdrInfo->decodedValue);
    }

    osMBuf_dealloc(pMsgDecoded->sipMsg);
}

static void sipHdrListMemDestroy(void* data)
{
    if(!data)
    {
        return;
    }

    sipHdrInfo_t* pSipHdrInfo = data;
	osMem_deref(pSipHdrInfo->rawHdr.pRawHdr);
	osList_delete(&pSipHdrInfo->rawHdr.rawHdrList);
	osMem_deref(pSipHdrInfo->decodedHdr.pDecodedHdr);
	osList_delete(&pSipHdrInfo->decodedHdr.decodedHdrList);
    //to-do, double check if this line shall be called. decodeValue may refer field in sipBuf    osMem_deref(pHdrInfo->decodedValue);
} 	
