#include <string.h>
#include "osDebug.h"
#include "osTypes.h"
#include "osPL.h"
#include "osList.h"
#include "osMemory.h"
#include "osList.h"

#include "sipParsing.h"
#include "sipUriparam.h"
#include "sipGenericNameParam.h"


static bool sipUriParamOtherExist(osList_t *pList, char* pOther, int len);
static sipHdrParamNameValue_t* sipUriParamAddOther(osList_t *pList, sipHdrParamNameValue_t* pOther, char* otherInfo, size_t len);


osStatus_e sipParser_uriparam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParentParsingInfo, sipParsingStatus_t* pParsingStatus)
{
	debug("enter.");

	osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pParsingStatus || !pParentParsingInfo)
    {
        logError("NULL pointer is passed in, pSipMsg=%p, pParsingStatus=%p, pParentParsingInfo=%p.", pSipMsg, pParsingStatus, pParentParsingInfo);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pParsingStatus->status = SIPP_STATUS_OK;
    pParsingStatus->isEOH = false;
    pParsingStatus->tokenMatched = SIP_TOKEN_INVALID;

    sipUriparam_t* pUriParam = (sipUriparam_t*) pParentParsingInfo->arg;
//	pUriParam->uriParamMask = 0;
//	osList_init(&pUriParam->other);

	//construct token list
	char token[SIPP_MAX_PARAM_NUM];
	token[0] = ';';
	for(int i=0; i< pParentParsingInfo->tokenNum; i++)
	{
		token[i+1] = pParentParsingInfo->token[i];
	}
	uint8_t tokenNum = pParentParsingInfo->tokenNum+1;
	uint8_t internalTokenNum = 1;

	char msgChar;
	sipUriParam_e uriParamName = SIP_URI_PARAM_INVALID;
	bool isCheckName = true;
	int valueLen = 0;
	int nameLen = 0;
	size_t origPos = pSipMsg->pos;
	sipHdrParamNameValue_t* pOther = NULL;
	while(pSipMsg->pos < hdrEndPos)
	{
		msgChar = pSipMsg->buf[pSipMsg->pos];

		if(isCheckName)
		{
			if(SIP_IS_PARAMCHAR(msgChar))
			{
				pSipMsg->pos++;
			}
			else if(msgChar == '=')
			{
				nameLen = pSipMsg->pos - origPos;
				switch(nameLen)
				{
					case 3:
						if(strncmp(&pSipMsg->buf[origPos], "ttl", 3) == 0)
						{
							if(pUriParam->uriParamMask & 1 << SIP_URI_PARAM_TTL)
							{
								logError("SIP URI parameter (SIP_URI_PARAM_TTL) is duplicated.");
								status = OS_ERROR_INVALID_VALUE;
								goto EXIT;
							}
							uriParamName = SIP_URI_PARAM_TTL;
						}
						else
						{
							uriParamName = SIP_URI_PARAM_OTHER;
						}

						isCheckName = false;
						break;
					case 4:
    	                if(strncmp(&pSipMsg->buf[origPos], "user", 4) == 0)
        	            {
            	            if(pUriParam->uriParamMask & 1 << SIP_URI_PARAM_USER)
							{
                	            logError("SIP URI parameter (SIP_URI_PARAM_USER) is duplicated.");
                    	        status = OS_ERROR_INVALID_VALUE;
                        	    goto EXIT;
                        	}

							uriParamName = SIP_URI_PARAM_USER;
    	                }
        	            else
            	        {
                	        uriParamName = SIP_URI_PARAM_OTHER;
    	                }
						isCheckName = false;
            	    	break;
                	case 5:
	                    if(strncmp(&pSipMsg->buf[origPos], "maddr", 5) == 0)
    	                {
        	                if(pUriParam->uriParamMask & 1 << SIP_URI_PARAM_MADDR)
							{
            	                logError("SIP URI parameter (SIP_URI_PARAM_MADDR) is duplicated.");
                	            status = OS_ERROR_INVALID_VALUE;
                    	        goto EXIT;
                        	}

	                        uriParamName = SIP_URI_PARAM_MADDR;
        	            }
            	        else
                	    {
                    	    uriParamName = SIP_URI_PARAM_OTHER;
    	                }
						isCheckName = false;
            	        break;
                	case 6:
	                    if(strncmp(&pSipMsg->buf[origPos], "method", 6) == 0)
    	                {
        	                if(pUriParam->uriParamMask & 1 << SIP_URI_PARAM_METHOD)
            	        	{
						        logError("SIP URI parameter (SIP_URI_PARAM_METHOD) is duplicated.");
                	            status = OS_ERROR_INVALID_VALUE;
                    	        goto EXIT;
                        	}

							uriParamName = SIP_URI_PARAM_METHOD;
        	            }
            	        else
                	    {
                    	    uriParamName = SIP_URI_PARAM_OTHER;
    	                }
						isCheckName = false;
            	        break;
                	case 9:
	                    if(strncmp(&pSipMsg->buf[origPos], "transport", 9) == 0)
    	                {
        	                if(pUriParam->uriParamMask & 1 << SIP_URI_PARAM_TRANSPORT)
            	            { 
							    logError("SIP URI parameter (SIP_URI_PARAM_TRANSPORT) is duplicated.");
                	            status = OS_ERROR_INVALID_VALUE;
                    	        goto EXIT;
                        	}

							uriParamName = SIP_URI_PARAM_TRANSPORT;
        	            }
            	        else
                	    {
							uriParamName = SIP_URI_PARAM_OTHER;
    	                }
						isCheckName = false;
            	        break;
					default:
						uriParamName = SIP_URI_PARAM_OTHER;
						isCheckName = false;
						break;
				}	

				if(uriParamName == SIP_URI_PARAM_OTHER)
				{
					if(sipUriParamOtherExist(&pUriParam->other, &pSipMsg->buf[origPos], nameLen))
					{
                		logError("SIP URI parameter (SIP_URI_PARAM_OTHER) is duplicated, first char= %c.", pSipMsg[origPos]);
                   		status = OS_ERROR_INVALID_VALUE;
                    	goto EXIT;
              		}
					else
					{
						pOther = sipUriParamAddOther(&pUriParam->other, pOther, &pSipMsg->buf[origPos], nameLen);
						if(pOther ==NULL)
						{
							logError("pOther allocation fails.");
							status = OS_ERROR_MEMORY_ALLOC_FAILURE;
							goto EXIT;
						}
					}
				}

				origPos = ++pSipMsg->pos;
				pUriParam->uriParamMask |= 1 << uriParamName;
			}
			else if(SIP_IS_MATCH_TOKEN(msgChar, token, 0, tokenNum))
			{
				nameLen = pSipMsg->pos - origPos;
				if(nameLen == 2 && strncmp(&pSipMsg->buf[origPos], "lr", 2) == 0)
				{
					if(pUriParam->uriParamMask & 1 << SIP_URI_PARAM_LR)
                    {
						logError("SIP URI parameter (SIP_URI_PARAM_LR) is duplicated.");
                        status = OS_ERROR_INVALID_VALUE;
                        goto EXIT;
                    }

                    uriParamName = SIP_URI_PARAM_LR;
                    pUriParam->uriParamMask |= 1 << SIP_URI_PARAM_LR;
                }
                else
                {
                    pUriParam->uriParamMask |= 1 << SIP_URI_PARAM_OTHER;
					if(sipUriParamOtherExist(&pUriParam->other, &pSipMsg->buf[origPos], nameLen))
					{
	                    logError("SIP URI parameter (SIP_URI_PARAM_OTHER) is duplicated, first char= %c.", pSipMsg[origPos]);
    	                status = OS_ERROR_INVALID_VALUE;
        	            goto EXIT;
            	    }
					else
					{
						if(sipUriParamAddOther(&pUriParam->other, pOther, &pSipMsg->buf[origPos], nameLen) == NULL)
                        {
                            logError("pOther allocation fails.");
                            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
                            goto EXIT;
						}
					}
				}

				pParsingStatus->tokenMatched = msgChar;
				//if match internal token, continue parsing
				if(SIP_IS_MATCH_TOKEN(msgChar, token, 0, internalTokenNum))
				{
					origPos = ++pSipMsg->pos;
					pOther = NULL;
                	continue;
				}
				else
				{
					goto EXIT;
				} 
			}
			else
			{
				logError("SIP URI parameter has a illegal char (%d).", msgChar);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}
		}
		else
		//now check the uri-param value
		{
			if(SIP_IS_MATCH_TOKEN(pSipMsg->buf[pSipMsg->pos], token, 0, tokenNum))
			{
				switch(uriParamName)
				{
    				case SIP_URI_PARAM_TRANSPORT:
						pUriParam->transport.p = &pSipMsg->buf[origPos];
						pUriParam->transport.l = pSipMsg->pos - origPos;
						break;
    				case SIP_URI_PARAM_USER:
                        pUriParam->user.p = &pSipMsg->buf[origPos];
						pUriParam->user.l = pSipMsg->pos - origPos;
						break;
    				case SIP_URI_PARAM_METHOD:
                        pUriParam->method.p = &pSipMsg->buf[origPos];
						pUriParam->method.l = pSipMsg->pos - origPos;
						break;
    				case SIP_URI_PARAM_TTL:
                        pUriParam->ttl.p = &pSipMsg->buf[origPos];
                        pUriParam->ttl.l = pSipMsg->pos - origPos;
                        break;
    				case SIP_URI_PARAM_MADDR:
                        pUriParam->maddr.p = &pSipMsg->buf[origPos];
                        pUriParam->maddr.l = pSipMsg->pos - origPos;
                        break;
    				case SIP_URI_PARAM_OTHER:
						sipUriParamAddOther(&pUriParam->other, pOther, &pSipMsg->buf[origPos], pSipMsg->pos-origPos);
						break;
				}

                pParsingStatus->tokenMatched = msgChar;
                if(SIP_IS_MATCH_TOKEN(msgChar, token, 0, internalTokenNum))
				{
					origPos = ++pSipMsg->pos;
                    pOther = NULL;
					isCheckName = true;
					continue;
				}
				else
				{
					goto EXIT;
				}
			}
			else
			{
				switch(uriParamName)
				{
					case SIP_URI_PARAM_TTL:
						if(SIP_IS_DIGIT(msgChar) && ++valueLen <=3)
						{
							pSipMsg->pos++;
						}
						else
						{
							logError("SIP URI parameter TTL has a illegal char (%c), or exceed 3 digits (d).", msgChar, valueLen);
							status = OS_ERROR_INVALID_VALUE;
							goto EXIT;
						}
						break;
					default:
						pSipMsg->pos++;
						break;
				}
			}
		}
	}

    pParsingStatus->isEOH = true;
	if(origPos == pSipMsg->pos)
	{
		logError("a empty parameter.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}
	else if(isCheckName)
	{
        nameLen = pSipMsg->pos - origPos;
        if(nameLen == 2 && strncmp(&pSipMsg->buf[origPos], "lr", 2) == 0)
        {
            if(pUriParam->uriParamMask & 1 << SIP_URI_PARAM_LR)
            {
                logError("SIP URI parameter (SIP_URI_PARAM_LR) is duplicated.");
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }
            uriParamName = SIP_URI_PARAM_LR;
            pUriParam->uriParamMask |= 1 << SIP_URI_PARAM_LR;
        }
        else
        {
            pUriParam->uriParamMask |= 1 << SIP_URI_PARAM_OTHER;
            if(sipUriParamOtherExist(&pUriParam->other, &pSipMsg->buf[origPos], nameLen))
            {
                logError("SIP URI parameter (SIP_URI_PARAM_OTHER) is duplicated, first char= %c.", pSipMsg[origPos]);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }
            else
            {
                if(sipUriParamAddOther(&pUriParam->other, pOther, &pSipMsg->buf[origPos], nameLen) == NULL)
                {
                    logError("pOther allocation fails.");
                    status = OS_ERROR_MEMORY_ALLOC_FAILURE;
                    goto EXIT;
                }
            }
        }
	}
	else
	{
	    switch(uriParamName)
        {
            case SIP_URI_PARAM_TRANSPORT:
                pUriParam->transport.p = &pSipMsg->buf[origPos];
                pUriParam->transport.l = pSipMsg->pos - origPos;
                break;
            case SIP_URI_PARAM_USER:
                pUriParam->user.p = &pSipMsg->buf[origPos];
                pUriParam->user.l = pSipMsg->pos - origPos;
                break;
            case SIP_URI_PARAM_METHOD:
                pUriParam->method.p = &pSipMsg->buf[origPos];
                pUriParam->method.l = pSipMsg->pos - origPos;
                break;
            case SIP_URI_PARAM_TTL:
                pUriParam->ttl.p = &pSipMsg->buf[origPos];
                pUriParam->ttl.l = pSipMsg->pos - origPos;
                break;
            case SIP_URI_PARAM_MADDR:
                pUriParam->maddr.p = &pSipMsg->buf[origPos];
                pUriParam->maddr.l = pSipMsg->pos - origPos;
                break;
            case SIP_URI_PARAM_OTHER:
                debug("sean, add value for uriParamName=%d, value len=%ld.", uriParamName, pSipMsg->pos-origPos+1);
                sipUriParamAddOther(&pUriParam->other, pOther, &pSipMsg->buf[origPos], pSipMsg->pos-origPos+1);
                break;
        }
	}

EXIT:
	if(status == OS_STATUS_OK)
	{
		if(++pSipMsg->pos ==  hdrEndPos)
		{
			pParsingStatus->isEOH = true;
		}
	}

	debug("exit, status=%d.", status);
	return status;
}


static bool cmpOtherName(osListElement_t *le, void *arg)
{
	DEBUG_BEGIN

	sipHdrParamNameValue_t* pOther = (sipHdrParamNameValue_t*) le->data;
	osPointerLen_t* pName = &pOther->name;

	if(osPL_casecmp(pName, (osPointerLen_t*) arg) == 0)
	{
		return true;
	}

	return false;
}


static bool sipUriParamOtherExist(osList_t *pList, char* pOther, int len)
{
	DEBUG_BEGIN;

	bool status = false;

	if(!pList || !pOther)
	{
		logError("NULL pointer, pList=%p, pOther=%p.", pList, pOther);
		goto EXIT;
	}

	osPointerLen_t namePL;
	namePL.p = pOther;
	namePL.l = len;

	if(osList_lookup(pList, true, cmpOtherName, &namePL) != NULL)
	{
		status = true;
	}

EXIT:
	DEBUG_END;
	return status;
}


static sipHdrParamNameValue_t* sipUriParamAddOther(osList_t *pList, sipHdrParamNameValue_t* pOther, char* otherInfo, size_t len)
{
//	DEBUG_BEGIN

	sipHdrParamNameValue_t* retPt = NULL;

	//pLE==NULL when it is for other-param name
	if(pOther == NULL)
	{
		pOther = osMem_alloc(sizeof(sipHdrParamNameValue_t), NULL);
		if(pOther == NULL)
		{
			logError("could not allocate memory for sipHdrParamNameValue_t.");
			goto EXIT;
		}

		pOther->name.p = otherInfo;
		pOther->name.l = len;

		osListElement_t* pLE = osList_append(pList, pOther);
		if(pLE == NULL)
		{
			logError("osList_append failure.");
			osMem_deref(pOther);
			goto EXIT;
		}

		retPt = pOther;
	}
	else
	//for other-param value
	{
		pOther->value.p = otherInfo; 
		pOther->value.l = len;

		retPt = pOther;
	}

EXIT:
//	DEBUG_END
	return retPt;
}


void sipUriparam_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	sipUriparam_t* pUriParam = data;
	osList_delete(&pUriParam->other);
}
