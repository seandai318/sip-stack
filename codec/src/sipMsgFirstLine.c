#include <string.h>

#include "osTypes.h"
#include "osMisc.h"
#include "osDebug.h"
#include "osMemory.h"

#include "sipParsing.h"
#include "sipMsgFirstLine.h"



osStatus_e sipParser_firstLine(osMBuf_t* pSipMsg, sipFirstline_t* pFL)
{ 
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pFL)
    {
        logError("NULL pointer, pSipMsg=%p, pFL=%p.", pSipMsg, pFL);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(strncmp((char*) &pSipMsg->buf[pSipMsg->pos], "SIP", 3) == 0)
	{
		//this is a SIP status line
		pFL->isReqLine = false;
	}
	else
	{
		//this is a SIP Request line
		pFL->isReqLine = true;
	}

	if(pFL->isReqLine)
	{
		//find the Method
		osPointerLen_t sipMethod;
		sipMethod.p = &pSipMsg->buf[pSipMsg->pos];
		size_t origPos = pSipMsg->pos;
		while(pSipMsg->buf[pSipMsg->pos] != ' ' && pSipMsg->pos < pSipMsg->end)
		{
			if(pSipMsg->buf[pSipMsg->pos] == 0xd)
			{
				logError("SIP request line does not have method.")
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}

			pSipMsg->pos++;
		}
		
		sipMethod.l =  pSipMsg->pos - origPos;
		pFL->u.sipReqLine.sipReqCode = sipMsg_method2Code(&sipMethod);
		if(pFL->u.sipReqLine.sipReqCode == SIP_METHOD_INVALID)
		{
			logError("SIP message has a invalid method (%r).", &sipMethod);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		} 
		pSipMsg->pos++;

		//now parse sip uri	
        sipParsingStatus_t parsingStatus;
        sipParsingInfo_t parentParsingInfo;

        parentParsingInfo.tokenNum = 1;
		parentParsingInfo.extTokenNum = 1;
        parentParsingInfo.token[0] = ' ';
        parentParsingInfo.arg = &pFL->u.sipReqLine.sipUri;
		osList_init(&pFL->u.sipReqLine.sipUri.headers);

		status = sipParamUri_parse(pSipMsg, pSipMsg->end, &parentParsingInfo, &parsingStatus);
		if(status != OS_STATUS_OK)
		{
			logError("sip request URI parsing error.")
			goto EXIT;
		}

		//now parsing sip version
		if(strncmp(&pSipMsg->buf[pSipMsg->pos], "SIP/2.0\r\n", 9) != 0)
        {
            logError("The SIP request does not have correct SIP version or ending.");
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }

		//pos shift 8 instead of 9, the EXIT place will add extra 1
        pSipMsg->pos +=8;
		goto EXIT;
	}
	else
	{
		if(strncmp(&pSipMsg->buf[pSipMsg->pos], "SIP/2.0 ", 8) != 0)
		{
			logError("The SIP status line does not have correct SIP version.");
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		pSipMsg->pos +=8;
		
		if(pSipMsg->buf[pSipMsg->pos+3] != ' ')
		{
			logError("Expect a SP after the response code, but it is not (%c).", pSipMsg->buf[pSipMsg->pos+3]);
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
		}

		status = osStr2Int(&pSipMsg->buf[pSipMsg->pos], 3, (int*) &pFL->u.sipStatusLine.sipStatusCode);
		if(status != OS_STATUS_OK)
		{
			logError("sip status line does not contain a valid status code.");
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }

		if(!sipIsStatusCodeValid(pFL->u.sipStatusLine.sipStatusCode))
		{
            logError("sip status line has a invalid status code (%d).", pFL->u.sipStatusLine.sipStatusCode);
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }

		pSipMsg->pos += 4;

		pFL->u.sipStatusLine.reason.p = &pSipMsg->buf[pSipMsg->pos];
		pFL->u.sipStatusLine.reason.l = 0;

		while(pSipMsg->pos < pSipMsg->end)
		{
			if(pSipMsg->buf[pSipMsg->pos] == 0xd)
			{
				if(pSipMsg->buf[++pSipMsg->pos] != 0xa)
				{
					logError("sip status line does not end with CRLF (0x%x).", pSipMsg->buf[pSipMsg->pos]);
		            status = OS_ERROR_INVALID_VALUE;
        		    goto EXIT;
        		}
				else
				{
					goto EXIT;
				}
			}
			else
			{
				pSipMsg->pos++;
				pFL->u.sipStatusLine.reason.l++;
			}
		}
	}

EXIT:
	if(status == OS_STATUS_OK)
	{
		pSipMsg->pos++;
	}

	return status;
}


osStatus_e sipHdrFirstline_encode(osMBuf_t* pSipBuf, void* pReqLineDT, void* other)
{
	osStatus_e status = OS_STATUS_OK;	
	sipReqLinePT_t* pReqLine = pReqLineDT;

	if(!pSipBuf || !pReqLine)
	{
		logError("null pointer, pSipBuf=%p, pReqLine=%p.", pSipBuf, pReqLine);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	osPointerLen_t method;
	status = sipMsg_code2Method(pReqLine->sipReqCode, &method);
	if(status != OS_STATUS_OK)
	{
		logError("incorrect request code (%d).", pReqLine->sipReqCode);
		goto EXIT;
	}

	osMBuf_writePL(pSipBuf, &method, true);
	osMBuf_writeU8(pSipBuf, ' ', true);
	status = sipParamUri_encode(pSipBuf, pReqLine->pSipUri);
    if(status != OS_STATUS_OK)
    {
        logError("encode sip URI fails.");
        goto EXIT;
    }
	osMBuf_writeStr(pSipBuf, " SIP/2.0\r\n", true);

EXIT:
	return status;
}


osStatus_e sipHdrFirstline_encode1(osMBuf_t* pSipBuf, void* pReqLineDT, void* other, sipFirstline_t* pFirstLine)
{
    osStatus_e status = OS_STATUS_OK;
    sipReqLinePT_t* pReqLine = pReqLineDT;

    if(!pSipBuf || !pReqLine || !pFirstLine)
    {
        logError("null pointer, pSipBuf=%p, pReqLine=%p, pFirstLine=%p.", pSipBuf, pReqLine, pFirstLine);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    osPointerLen_t method;
    status = sipMsg_code2Method(pReqLine->sipReqCode, &method);
    if(status != OS_STATUS_OK)
    {
        logError("incorrect request code (%d).", pReqLine->sipReqCode);
        goto EXIT;
    }

    osMBuf_writePL(pSipBuf, &method, true);
    osMBuf_writeU8(pSipBuf, ' ', true);
    status = sipParamUri_encode(pSipBuf, pReqLine->pSipUri);
    if(status != OS_STATUS_OK)
    {
        logError("encode sip URI fails.");
        goto EXIT;
    }
    osMBuf_writeStr(pSipBuf, " SIP/2.0\r\n", true);

	//fill pFirstLine.  the sipUri is not filled, as it is not to be used by other modules
	pFirstLine->isReqLine = true;
	pFirstLine->u.sipReqLine.sipReqCode = pReqLine->sipReqCode;

EXIT:
    return status;
}


osStatus_e sipHdrFirstline_respEncode(osMBuf_t* pSipBuf, void* pRespCode, void* other)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipBuf || !pRespCode)
    {
        logError("null pointer, pSipBuf=%p, pRespCode=%p.", pSipBuf, pRespCode);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	osMBuf_writeStr(pSipBuf, "SIP/2.0 ", true);
	const char* respStatus = sipHdrFirstline_respCode2status(*(sipResponse_e*)pRespCode);
	if(!respStatus)
	{
		logError("fails to get the response status for response code (%d).", *(sipResponse_e*)pRespCode);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}
	osMBuf_writeStr(pSipBuf, respStatus, true);
	osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
    return status;
}


osStatus_e sipHdrFirstline_respEncode1(osMBuf_t* pSipBuf, void* pRespCode, void* other, sipFirstline_t* pFirstLine)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipBuf || !pRespCode)
    {
        logError("null pointer, pSipBuf=%p, pRespCode=%p.", pSipBuf, pRespCode);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    osMBuf_writeStr(pSipBuf, "SIP/2.0 ", true);
    const char* respStatus = sipHdrFirstline_respCode2status(*(sipResponse_e*)pRespCode);
    if(!respStatus)
    {
        logError("fails to get the response status for response code (%d).", *(sipResponse_e*)pRespCode);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
    osMBuf_writeStr(pSipBuf, respStatus, true);
    osMBuf_writeStr(pSipBuf, "\r\n", true);

    pFirstLine->isReqLine = false;
    pFirstLine->u.sipStatusLine.sipStatusCode = *(sipResponse_e*)pRespCode;
    pFirstLine->u.sipStatusLine.reason.p = &respStatus[4];	//bypass the respCode part, start from the reason phrase
	pFirstLine->u.sipStatusLine.reason.l = strlen(respStatus) - 4;

EXIT:
    return status;
}


osStatus_e sipHdrFirstline_create(void* pReqLineDT, void* pUriDT, void* pReqTypeDT)
{
	osStatus_e status = OS_STATUS_OK;
	sipReqLinePT_t* pReqLine = pReqLineDT;
	sipUri_t* pUri = pUriDT;
	sipRequest_e* pReqType = pReqTypeDT;

	if(!pReqLine || !pUri || !pReqType)
	{
		logError("null pointer, pReqLine=%p, pUri=%p, pReqType=%p.", pReqLine, pUri, pReqType);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	pReqLine->sipReqCode = *pReqType;
	pReqLine->pSipUri = pUri;
	
EXIT:
	return status;
}


typedef osStatus_e (*sipHdrEncode_h) (osMBuf_t* pSipBuf, void* hdrData, void* other);


bool sipIsStatusCodeValid(int statusCode)
{
	if(statusCode < 100 || statusCode >=700)
	{
		return false;
	}

	switch (statusCode)
	{
		case SIP_RESPONSE_100:   //Trying
            return true;
            break;
    	case SIP_RESPONSE_180:   //Ringing
            return true;
            break;
    	case SIP_RESPONSE_181:   //Call Is Being Forwarded
            return true;
            break;
    	case SIP_RESPONSE_182:   //Queued
            return true;
            break;
    	case SIP_RESPONSE_183:   //Session Progress
            return true;
            break;
  		case SIP_RESPONSE_200:
            return true;
            break;
    	case SIP_RESPONSE_202:
            return true;
            break;
    	case SIP_RESPONSE_300:
            return true;
            break;
    	case SIP_RESPONSE_301:
            return true;
            break;
    	case SIP_RESPONSE_302:
            return true;
            break;
    	case SIP_RESPONSE_305:
            return true;
            break;
    	case SIP_RESPONSE_380:
            return true;
            break;
    	case SIP_RESPONSE_400:   //Bad Request
            return true;
            break;
    	case SIP_RESPONSE_401:   //Unauthorized
            return true;
            break;
    	case SIP_RESPONSE_402:   //Payment Required
            return true;
            break;
    	case SIP_RESPONSE_403:   //Forbidden
            return true;
            break;
    	case SIP_RESPONSE_404:   //Not Found
            return true;
            break;
    	case SIP_RESPONSE_405:   //Method Not Allowed
            return true;
            break;
    	case SIP_RESPONSE_406:   //Not Acceptable
            return true;
            break;
    	case SIP_RESPONSE_407:   //Proxy Authentication Required
            return true;
            break;
    	case SIP_RESPONSE_408:   //Request Timeout
            return true;
            break;
    	case SIP_RESPONSE_410:   //Gone
            return true;
            break;
    	case SIP_RESPONSE_413:   //Request Entity Too Large
            return true;
            break;
    	case SIP_RESPONSE_414:   //Request-URI Too Large
            return true;
            break;
    	case SIP_RESPONSE_415:   //Unsupported Media Type
            return true;
            break;
    	case SIP_RESPONSE_416:   //Unsupported URI Scheme
            return true;
            break;
    	case SIP_RESPONSE_420:   //Bad Extension
            return true;
            break;
    	case SIP_RESPONSE_421:   //Extension Required
            return true;
            break;
    	case SIP_RESPONSE_423:   //Interval Too Brief
            return true;
            break;
    	case SIP_RESPONSE_480:   //Temporarily not available
            return true;
            break;
    	case SIP_RESPONSE_481:   //Call Leg/Transaction Does Not Exist
            return true;
            break;
    	case SIP_RESPONSE_482:   //Loop Detected
            return true;
            break;
    	case SIP_RESPONSE_483:   //Too Many Hops
            return true;
            break;
    	case SIP_RESPONSE_484:   //Address Incomplete
            return true;
            break;
    	case SIP_RESPONSE_485:   //Ambiguous
            return true;
            break;
    	case SIP_RESPONSE_486:   //Busy Here
            return true;
            break;
    	case SIP_RESPONSE_487:   //Request Terminated
            return true;
            break;
    	case SIP_RESPONSE_488:   //Not Acceptable Here
            return true;
            break;
    	case SIP_RESPONSE_491:   //Request Pending
            return true;
            break;
    	case SIP_RESPONSE_493:   //Undecipherable
            return true;
            break;
    	case SIP_RESPONSE_500:   //Internal Server Error
            return true;
            break;
    	case SIP_RESPONSE_501:   //Not Implemented
            return true;
            break;
    	case SIP_RESPONSE_502:   //Bad Gateway
            return true;
            break;
    	case SIP_RESPONSE_503:   //Service Unavailable
            return true;
            break;
    	case SIP_RESPONSE_504:   //Server Time-out
            return true;
            break;
    	case SIP_RESPONSE_505:   //SIP Version not supported
            return true;
            break;
    	case SIP_RESPONSE_513:   //Message Too Large
            return true;
            break;
    	case SIP_RESPONSE_600:   //Busy Everywhere
            return true;
            break;
    	case SIP_RESPONSE_603:   //Decline
            return true;
            break;
    	case SIP_RESPONSE_604:   //Does not exist anywhere
            return true;
            break;
    	case SIP_RESPONSE_606:   //Not Acceptable
			return true;
			break;
		default:
			logWarning("the SIP response status code is not defined in RFC3261.");
			return true;
			break;
	}

	return false;
}


osStatus_e sipMsg_code2Method(sipRequest_e code, osPointerLen_t* pMethod)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pMethod)
    {
        status = OS_ERROR_INVALID_VALUE;
    }

	switch(code)
	{
		case SIP_METHOD_ACK:
			pMethod->p = "ACK";
			pMethod->l = 3;
			break;
		case SIP_METHOD_BYE:
            pMethod->p = "BYE";
            pMethod->l = 3;
            break;
        case SIP_METHOD_CANCEL:
            pMethod->p = "CANCEL";
            pMethod->l = 6;
            break;
        case SIP_METHOD_INVITE:
            pMethod->p = "INVITE";
            pMethod->l = 6;
            break;
        case SIP_METHOD_MESSAGE:
            pMethod->p = "MESSAGE";
            pMethod->l = 7;
            break;
        case SIP_METHOD_NOTIFY:
            pMethod->p = "NOTIFY";
            pMethod->l = 6;
            break;
        case SIP_METHOD_OPTION:
            pMethod->p = "OPTION";
            pMethod->l = 6;
            break;
        case SIP_METHOD_REGISTER:
            pMethod->p = "REGISTER";
            pMethod->l = 8;
            break;
        case SIP_METHOD_SUBSCRIBE:
            pMethod->p = "SUBSCRIBE";
            pMethod->l = 9;
            break;
		default:
			pMethod->l = 0;
			status = OS_ERROR_INVALID_VALUE;
			break;
	}

	return status;
}



sipRequest_e sipMsg_method2Code(osPointerLen_t* pMethod)
{
	if(!pMethod)
	{
		return SIP_METHOD_INVALID;
	}

	switch (pMethod->l)
	{
		case 3:
			if(strncmp((char*) pMethod->p, "ACK", 3) == 0)
			{
				return SIP_METHOD_ACK;
			}
			else if(strncmp((char*) pMethod->p, "BYE", 3) == 0)
			{
				return SIP_METHOD_BYE;
			}
			break;
		case 6:
			switch (pMethod->p[0])
			{
				case 'C':
					if(strncmp((char*) pMethod->p, "CANCEL", 6)==0)
					{
						return SIP_METHOD_CANCEL;
					}
					break;
				case 'I':
					if(strncmp((char*) pMethod->p, "INVITE", 6)==0)
					{
						return SIP_METHOD_INVITE;
					}
					break;
				case 'N':
					if(strncmp((char*) pMethod->p, "NOTIFY", 6)==0)
					{
						return SIP_METHOD_NOTIFY;
					}
					break;
				case 'O':
					if(strncmp((char*) pMethod->p, "OPTION", 6)==0)
					{
						return SIP_METHOD_OPTION;
					}
					break;
				default:
					break;
			}
			break;
		case 7:
			if(strncmp((char*) pMethod->p, "MESSAGE", 7)==0)
			{
				return SIP_METHOD_MESSAGE;
			}
			break;
		case 8:
            if(strncmp((char*) pMethod->p, "REGISTER", 8)==0)
            {
                return SIP_METHOD_REGISTER;
            }
			break;
		case 9:
            if(strncmp((char*) pMethod->p, "SUBSCRIBE", 9)==0)
            {
                return SIP_METHOD_SUBSCRIBE;
            }
            break;
		default:
			break;
	}

	return SIP_METHOD_INVALID;
}


const char* sipHdrFirstline_respCode2status(sipResponse_e respCode)
{
	switch(respCode)
	{
		case SIP_RESPONSE_100:
			return "100 Trying";
			break;
    	case SIP_RESPONSE_180:
			return "180 Ringing";
			break;
    	case SIP_RESPONSE_181:
			return "181 Is Being Forwarded";
			break;
    	case SIP_RESPONSE_182:
			return "182 Queued";
			break;
    	case SIP_RESPONSE_183:
			return "183 Session Progress";
			break;
    	case SIP_RESPONSE_200:
			return "200 OK";
			break; 
    	case SIP_RESPONSE_202:
			return "202 Accepted";
			break;
    	case SIP_RESPONSE_300:
			return "300 Multiple Choices";
			break;
    	case SIP_RESPONSE_301:
			return "301 Moved Permanently";
			break;
    	case SIP_RESPONSE_302:
			return "302 Moved Temporarily";
			break;
    	case SIP_RESPONSE_305:
			return "305 Use Proxy";
			break;
    	case SIP_RESPONSE_380:
			return "380 Alternative Service";
			break;
    	case SIP_RESPONSE_400:
			return "400 Bad Request";
			break;
    	case SIP_RESPONSE_401:
			return "401 Unauthorized";
			break;
    	case SIP_RESPONSE_402:
			return "402 Payment Required";
			break;
    	case SIP_RESPONSE_403:
			return "403 Forbidden";
			break;
   	 	case SIP_RESPONSE_404:
			return "404 Not Found";
			break;
    	case SIP_RESPONSE_405:
			return "405 Method Not Allowed";
			break;
    	case SIP_RESPONSE_406:
			return "406 Not Acceptable";
			break;
    	case SIP_RESPONSE_407:
			return "407 Proxy Authentication Required";
			break;
    	case SIP_RESPONSE_408:
			return "408 Request Timeout";
			break;
    	case SIP_RESPONSE_410:
			return "410 Gone";
			break;
    	case SIP_RESPONSE_413:
			return "413 Request Entity Too Large";
			break;
    	case SIP_RESPONSE_414:
			return "414 Request-URI Too Large";
			break;
    	case SIP_RESPONSE_415:
			return "415 Unsupported Media Type";
			break;
    	case SIP_RESPONSE_416:
			return "416 Unsupported URI Scheme";
			break;
    	case SIP_RESPONSE_420:
			return "420 Bad Extension";
			break;
    	case SIP_RESPONSE_421:
			return "421 Extension Required";
			break;
    	case SIP_RESPONSE_423:
			return "423 Interval Too Brief";
			break;
    	case SIP_RESPONSE_480:
			return "480 Temporarily not available";
			break;
    	case SIP_RESPONSE_481:
			return "481 Call Leg/Transaction Does Not Exist";
			break;
    	case SIP_RESPONSE_482:
			return "482 Loop Detected";
			break;
    	case SIP_RESPONSE_483:
			return "483 Too Many Hops";
			break;
    	case SIP_RESPONSE_484:
			return "484 Address Incomplete";
			break;
    	case SIP_RESPONSE_485:
			return "485 Ambiguous";
			break;
    	case SIP_RESPONSE_486:
			return "486 Busy Here";
			break;
    	case SIP_RESPONSE_487:
			return "487 Request Terminated";
			break;
    	case SIP_RESPONSE_488:
			return "488 Not Acceptable Here";
			break;
    	case SIP_RESPONSE_491:
			return "491 Request Pending";
			break;
    	case SIP_RESPONSE_493:
			return "493 Undecipherable";
			break;
    	case SIP_RESPONSE_500:
			return "500 Internal Server Error";
			break;
    	case SIP_RESPONSE_501:
			return "501 Not Implemented";
			break;
    	case SIP_RESPONSE_502:
			return "502 Bad Gateway";
			break;
    	case SIP_RESPONSE_503:
			return "503 Service Unavailable";
			break;
    	case SIP_RESPONSE_504:
			return "504 Server Time-out";
			break;
    	case SIP_RESPONSE_505:
			return "505 SIP Version not supported";
			break;
    	case SIP_RESPONSE_513:
			return "513 Message Too Large";
			break;
    	case SIP_RESPONSE_600:
			return "600 Busy Everywhere";
			break;
    	case SIP_RESPONSE_603:
			return "603 Decline";
			break;
    	case SIP_RESPONSE_604:
			return "604 Does not exist anywhere";
			break;
    	case SIP_RESPONSE_606:
			return "606 Not Acceptable";
			break;
		default:
			break;
	}

	return NULL;
}


sipRequest_e sipMsg_getReqCode(osPointerLen_t* reqMethod)
{
	sipRequest_e reqCode = SIP_METHOD_INVALID;

	if(!reqMethod)
	{
		goto EXIT;
	}

	switch(reqMethod->l)
	{
		case 3:
			if(reqMethod->p[0]=='A')
			{
				reqCode = osPL_strcmp(reqMethod, "ACK") ? SIP_METHOD_INVALID : SIP_METHOD_ACK;
			}
			else if(reqMethod->p[0]=='B')
			{
				reqCode = osPL_strcmp(reqMethod, "BYE") ? SIP_METHOD_INVALID : SIP_METHOD_BYE;
			}
			break;
		case 4:
			if(reqMethod->p[0]=='I')
			{
				reqCode = osPL_strcmp(reqMethod, "INFO") ? SIP_METHOD_INVALID : SIP_METHOD_INFO;
			}
			break;
		case 6:
			if(reqMethod->p[0]=='C')
			{
				reqCode = osPL_strcmp(reqMethod, "CANCEL") ? SIP_METHOD_INVALID : SIP_METHOD_CANCEL;
			}
			else if(reqMethod->p[0]=='I')
            {
                reqCode = osPL_strcmp(reqMethod, "INVITE") ? SIP_METHOD_INVALID : SIP_METHOD_INVITE;
            }
            else if(reqMethod->p[0]=='N')
            {
                reqCode = osPL_strcmp(reqMethod, "NOTIFY") ? SIP_METHOD_INVALID : SIP_METHOD_NOTIFY;
            }
            else if(reqMethod->p[0]=='O')
            {
                reqCode = osPL_strcmp(reqMethod, "OPTION") ? SIP_METHOD_INVALID : SIP_METHOD_OPTION;
            }
            break;
		case 7:
			if(reqMethod->p[0]=='M')
			{
				reqCode = osPL_strcmp(reqMethod, "MESSAGE") ? SIP_METHOD_INVALID : SIP_METHOD_MESSAGE;
            }
            break;
		case 8:
			if(reqMethod->p[0]=='R')
            {
                reqCode = osPL_strcmp(reqMethod, "REGISTER") ? SIP_METHOD_INVALID : SIP_METHOD_REGISTER;
            }
            break;
        case 9:
            if(reqMethod->p[0]=='S')
            {
                reqCode = osPL_strcmp(reqMethod, "SUBSCRIBE") ? SIP_METHOD_INVALID : SIP_METHOD_SUBSCRIBE;
            }
            break;
		default:
			logError("invalid reqMethod (%r).", reqMethod);
			break;
	}

EXIT:
	return reqCode;
}


void sipFirstLine_cleanup(void* pData)
{
	if(!pData)
	{
		return;
	}

	sipFirstline_t* pFirstLine = (sipFirstline_t*) pData;
	if(pFirstLine->isReqLine)
	{
		sipUri_t* pUri = &pFirstLine->u.sipReqLine.sipUri;
		sipUri_cleanup(pUri);
	}
	else
	{
		//nothing to cleanup for the sipRespLine for the decoded message, since reason directly points to the pSipMsg
	}
}
	

void* sipFirstLine_alloc()
{
	return osMem_alloc(sizeof(sipFirstline_t), sipFirstLine_cleanup);
}
