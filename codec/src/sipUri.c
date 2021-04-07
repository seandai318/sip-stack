/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipUri.c
 ********************************************************/

#include <string.h>
#include "osDebug.h"

#include "sipHeader.h"
#include "sipConfig.h"
#include "sipParsing.h"
#include "sipUri.h"
#include "sipUriparam.h"
#include "sipHostport.h"
#include "sipUserinfo.h"
#include "sipParamHeaders.h"
#include "sipGenericNameParam.h"


static sipParsingABNF_t sipUriABNF[]={	\
	{1, 1, 					SIP_TOKEN_INVALID, 	0, SIPP_PARAM_SIP, 		NULL, 				NULL}, \
	{0, 1, 					SIP_TOKEN_INVALID, 	0, SIPP_PARAM_URIINFO,	sipParser_userinfo, sipUserinfo_cleanup}, \
	{1, 1, 					'@', 				0, SIPP_PARAM_HOSTPORT, sipParser_hostport, NULL}, \
	{0, SIPP_MAX_PARAM_NUM, ';', 				0, SIPP_PARAM_URIPARAM, sipParser_uriparam, NULL}, \
	{0, SIPP_MAX_PARAM_NUM, '?', 				0, SIPP_PARAM_HEADERS,  sipParser_headers, 	sipParser_headersCleanup}};


static osStatus_e sipParsing_setUriParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg);
static int sipP_uriNum = sizeof(sipUriABNF)/sizeof(sipParsingABNF_t);



osStatus_e sipParamUri_parse(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParentParsingInfo, sipParsingStatus_t* pParsingStatus)
{
	osStatus_e status = OS_STATUS_OK;
    sipParsingInfo_t sippParsingInfo[sipP_uriNum-1];
	SIP_INIT_PARSINGINFO(sippParsingInfo, sipP_uriNum-1);

	if(!pSipMsg || !pParsingStatus || !pParentParsingInfo)
	{
		logError("NULL pointer is passed in, pSipMsg=%p, pParsingStatus=%p, pParentParsingInfo=%p.", pSipMsg, pParsingStatus, pParentParsingInfo);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	sipUri_t* pSipUri = (sipUri_t*) pParentParsingInfo->arg;

	//for SIPP_PARAM_SIP, we directly parse
	if(sipUriABNF[0].paramName != SIPP_PARAM_SIP)
	{
		logError("sipParsingABNF error, SIPP_PARAM_SIP is not the first parameter.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}
	
	pSipUri->sipUser.p = &pSipMsg->buf[pSipMsg->pos];
	pSipUri->sipUser.l = 0;
    if(strncasecmp(&pSipMsg->buf[pSipMsg->pos], "sip:", 4) == 0)
    {
        pSipMsg->pos += 4;
        pSipUri->sipUriType = URI_TYPE_SIP;
    }
    else if(strncasecmp(&pSipMsg->buf[pSipMsg->pos], "sips:", 5) == 0)
    {
        pSipMsg->pos += 5;
        pSipUri->sipUriType = URI_TYPE_SIPS;
    }
    else if(strncasecmp(&pSipMsg->buf[pSipMsg->pos], "tel:", 4) == 0)
    {
		pSipMsg->pos += 4;
		pParsingStatus->status = URI_TYPE_TEL;
	}
	else
	{
        pParsingStatus->status = SIPP_STATUS_OTHER_VALUE;
        logError("the uri type is not supported.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	//start to parse the remaining parameters
	sipParsing_setParsingInfo(&sipUriABNF[1], sipP_uriNum-1, pParentParsingInfo, sippParsingInfo, sipParsing_setUriParsingInfo);

    status = sipParsing_getHdrValue(pSipMsg, hdrEndPos, &sipUriABNF[1], sippParsingInfo, sipP_uriNum-1, pParsingStatus);

	pSipUri->sipUser.l = pSipUri->hostport.host.p + pSipUri->hostport.host.l - pSipUri->sipUser.p;

	//convert port to portValue
	if(pSipUri->hostport.port.l == 0)
	{
		pSipUri->hostport.portValue = 0;
	}
	else
	{
		pSipUri->hostport.portValue = osPL_str2u32(&pSipUri->hostport.port);
	}

EXIT:
	return status;
}



bool sipUri_isTelSub(sipUri_t* pUri)
{
    if(!pUri)
    {
        logError("NULL pUri pointer.");
        return false;
    }

    return pUri->userInfo.sipUser.isTelSub;
}


osPointerLen_t* sipUri_getUser(sipUri_t* pUri)
{
	if(!pUri)
	{
		logError("NULL pUri pointer.");
		return NULL;
	}

	return &pUri->userInfo.sipUser.user;
}


osPointerLen_t* sipUri_getPassword(sipUri_t* pUri)
{
    if(!pUri)
    {
        logError("NULL pUri pointer.");
        return NULL;
    }

	return &pUri->userInfo.password;
}


osPointerLen_t* sipUri_getHost(sipUri_t* pUri)
{
    if(!pUri)
    {
        logError("NULL pUri pointer.");
        return NULL;
    }

    return &pUri->hostport.host;
}


osPointerLen_t* sipUri_getPort(sipUri_t* pUri)
{
    if(!pUri)
    {
        logError("NULL pUri pointer.");
        return NULL;
    }

    return &pUri->hostport.port;
}


uint32_t sipUri_getUriparamMask(sipUri_t* pUri)
{
    if(!pUri)
    {
        logError("NULL pUri pointer.");
        return 0;
    }

    return pUri->uriParam.uriParamMask;
}


osPointerLen_t* sipUri_getTransport(sipUri_t* pUri)
{
    if(!pUri)
    {
        logError("NULL pUri pointer.");
        return 0;
    }

 	return &pUri->uriParam.transport;
}


osList_t* sipUri_getOtherparam(sipUri_t* pUri)
{
    if(!pUri)
    {
        logError("NULL pUri pointer.");
        return NULL;
    }

    return &pUri->uriParam.other;
}

bool sipUri_isParamInOther(sipUri_t* pUri, osPointerLen_t* pParam)
{
	bool isExist = false;
	if(!pUri || !pParam)
	{	
		logError("null pointer, pUri=%p, pParam=%p.", pUri, pParam);
		goto EXIT;
	}

	if((pUri->uriParam.uriParamMask & 1<<SIP_URI_PARAM_OTHER) == 0)
	{
		goto EXIT;
	}

	osListElement_t* pLE = pUri->uriParam.other.head;
	while(pLE)
	{
		osPointerLen_t* pOtherValue = pLE->data;
		if(osPL_cmp(pParam, pOtherValue) == 0)
		{
			isExist = true;
			break;
		}

		pLE = pLE->next;
	}

EXIT:
	return isExist;
}
//set up the parsed parameter's data structure based on the paramName
static osStatus_e sipParsing_setUriParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg)
{
	osStatus_e status = OS_STATUS_OK;

	sipUri_t* pSipUri = (sipUri_t*) arg;

    switch (paramName)
	{
        case SIPP_PARAM_URIINFO:
            pSippParsingInfo->arg = &pSipUri->userInfo;
            break;

        case SIPP_PARAM_HOSTPORT:
            pSippParsingInfo->arg = &pSipUri->hostport;
            break;

        case SIPP_PARAM_URIPARAM:
			pSipUri->uriParam.uriParamMask = 0;
			osList_init(&pSipUri->uriParam.other);
            pSippParsingInfo->arg = &pSipUri->uriParam;
            break;

        case SIPP_PARAM_HEADERS:
            pSippParsingInfo->arg = &pSipUri->headers;
            break;

        default:
            logError("unexpected sub parameter for Uri parameter, sipUriABNF.paramName=%s.", paramName);
            status = OS_ERROR_INVALID_VALUE;
    }

	return status;
}


//for encoding, all Uri param is put in sipUriparam_t.other
osStatus_e sipParamUri_encode(osMBuf_t* pSipBuf, sipUri_t* pUri)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pSipBuf || !pUri)
	{
		logError("null pointer, pSipBuf=%p, pUri=%p.", pSipBuf, pUri);
		status = OS_STATUS_OK;
		goto EXIT;		
	}

	if(pUri->sipUser.l !=0)
	{
		osMBuf_writePL(pSipBuf, &pUri->sipUser, true);
	}
	else
	{
		osPointerLen_t uriTypeStr;
		status = sipParamUri_code2name(pUri->sipUriType, &uriTypeStr);
		if(status != OS_STATUS_OK)
		{
			logError("incorrect URI type (%d).", pUri->sipUriType);
			goto EXIT;
		}

		bool isUser = false;
		osMBuf_writePL(pSipBuf, &uriTypeStr, true);
    	osMBuf_writeU8(pSipBuf, ':', true);
		if(pUri->userInfo.sipUser.user.l !=0)
		{
			osMBuf_writePL(pSipBuf, &pUri->userInfo.sipUser.user, true);
			isUser = true;
		}
		if(pUri->userInfo.password.l !=0)
		{
			osMBuf_writePL(pSipBuf, &pUri->userInfo.password, true);
		}

		if(isUser)
		{
			osMBuf_writeU8(pSipBuf, '@', true);
		}
		osMBuf_writePL(pSipBuf, &pUri->hostport.host, true);
	}

	if(pUri->hostport.portValue > 0)
	{
		osMBuf_writeU8(pSipBuf, ':', true);
		osMBuf_writeU32Str(pSipBuf, pUri->hostport.portValue, true);
	}

	if(pUri->uriParam.uriParamMask & 1<<SIP_URI_PARAM_TRANSPORT)
	{
		osMBuf_writeStr(pSipBuf, ";transport=", true);
		osMBuf_writePL(pSipBuf, &pUri->uriParam.transport, true);
	}
    if(pUri->uriParam.uriParamMask & 1<<SIP_URI_PARAM_USER)
    {
        osMBuf_writeStr(pSipBuf, ";user=", true);
        osMBuf_writePL(pSipBuf, &pUri->uriParam.user, true);
    }
    if(pUri->uriParam.uriParamMask & 1<<SIP_URI_PARAM_METHOD)
    {
        osMBuf_writeStr(pSipBuf, ";method=", true);
        osMBuf_writePL(pSipBuf, &pUri->uriParam.method, true);
    }
    if(pUri->uriParam.uriParamMask & 1<<SIP_URI_PARAM_TTL)
    {
        osMBuf_writeStr(pSipBuf, ";ttl=", true);
        osMBuf_writePL(pSipBuf, &pUri->uriParam.ttl, true);
    }
    if(pUri->uriParam.uriParamMask & 1<<SIP_URI_PARAM_MADDR)
    {
        osMBuf_writeStr(pSipBuf, ";maddr=", true);
        osMBuf_writePL(pSipBuf, &pUri->uriParam.maddr, true);
    }
    if(pUri->uriParam.uriParamMask & 1<<SIP_URI_PARAM_LR)
    {
        osMBuf_writeStr(pSipBuf, ";lr", true);
    }
	
	osList_t* pUriParamList = &pUri->uriParam.other;
	osListElement_t* pUriParamLE = pUriParamList->head;
	while(pUriParamLE)
	{
		osMBuf_writeU8(pSipBuf, ';', true);
		sipHdrParamNameValue_t* pParam = pUriParamLE->data;
		osMBuf_writePL(pSipBuf, &pParam->name, true);
		if(pParam->value.l !=0)
		{
			osMBuf_writeU8(pSipBuf, '=', true);
			osMBuf_writePL(pSipBuf, &pParam->value, true);
		}

		pUriParamLE = pUriParamLE->next;
	}

EXIT:
	return status;
}


osStatus_e sipParamUri_create(sipUri_t* pUri)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pUri)
	{
		logError("null pointer, pUri.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(pUri->hostport.host.l == 0)
	{
		sipConfig_getHost(&pUri->hostport.host, &pUri->hostport.portValue);
#if 0
		pUri->hostport.host.p = sipConfig_getHostIP();
		pUri->hostport.host.l = strlen(pUri->hostport.host.p);

		pUri->hostport.portValue = sipConfig_getHostPort();
#endif
	}

EXIT:
	return status;
}


osStatus_e sipParamUri_build(sipUri_t* pUri, sipUriType_e uriType, char* user, uint32_t userLen, char* password, uint32_t pwLen, char* host, uint32_t hostLen, uint32_t port)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pUri || !host)
	{
		logError("Null pointer, pUri=%p, host=%p.", pUri, host);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if((!user && userLen > 0) || hostLen == 0 || (!password && pwLen >0))
    {
        logError("user or host or password input is wrong.  user=%p, userLen=%d, hostLen=%d, password=%p, pwLen=%d.", user, userLen, hostLen, password, pwLen);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	pUri->sipUser.l = 0;
    pUri->sipUriType = uriType;
	pUri->userInfo.sipUser.user.p = user;
	pUri->userInfo.sipUser.user.l = userLen;
	pUri->userInfo.password.p = password;
	pUri->userInfo.password.l = pwLen;
	pUri->hostport.host.p= host;
	pUri->hostport.host.l = hostLen;
	pUri->hostport.portValue = port;
	pUri->uriParam.uriParamMask = 0;
	pUri->uriParam.transport.l=0;
    pUri->uriParam.user.l=0;
    pUri->uriParam.method.l=0;
    pUri->uriParam.ttl.l=0;
    pUri->uriParam.maddr.l=0;
    osList_init(&pUri->uriParam.other);

EXIT:
	return status;
}


osStatus_e sipParamUri_addDisplayName(sipUriExt_t* pUriExt, osPointerLen_t* displayName)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pUriExt)
    {
        logError("Null pointer, pUriExt.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	if(displayName)
	{
		pUriExt->displayName = *displayName;
	}

EXIT:
	return status;
}
	

osStatus_e sipParamUri_addParam(sipUri_t* pUri, sipUriParam_e paramType, void* pParam)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pUri)
    {
        logError("Null pointer, pUri.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	if(paramType != SIP_URI_PARAM_LR && !pParam)
	{
		logError("param is not SIP_URI_PARAM_LR, but the pParam is NULL.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	switch(paramType)
	{
		case SIP_URI_PARAM_TRANSPORT:
			pUri->uriParam.uriParamMask |= 1<<SIP_URI_PARAM_TRANSPORT;
			pUri->uriParam.transport = *(osPointerLen_t*)pParam;
			break;
        case SIP_URI_PARAM_USER:
            pUri->uriParam.uriParamMask |= 1<<SIP_URI_PARAM_USER;
            pUri->uriParam.user = *(osPointerLen_t*)pParam;
            break;
        case SIP_URI_PARAM_METHOD:
            pUri->uriParam.uriParamMask |= 1<<SIP_URI_PARAM_METHOD;
            pUri->uriParam.method = *(osPointerLen_t*)pParam;
            break;
        case SIP_URI_PARAM_LR:
            pUri->uriParam.uriParamMask |= 1<<SIP_URI_PARAM_LR;
            break;
        case SIP_URI_PARAM_MADDR:
            pUri->uriParam.uriParamMask |= 1<<SIP_URI_PARAM_MADDR;
            pUri->uriParam.maddr = *(osPointerLen_t*)pParam;
            break;
        case SIP_URI_PARAM_TTL:
            pUri->uriParam.uriParamMask |= 1<<SIP_URI_PARAM_TTL;
            pUri->uriParam.ttl = *(osPointerLen_t*)pParam;
            break;
        case SIP_URI_PARAM_OTHER:
            pUri->uriParam.uriParamMask |= 1<<SIP_URI_PARAM_OTHER;
			osList_append(&pUri->uriParam.other, (sipHdrParamNameValue_t*)pParam);
            break;
		default:
			logError("invalid paramType (%d),", paramType);
			break;
	}

EXIT:
	return status;
}


osStatus_e sipParamUri_code2name(sipUriType_e uriType, osPointerLen_t* uriStr)
{
	osStatus_e status = OS_STATUS_OK;

	if(!uriStr)
	{
		logError("null pointer, uriStr.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	switch (uriType)
	{
		case URI_TYPE_SIP:
			uriStr->p = "sip";
			uriStr->l = 3; 
			break;
		case URI_TYPE_SIPS:
			uriStr->p = "sips";
			uriStr->l = 4;
			break;
		case URI_TYPE_TEL:
			uriStr->p = "tel";
			uriStr->l = 3;
			break;
		default:
			logError("invalid uriType (%d).", uriType);
			status = OS_ERROR_INVALID_VALUE;
			break;
	}

EXIT:
	return status;
}


//pSipUri: sip:xys@abc.com:5060, where 5060 is port and is only included if isIncludePort==true
osStatus_e sipParamUri_getUriFromRawHdrValue(osPointerLen_t* pHdrValue, bool isIncludePort, osPointerLen_t* pSipUri)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pHdrValue || !pSipUri)
	{
		logError("null pointer, pHdrValue=%p, pSipUri=%p.", pHdrValue, pSipUri);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	//initiate pSipUri
    pSipUri->l = 0;

	//this part is the same logic as in sipParserHdr_genericNameParam() of sipHdrGenericNameParam.c
	size_t pos = 0;
	//bypass empty spaces
	while (pos < pHdrValue->l)
	{
		if(pHdrValue->p[pos] != ' ' && pHdrValue->p[pos] != '\t')
		{
			break;
		}

		++pos;
	}

	if(pos >= pHdrValue->l)
	{
		logError("fails to find sip URI, the hdr is empty.");
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    bool isDisplayName = false;
    bool isRAQ = false;
    if(pHdrValue->p[pos] == '"')
    {
        isDisplayName = true;
        isRAQ = true;
    }
    else if(pHdrValue->p[pos] == '<')
    {
        isRAQ = true;
    }

    size_t origPos = pos;
    if(!isDisplayName && pHdrValue->p[origPos] != '<')
    {
        while(pos < pHdrValue->l)
        {
			//hit sip:, sips:, tel:
            if(pHdrValue->p[pos] == ':')
            {
                //there is no display name, you are done.
                break;
            }

            if(pHdrValue->p[pos] == '<')
            {
                //there is display name, you are done.
                isDisplayName = true;
                isRAQ = true;
                break;
            }

            pos++;
        }
    }

    if(pos >= pHdrValue->l)
    {
        logError("fails to find sip URI, could not determine if there is a display name.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    pos = origPos;
    if(isDisplayName)
    {
        while(pos < pHdrValue->l)
        {
            if(pHdrValue->p[pos++] == '<')
            {
                break;
            }
        }
    }
    else if(pHdrValue->p[pos] == '<')
    {
        //need to start the next level parsing after '<'
        pos++;
    }

	//now we are at the beginning of sip URI
	origPos = pos;
	if(!memcmp(&pHdrValue->p[pos], "sip:", 4) || !memcmp(&pHdrValue->p[pos], "tel:", 4) || !memcmp(&pHdrValue->p[pos], "sips:", 5))
	{
		pSipUri->p = &pHdrValue->p[origPos];
		pSipUri->l = 0;
		pos += pHdrValue->p[pos+3] == 's' ? 5 : 4;

		while(pos < pHdrValue->l)
		{
			if(pHdrValue->p[pos] == ';' || pHdrValue->p[pos] == '>' || pHdrValue->p[pos] == (isIncludePort ? '>' : ':'))
			{
				break;
			}
			pos++;
		}

		pSipUri->l = pos - origPos;
		goto EXIT;
	}

	logError("the hdr does not contain sip or tel uri.")
	status = OS_ERROR_INVALID_VALUE;

EXIT:
	return status;
}	


//if the specified hdr has multiple value, get the sip uri from the first value.
osStatus_e sipParamUri_getUriFromSipMsg(osMBuf_t* pSipBuf, bool isIncludePort, osPointerLen_t* pSipUri, sipHdrName_e hdrCode)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipBuf || !pSipUri)
    {
        logError("null pointer, pSipBuf=%p, pSipUri=%p.", pSipBuf, pSipUri);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	sipRawHdr_t rawHdr;
	status = sipDecode_getTopRawHdr(pSipBuf, &rawHdr, hdrCode);
    if(status != OS_STATUS_OK)
    {
        logError("fails to sipDecode_getTopRawHdr.");
        goto EXIT;
    }

	status = sipParamUri_getUriFromRawHdrValue(&rawHdr.value, isIncludePort, pSipUri);
    if(status != OS_STATUS_OK)
    {
        logError("fails to sipParamUri_getUriFromRawHdrValue.");
        goto EXIT;
    }

EXIT:
	return status;
}	


osStatus_e sipUri_saParse(osPointerLen_t* pRawUri, sipUri_t* pUri)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pRawUri || !pUri)
	{
		logError("null pointer, pRawUri=%p, pUri=%p.", pRawUri, pUri);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	osMBuf_t uriBuf = {(char*)pRawUri->p, pRawUri->l, 0, pRawUri->l}; 

    //start parsing the header URI
    osList_init(&pUri->headers);
    sipParsingStatus_t parsingStatus;
    sipParsingInfo_t parentParsingInfo;
    parentParsingInfo.arg = pUri;
    parentParsingInfo.token[0]='>';
    parentParsingInfo.extTokenNum=0;
    parentParsingInfo.tokenNum = 1;
    status = sipParamUri_parse(&uriBuf, pRawUri->l, &parentParsingInfo, &parsingStatus);
    if(status != OS_STATUS_OK)
    {
        logError("parsing URI failure, status=%d.", status);
        goto EXIT;
    }

EXIT:
	return status;
}


void sipUri_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	sipUri_t* pUri = data;
	sipUriparam_cleanup(&pUri->uriParam);
	sipHostport_cleanup(&pUri->hostport);
	sipUserinfo_cleanup(&pUri->userInfo);

	//for now, assume there is no headers parameters, otherwise, need to perform osList_delete() for each header
}

void sipParamUri_clear(sipUri_t* pUri)
{
	if(!pUri)
	{
		return;
	}

	osList_clear(&pUri->uriParam.other);
}
