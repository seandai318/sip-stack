/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipUserinfo.c
 ********************************************************/

#include "osDebug.h"

#include "sipParsing.h"
#include "sipUserinfo.h"


//static osStatus_e sipParser_user(osMBuf_t* pSipMsg, size_t hdrEndPos, char token[], uint8_t tokenNum, sipParsingStatus_t* pStatus,  void* pUser);
//static osStatus_e sipParser_password(osMBuf_t* pSipMsg, size_t hdrEndPos, char token[], uint8_t tokenNum, sipParsingStatus_t* pStatus,  void* arg);

static sipParsingABNF_t sipUserinfoABNF[]={	\
	{1, 1, SIP_TOKEN_INVALID,   0, SIPP_PARAM_USER,     sipParsing_plGetParam, NULL}, \
	{0, 1, ':', 				0, SIPP_PARAM_PASSWORD, sipParsing_plGetParam, NULL}};


static osStatus_e sipParsing_setUserinfoParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg);
static int sipP_userinfoNum = sizeof(sipUserinfoABNF) / sizeof(sipParsingABNF_t);



osStatus_e sipParser_userinfo(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParentParsingInfo, sipParsingStatus_t* pParsingStatus)
{
	DEBUG_BEGIN

	osStatus_e status = OS_STATUS_OK;
    sipParsingInfo_t sippParsingInfo[sipP_userinfoNum];
    SIP_INIT_PARSINGINFO(sippParsingInfo, sipP_userinfoNum);

	if(!pSipMsg || !pParsingStatus || !pParentParsingInfo)
	{
		logError("NULL pointer is passed in, pSipMsg=%p, pParsingStatus=%p, pParentParsingInfo=%p.", pSipMsg, pParsingStatus, pParentParsingInfo);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    sipParsing_setParsingInfo(sipUserinfoABNF, sipP_userinfoNum, pParentParsingInfo, sippParsingInfo, sipParsing_setUserinfoParsingInfo);

    status = sipParsing_getHdrValue(pSipMsg, hdrEndPos, sipUserinfoABNF, sippParsingInfo, sipP_userinfoNum, pParsingStatus);

EXIT:
	DEBUG_END
    return status;
}


void sipUserinfo_cleanup(void* arg)
{
	if(!arg)
	{
		return;
	}

	sipUserinfo_t* pUserinfo = (sipUserinfo_t*) arg;

	pUserinfo->sipUser.user.l = 0;
	pUserinfo->sipUser.user.p = NULL;
	pUserinfo->password.l = 0;
	pUserinfo->password.p = NULL;
}


static osStatus_e sipParsing_setUserinfoParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg)
{
    osStatus_e status = OS_STATUS_OK;

    sipUserinfo_t* pUserinfo = (sipUserinfo_t*) arg;

	pUserinfo->sipUser.user.l = 0;

	switch (paramName)
  	{
        case SIPP_PARAM_USER:
            pSippParsingInfo->arg = &pUserinfo->sipUser.user;
			//for now assume all user is a normal sip user, not telephone subscriber
			pUserinfo->sipUser.isTelSub = false;
			pUserinfo->sipUser.user.l = 0;
            break;
				
		case SIPP_PARAM_PASSWORD:
            pSippParsingInfo->arg = &pUserinfo->password;
			pUserinfo->password.l = 0;
            break;

        default:
            logError("unexpected parameter, sipUserinfoABNF paramName=%s.", paramName);
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
    }

EXIT:
    return status;
}



