/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file masSipHelper.c
 ********************************************************/

#include <stdio.h>
#include <time.h>

#include "osDebug.h"
#include "osMemory.h"
#include "osMisc.h"
#include "osPrintf.h"

#include "sipMsgRequest.h"

#include "sipTransIntf.h"
#include "sipTU.h"
#include "masConfig.h"
#include "masSipHelper.h"


osStatus_e masSip_buildContent(osPointerLen_t* sms, osPointerLen_t* caller, osPointerLen_t* called, bool isNewline, osDPointerLen_t* content)
{
	osStatus_e status = OS_STATUS_OK;

    content->p = osmalloc(MAS_MAX_SMS_CONTENT_SIZE, NULL);
    if(!content->p)
    {
        logError("fails to osmalloc for size (%d)", MAS_MAX_SMS_CONTENT_SIZE);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

	time_t t = time(&t);
	struct tm tm;
	if(gmtime_r(&t, &tm) == NULL)
	{
		logError("fails to gmtime_r.");
		status = OS_ERROR_SYSTEM_FAILURE;
		goto EXIT;
	}

	ssize_t len;
	if(isNewline)
	{
		len = osPrintf_buffer(content->p, MAS_MAX_SMS_CONTENT_SIZE, "\r\nTo: <%r>\r\nFrom: <%r>\r\nDateTime: %d-%d-%dT%d:%d:%d.000Z\r\n\r\nContent-Type: text/plain;charset=utf-8\r\n\r\n%r", called, caller, tm.tm_year+1900, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, sms);
	}
	else
	{
        len = osPrintf_buffer(content->p, MAS_MAX_SMS_CONTENT_SIZE, "To: <%r>\r\nFrom: <%r>\r\nDateTime: %d-%d-%dT%d:%d:%d.000Z\r\n\r\nContent-Type: text/plain;charset=utf-8\r\n\r\n%r", called, caller, tm.tm_year+1900, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, sms);
    }

	if(len < 0)
	{
		logError("fails to create a sms content for sms(%r).");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	content->l = len;

EXIT:
	if(status != OS_STATUS_OK)
	{
		osDPL_dealloc(content);
	}

	return status;	
}


osStatus_e masSip_getSms(sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pSms)
{
	DEBUG_BEGIN
	osStatus_e status = OS_STATUS_OK;

	if(!pReqDecodedRaw || !pSms)
	{
		logError("null pointer, pReqDecodedRaw=%p, pSms=%p.", pReqDecodedRaw, pSms);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	pReqDecodedRaw->msgContent.pos = 0;
	osPointerLen_t clHdr = {"Content-Length", 14};
	ssize_t pos = osMbuf_findMatch(&pReqDecodedRaw->msgContent, &clHdr);
	if(pos <0)
	{
		logError("fails to find Content-Length in the SIP MESSAGE message body.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	osPointerLen_t clValue={};
	pos = osMbuf_findValue(&pReqDecodedRaw->msgContent, ':', '\r', true, &clValue);
    if(pos <0)
    {
        logError("fails to find Content-Length value in the SIP MESSAGE message body.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	int smsLen=0;
	status = osStr2Int((char*)clValue.p, clValue.l, &smsLen);
	if(status != OS_STATUS_OK)
	{
		logError("fails to convert str to int for (%r).", &clValue);
		goto EXIT;
	}

	//reset the pos to the beginning of content-length value
    pReqDecodedRaw->msgContent.pos = pos;
    osPointerLen_t smsTag = {"\r\n\r\n", 4};
    pos = osMbuf_findMatch(&pReqDecodedRaw->msgContent, &smsTag);
    if(pos <0)
    {
        logError("fails to find SMS in the SIP MESSAGE message body.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	pSms->p = &pReqDecodedRaw->msgContent.buf[pos+4];	
	pSms->l = pReqDecodedRaw->msgContent.end - (pos + 4);
	if(smsLen != pSms->l)
	{
		logError("the length of SMS(%d) does not match with the Content-Length value(%d).", pSms->l, smsLen);
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

EXIT:
	DEBUG_END
	return status;
}


osMBuf_t* masSip_buildRequest(osPointerLen_t* user, osPointerLen_t* caller, sipUri_t* pCalledContactUser, osPointerLen_t* sms, sipTransViaInfo_t* pTransViaId, size_t* pViaProtocolPos)
{
	osStatus_e status = OS_STATUS_OK;

	if(!user || !caller || !pCalledContactUser || !pTransViaId || !pViaProtocolPos || !sms)
	{
		logError("null pointer, user=%p, caller=%p, pCalledContactUser=%p, sms=%p, pTransViaId=%p, pViaProtocolPos=%p", user, caller, pCalledContactUser, sms, pTransViaId, pViaProtocolPos);
		return NULL;
	}

	osDPointerLen_t content={};
    osMBuf_t* pSipBuf = sipTU_uacBuildRequest(SIP_METHOD_MESSAGE, pCalledContactUser, user, caller, pTransViaId, pViaProtocolPos);
    if(!pSipBuf)
    {
        logError("fails to sipTU_uacBuildRequest for a SMS from(%r) to(%r).", caller, user);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    //now add other headers. add cSeq, p-asserted-id, content-type
    osPointerLen_t cSeqHdr = {"1 MESSAGE", 9};
    status = sipMsgAppendHdrStr(pSipBuf, "Cseq", &cSeqHdr, 0);

    status = sipMsgAppendHdrStr(pSipBuf, "P-Asserted-Identity", caller, 0);

    osPointerLen_t cType = {"message/cpim", 12};
    status = sipMsgAppendHdrStr(pSipBuf, "Content-Type", &cType, 0);

    status = masSip_buildContent(sms, caller, user, false, &content);
    if(status != OS_STATUS_OK)
    {
        logError("fails to masBuildSipContent.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    status = sipMsgAppendHdrStr(pSipBuf, "Content-Length", NULL, content.l);

    //append message body
    status = sipMsgAppendContent(pSipBuf, (osPointerLen_t*)&content, true);

EXIT:
	if(status != OS_STATUS_OK)
	{
		osfree(pSipBuf);
		pSipBuf = NULL;
	}

	osDPL_dealloc(&content);
	return pSipBuf;
}
