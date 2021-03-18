/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipParsing.c  Overall SIP parsing function
 ********************************************************/

#include "osDebug.h"
#include "osPL.h"
#include "osMemory.h"
#include "sipParsing.h"
#include "sipDebug.h"
#include "./sipGenericNameParam.h"


static bool sipParsing_cmpParam(osListElement_t *le, void *arg);
static void printTokenInfo(int abnfNum, sipParsingInfo_t sippParsingInfo[]);


osStatus_e sipParsing_getHdrValue(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingABNF_t* sipABNF, sipParsingInfo_t* sippInfo, uint8_t sbnfNum, sipParsingStatus_t* pParsingStatus)
{
	mDEBUG_BEGIN(LM_SIPP)

	osStatus_e status = OS_STATUS_OK;
	char tokenMatched = SIP_TOKEN_INVALID;
	size_t origPos;
	bool isEOH = false;

	if(!pSipMsg || !sipABNF || !sippInfo)
	{
        logError("NULL pointer is passed in, pSipMsg=%p, sipABNF=%p, sippInfo=%p.", pSipMsg, sipABNF, sippInfo);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	for(int i=0; i<sbnfNum; i++)
	{
//		DEBUG_SIP_PRINT_TOKEN(sippInfo[i].token, sippInfo[i].tokenNum);
		mdebug(LM_SIPP, "start parsing for: sbnfNum=%d, idx=%d, paramName=%d, sipABNF[i].Token='%c'(0x%x). Previous parsing: isEOH=%d, tokenMatched='%c'(0x%x), pos=%ld", sbnfNum, i, sipABNF[i].paramName, sipABNF[i].extToken, sipABNF[i].extToken, isEOH, tokenMatched, tokenMatched, pSipMsg->pos);
		//if tokenMatched does not match the one starting the current parameter, check if the current parameter is optional
		if(isEOH || (tokenMatched != SIP_TOKEN_INVALID && tokenMatched != sipABNF[i].extToken))
		{
			//current parameter is not optional
			if(sipABNF[i].a > 0)
			{
				logError("sipABNF[%d] is not optional, but the preceeding parsing indicates this parameter is to be bypassed. isEOH=%d, tokenMatched=%c, sipABNF[%d].token= %c.", i, isEOH, tokenMatched, i, sipABNF[i].extToken);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}
				
			//current parameter is optional, bypass
			continue;
		}
 
		origPos = pSipMsg->pos;
		
//		mdebug(LM_SIPP, "sean, i=%d, sbnf=%d, origPos=%ld, sipABNF[i].a=%d, sipABNF[i].b=%d", i, sbnfNum, origPos, sipABNF[i].a, sipABNF[i].b);
		if(pSipMsg->pos >= pSipMsg->end)
		{
			logError("sip parser reachs the end of sip message");
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		//start the processing of the current parameter
		int j=0;
		for(j=sipABNF[i].a; j<=sipABNF[i].b; j++)
		{
			tokenMatched = SIP_TOKEN_INVALID;

			if(j != 0 && isEOH)
			{
				logError("Sip message parsing error, EOH while mandatory parameters is expected, i=%d, j=%d.", i, j);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}
 
			status = sipABNF[i].parsingFunc(pSipMsg, hdrEndPos, &sippInfo[i], pParsingStatus);

			mdebug(LM_SIPP, "after parsingFunc() for sipABNF[%d], paramName=%d, status=%d, isEOH=%d, tokenMatched='%c'(0x%x)", i, sipABNF[i].paramName, status, pParsingStatus->isEOH, pParsingStatus->tokenMatched, pParsingStatus->tokenMatched);
			if(status != OS_STATUS_OK)
			{
				// if this parameter is optional (sipABNF[i].a == 0 || j > sipABNF[i].a)
                if(j==0 && status == OS_ERROR_INVALID_VALUE && pParsingStatus->status != SIPP_STATUS_DUPLICATE_PARAM && (sipABNF[i].a == 0 || j > sipABNF[i].a))
				{
					mdebug(LM_SIPP, "no match found for this parameter(ABNF idx=%d), but this parameter is optional(sipABNF[i].a=%d, j=%d), try next one.", i, sipABNF[i].a, j);

					if(sipABNF[i].cleanup)
					{	
						sipABNF[i].cleanup(sippInfo[i].arg);
					}
					pSipMsg->pos = origPos;
					break;
				}

				logError("parsing error(status=%d), pParsingStatus->status=%d, i=%d, sipABNF[i].a=%d, j=%d", status, pParsingStatus->status, i, sipABNF[i].a, j);
				goto EXIT;
			}

			//status =OK, tokenMatch is found or EOH
			if(pParsingStatus->isEOH)
			{
				isEOH = pParsingStatus->isEOH;

				
				if(j==sipABNF[i].a && j !=0 && pParsingStatus->status == SIPP_STATUS_EMPTY)
				{
                	logError("Sip message parsing error, a mandatory parameter (paramName=%d) is empty", sipABNF[i].paramName);
                	status = OS_ERROR_INVALID_VALUE;
                	goto EXIT;
            	}

				break;
			}
			else
			{
				tokenMatched = pParsingStatus->tokenMatched;
//				DEBUG_SIP_PRINT_TOKEN(sippInfo[i].token, sippInfo[i].tokenNum);
				if(SIP_IS_MATCH_TOKEN(tokenMatched, sippInfo[i].token, 0, sippInfo[i].tokenNum-sippInfo[i].extTokenNum))
				{
					mdebug(LM_SIPP, "tokenMatched='%c'(0x%x), match local token.", tokenMatched, tokenMatched);
					if(sipABNF[i].isInternalMatchContinue)
					{
						continue;
					}
					else
					{
						break;
					}
				}
				
				//if matchedToken is for next parameter defined in the parent token, we are done	
				if(SIP_IS_MATCH_TOKEN(tokenMatched, sippInfo[i].token, sippInfo[i].tokenNum-sippInfo[i].extTokenNum, sippInfo[i].tokenNum))
				{
					mdebug(LM_SIPP, "tokenMatched='%c'(0x%x), match parentToken.", tokenMatched, tokenMatched);
					//check if there is mandatory parameters remaining.  if yes, this is an error
					for(int k=i+1; k<sbnfNum; k++)
					{
						if(sipABNF[k].a > 0)
						{
							logError("parsing finds a matching token for the parent parameter, but still there is mandatory sub parameters in the current parameter.");
							status = OS_ERROR_INVALID_VALUE;
						}
					}
					goto EXIT;
				}
				//the last iteration, but matchedToken is still the current parameter's internal token, the next parameter's token not found, and not isEOH
				else if(j==sipABNF[i].b-1)
				{
                    logError("Sip message parsing error, a parameter (paramName=%d) has more arguments than defined (%d)", sipABNF[i].paramName, sipABNF[i].b);
                    status = OS_ERROR_INVALID_VALUE;
                    goto EXIT;
                }

				mdebug(LM_SIPP, "I shall not be here.");
			}
		}
	}

	//if isEOH and there is mandatory after isEOH is reached, the previous logic shall have jumped to EXIT with error status.
	//when the call flow goes through all sbnfNum, and reach here, the call flow finishes properly 
	//the check may not needed, just add here to enforce the proper status
	if(isEOH)
	{
		status = OS_STATUS_OK;
	}
EXIT:
	mDEBUG_END(LM_SIPP)
	return status;
}


void sipParsing_setParsingInfo(sipParsingABNF_t sipABNF[], int abnfNum, sipParsingInfo_t* pParentParsingInfo, sipParsingInfo_t sippParsingInfo[], sipParsing_setUniqueParsingInfo_h setUniqueParsingInfo_handler)
{
	mDEBUG_BEGIN(LM_SIPP)

    int prevMandatory = 0;  
    int lastMandatory = prevMandatory;

	for(int i=0; i<abnfNum; i++)
	{
		//set up the parsed parameter's data structure based on the paramName
		setUniqueParsingInfo_handler(sipABNF[i].paramName, &sippParsingInfo[i], pParentParsingInfo->arg);
/*
        // set up the parameter's external token based on ABNF
        if(sipABNF[i].extToken != SIP_TOKEN_INVALID)
        {
            sippParsingInfo[i].extToken[sippParsingInfo[i].extTokenNum++] = sipABNF[i].extToken;
            sippParsingInfo[i].extTokenNum++;
        }
*/
        if(sipABNF[i].a > 0 || i==(abnfNum-1))
        {
            lastMandatory = i;
        }

//		mdebug(LM_SIPP, "sean, lastMandatory=%d, prevMandatory=%d", lastMandatory, prevMandatory);
        //add external tokens for optional parameters in the middle
        for(int j=prevMandatory; j<lastMandatory; j++)
        {
			sippParsingInfo[j].tokenNum = 0;
            for(int k=j+1; k<=lastMandatory; k++)
            {
                if(sipABNF[k].extToken != SIP_TOKEN_INVALID)
                {
                    sippParsingInfo[j].token[sippParsingInfo[j].tokenNum++] = sipABNF[k].extToken;
//					mdebug(LM_SIPP, "add local token (%c) to the token list, sippParsingInfo idx=%d, tokenNum=%d, extTokenNum=%d", sippParsingInfo[j].token[sippParsingInfo[j].tokenNum-1], j, sippParsingInfo[j].tokenNum, sippParsingInfo[j].extTokenNum);
                }
            }
        }

		//lastMandatory is always set for the last parameter, even if that aprameter is optional.  At the end of look, need to reset the lastMandatory to the prevMandatory so that the parent tokens can be taken  
		if(i==(abnfNum-1) && sipABNF[i].a == 0)
		{
			lastMandatory = prevMandatory;
		}
		else
		{
        	prevMandatory = lastMandatory;
		}
    }

	// add the parent parameter's token to the paremeters in and after the last mandatory parameters
    for(int i=lastMandatory; i<abnfNum; i++)
    {
//    	mdebug(LM_SIPP, "sean-remove, add parent token to token list, sippParsingInfo idx=%d, tokenNum=%d, extTokenNum=%d, parent tokenNum=%d.", i, sippParsingInfo[i].tokenNum, sippParsingInfo[i].extTokenNum, pParentParsingInfo->tokenNum);

		sippParsingInfo[i].extTokenNum = 0;
        for(int j=0; j<pParentParsingInfo->tokenNum; j++)
        {
            sippParsingInfo[i].token[sippParsingInfo[i].tokenNum++] = pParentParsingInfo->token[j];
			sippParsingInfo[i].extTokenNum++;
        }
    }

	printTokenInfo(abnfNum, sippParsingInfo);

	mDEBUG_END(LM_SIPP)
}


osStatus_e sipParsing_plGetParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParsingInfo, sipParsingStatus_t* pStatus)
{
	mDEBUG_BEGIN(LM_SIPP)

	osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pStatus || !pParsingInfo)
    {
        logError("NULL pointer is passed in, pSipMsg=%p, pParsingStatus=%p, pParsingInfo=%p.", pSipMsg, pStatus, pParsingInfo);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pStatus->status = SIPP_STATUS_OK;
    pStatus->isEOH = false;
    pStatus->tokenMatched = SIP_TOKEN_INVALID;

    osPointerLen_t* pParm = (osPointerLen_t*) pParsingInfo->arg;
    pParm->p = (void*) &pSipMsg->buf[pSipMsg->pos];
    pParm->l = 0;
    size_t origPos = pSipMsg->pos;

    if(pParsingInfo->tokenNum ==0)
    {
		pSipMsg->pos = hdrEndPos;
        pStatus->isEOH = true;
        goto EXIT;
    }

//	DEBUG_SIP_PRINT_TOKEN(pParsingInfo->token, pParsingInfo->tokenNum);
    char msgChar;
    while(pSipMsg->pos < hdrEndPos)
    {
        msgChar = pSipMsg->buf[pSipMsg->pos];
        if(SIP_IS_MATCH_TOKEN(pSipMsg->buf[pSipMsg->pos], pParsingInfo->token, 0, pParsingInfo->tokenNum))
		{
            //find the token, done
            pStatus->tokenMatched = msgChar;
            goto EXIT;
        }

		pSipMsg->pos++;
    }

    mdebug(LM_SIPP, "no tokenMatch, reach the header end.");
	pStatus->status = SIPP_STATUS_TOKEN_NOT_MATCH;
	if(pParsingInfo->extTokenNum != 0 && !SIP_HAS_TOKEN_EOH(pParsingInfo->token, pParsingInfo->tokenNum))
	{
    	status = OS_ERROR_INVALID_VALUE;
	}
	pStatus->isEOH = true;

EXIT:
    if(status == OS_STATUS_OK)
	{
        pParm->l = pSipMsg->pos - origPos;
    }

    if(++pSipMsg->pos == hdrEndPos)
	{
		pStatus->isEOH = true;
	}

	mDEBUG_END(LM_SIPP)
    return status;
}


osStatus_e sipParsing_listPLGetParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParsingInfo, sipParsingStatus_t* pStatus)
{
    mDEBUG_BEGIN(LM_SIPP)

    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pStatus || !pParsingInfo)
    {
        logError("NULL pointer is passed in, pSipMsg=%p, pParsingStatus=%p, pParsingInfo=%p.", pSipMsg, pStatus, pParsingInfo);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pStatus->status = SIPP_STATUS_OK;
    pStatus->isEOH = false;
    pStatus->tokenMatched = SIP_TOKEN_INVALID;

    osList_t* pList = pParsingInfo->arg;
//    osPointerLen_t* pParm = (osPointerLen_t*) pParsingInfo->arg;
//    pParm->p = (void*) &pSipMsg->buf[pSipMsg->pos];
    size_t  len=0;
    size_t origPos = pSipMsg->pos;

    if(pParsingInfo->tokenNum ==0)
    {
        pSipMsg->pos = hdrEndPos;
        pStatus->isEOH = true;
        goto EXIT;
    }

//  DEBUG_SIP_PRINT_TOKEN(pParsingInfo->token, pParsingInfo->tokenNum);
	size_t pNameLen = 0;
    char msgChar;
    while(pSipMsg->pos < hdrEndPos)
    {
        msgChar = pSipMsg->buf[pSipMsg->pos];
        if(SIP_IS_MATCH_TOKEN(msgChar, pParsingInfo->token, 0, pParsingInfo->tokenNum))
        {
            //find the token, done
            pStatus->tokenMatched = msgChar;
            goto EXIT;
        }
		else if (msgChar == '=' && pNameLen==0)
		{
			pNameLen = pSipMsg->pos - origPos;
		}

        pSipMsg->pos++;
    }

    mdebug(LM_SIPP, "no tokenMatch, reach the header end, tokenNum=%d, extTokenNum=%d.", pParsingInfo->tokenNum, pParsingInfo->extTokenNum);
    pStatus->status = SIPP_STATUS_TOKEN_NOT_MATCH;
    if(pParsingInfo->extTokenNum != 0 && !SIP_HAS_TOKEN_EOH(pParsingInfo->token, pParsingInfo->tokenNum))
    {
        status = OS_ERROR_INVALID_VALUE;
    }
    pStatus->isEOH = true;

EXIT:
    if(status == OS_STATUS_OK)
    {
        len = pSipMsg->pos - origPos;

		if(pNameLen ==0)
		{
			pNameLen = len;
		}

		mdebug(LM_SIPP, "sean, len=%ld, pNameLen=%ld", len, pNameLen);
        if(len)
        {
			size_t pNameStartPos = origPos;
			size_t pNameEndPos = origPos+pNameLen -1;
            size_t pValueStartPos = origPos+pNameLen +1;
			size_t pOrigValueStartPos = pValueStartPos;
        	for(int i=0; i<pNameLen; i++)
        	{
            	if(SIP_IS_LWS(pSipMsg->buf[pNameEndPos-i]))
				{
					pNameLen--;
				}
				else
				{
					break;
				}
			}

            for(int i=0; i<pNameLen; i++)
            {
                if(SIP_IS_LWS(pSipMsg->buf[pNameStartPos+i]))
				{
					pNameStartPos++;
					pNameLen--;
				}
				else
				{
					break;
				}
			}
					
			for(int i=pOrigValueStartPos; i<pSipMsg->pos; i++)
			{
				if(SIP_IS_LWS(pSipMsg->buf[i]))
				{
					pValueStartPos++;
				}
				else
				{
					break;
				}
			}

			size_t valueLen = pSipMsg->pos < pValueStartPos ? 0 : (pSipMsg->pos - pValueStartPos);
			for(int i=1; i<=valueLen; i++)
			{
				if(SIP_IS_LWS(pSipMsg->buf[pSipMsg->pos-i]))
				{
					valueLen--;
				}
				else
				{
					break;
				}
			}

            if(sipParsing_isParamExist(pList, &pSipMsg->buf[pNameStartPos], pNameLen))
            {
                logError("duplicate header parameter.");
                pStatus->status = SIPP_STATUS_DUPLICATE_PARAM;
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT1;
            }

            status = sipParsing_listAddParam(pList, &pSipMsg->buf[pNameStartPos], pNameLen, &pSipMsg->buf[pValueStartPos], valueLen);
        }
    }

EXIT1:
    if(++pSipMsg->pos == hdrEndPos)
    {
        pStatus->isEOH = true;
    }

    mDEBUG_END(LM_SIPP)
    return status;
}




bool sipParsing_isParamExist(osList_t *pList, char* param, int len)
{
    mDEBUG_BEGIN(LM_SIPP)

    bool status = false;

    if(!pList || !param)
    {
        logError("NULL pointer, pList=%p, param=%p.", pList, param);
        goto EXIT;
    }

    osPointerLen_t paramPL;
    paramPL.p = param;
    paramPL.l = len;

    if(osList_lookup(pList, true, sipParsing_cmpParam, &paramPL) != NULL)
    {
        status = true;
    }

EXIT:
    mDEBUG_END(LM_SIPP)
    return status;
}


osStatus_e sipParsing_listAddParam(osList_t *pList, char* nameParam, size_t nameLen, char* valueParam, size_t valueLen)
{
//  mDEBUG_BEGIN(LM_SIPP)
    osStatus_e status = OS_STATUS_OK;

    sipHdrParamNameValue_t* paramPL = osmalloc(sizeof(sipHdrParamNameValue_t), NULL);
    if(paramPL == NULL)
    {
        logError("could not allocate memory for paramPL.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    paramPL->name.p = nameParam;
    paramPL->name.l = nameLen;
    paramPL->value.p = valueParam;
    paramPL->value.l = valueLen;

    osListElement_t* pLE = osList_append(pList, paramPL);

    if(pLE == NULL)
    {
        logError("osList_append failure.");
        osfree(paramPL);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

EXIT:
//  mDEBUG_END(LM_SIPP)
    return status;
}



bool SIP_IS_MATCH_TOKEN(char a, char token[], int tokenStart, int tokenStop)
{
	for(int i=tokenStart; i<tokenStop; i++) 
	{
		if (a==token[i]) 
		{
			return true;
		}
	}

	return false;
}


bool SIP_HAS_TOKEN_EOH(char token[], int tokenNum)
{
	for(int i=0; i<tokenNum; i++)
	{
		if(token[i] == SIP_TOKEN_EOH)
		{
			return true;
		}
	}

	return false;
}



static bool sipParsing_cmpParam(osListElement_t *le, void *arg)
{
    mDEBUG_BEGIN(LM_SIPP)
	bool status = false;

	sipHdrParamNameValue_t* leData = le->data;
    osPointerLen_t* name = &leData->name;

    if(osPL_casecmp(name, (osPointerLen_t*) arg) == 0)
    {
        status = true;
    }

EXIT:
	mDEBUG_END(LM_SIPP);
    return status;
}


static void printTokenInfo(int abnfNum, sipParsingInfo_t sippParsingInfo[])
{
    for(int i=0; i<abnfNum; i++)
    {
        if(sippParsingInfo[i].tokenNum == 0)
        {
            mdebug(LM_SIPP, "sippParsingInfo idx=%d, tokenNum=0, extTokenNum=%d", i, sippParsingInfo[i].extTokenNum);
            continue;
        }

        for(int k=0; k<sippParsingInfo[i].tokenNum; k++)
        {
            mdebug(LM_SIPP, "sippParsingInfo idx=%d, tokenNum=%d, extTokenNum=%d, token[%d]='%c'(0x%x)", i, sippParsingInfo[i].tokenNum, sippParsingInfo[i].extTokenNum, k, sippParsingInfo[i].token[k], sippParsingInfo[i].token[k]);
        }
    }
}

/*
void sipParsing_setExtToken(sipParsingABNF_t sipABNF[], int prevMandatory, int lastMandatory, int abnfIdx, int abnfBypass, sipParsingInfo_t sippParsingInfo[])
{
    // set up the previous parameter's external token for the current parameter
    if(abnfIdx >=abnfBypass+1 && sipABNF[abnfIdx].extToken != SIP_TOKEN_INVALID)
    {
        sippParsingInfo[abnfIdx-1-abnfBypass].token[sippParsingInfo[anbfIdx-1-abnfBypass].tokenNum++] = sipABN[abnfIdx].extToken;
        sippParsingInfo[abnfIdx-1-abnfBypass].tokenNumForNextParam++;
    }

    //add external tokens for optional parameters in the middle
    for(int j=prevMandatory; j<lastMandatory; j++)
    {
        for(int k=j+2; k <= lastMandatory; k++)
        {
            if(sipABNF[k].extToken != SIP_TOKEN_INVALID)
            {
                sippParsingInfo[j-abnfBypass].token[sippParsingInfo[j-abnfBypass].tokenNum++] = sipABNF[k].extToken;
                sippParsingInfo[j-abnfBypass].tokenNumForNextParam++;
            }
        }
    }
}


void sipParsing_setFuncExtToken(int abnfNum, int abnfBypass, int lastMandatory, char token[], uint8_t tokenNum, sipParsingInfo_t sippParsingInfo[])
{
    //add external tokens from function call to the parameters after (include) the last mandatory parameter
    for(int i=lastMandatory; i<abnfNum; i++)
    {
        for(int j=0; j<tokenNum; j++)
        {
            sippParsingInfo[i-abnfBypass].token[sippParsingInfo[i-abnfBypass].tokenNum+j] = token[j];
        }
        sippParsingInfo[i-abnfBypass].tokenNum += tokenNum;
        sippParsingInfo[i-abnfBypass].tokenNumForNextParam += tokenNum;
    }
}

*/	
