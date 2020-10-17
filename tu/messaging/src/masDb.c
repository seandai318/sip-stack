/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file masDb.c
 ********************************************************/

#include <my_global.h>
#include <mysql.h>
#include <string.h>
#include <stdlib.h>

#include "osDebug.h"
#include "osPrintf.h"
#include "osTimer.h"
#include "osPL.h"
#include "osSockAddr.h"

#include "sipConfig.h"
#include "sipMsgRequest.h"
#include "sipTU.h"
#include "sipRegistrar.h"
#include "masSipHelper.h"
#include "masMgr.h"
#include "masDb.h"


#define MASDB_SMS_QUERY_SMSID		0
#define MASDB_SMS_QUERY_CONTENT		1
#define MASDB_SMS_QUERY_USER		2
#define MASDB_SMS_QUERY_CALLER		3
#define MASDB_SMS_QUERY_TIMERN		4
#define MASDB_SMS_QUERY_EXPIRE		5
#define MASDB_SMS_QUERY_DROPTIME	6

#define MAS_DB_QUERY_LEN	200


static __thread MYSQL* dbHandler;

static osStatus_e masDbSmsHandler(const MYSQL_ROW pRow, bool* isContinue, bool* isStore);
static void masDbOnTimeout(uint64_t timerId, void* ptr);
static osStatus_e masDbUpdateSms(size_t smsId, uint32_t oldTimerN, size_t oldExpiry, size_t dropTime);


osStatus_e masDbInit(char* dbName)
{
	osStatus_e status = OS_STATUS_OK;

    dbHandler = mysql_init(NULL);
	if(!dbHandler)
  	{
		logError("fails to init mysql, error=%s.", mysql_error(dbHandler));
		status = OS_ERROR_SYSTEM_FAILURE;
		goto EXIT;
  	}

	if(mysql_real_connect(dbHandler, "localhost", "root", "mysql", dbName, 0, NULL, 0) == NULL)
  	{
		logError("fails to connect to DB %s, error=%s.", dbName, mysql_error(dbHandler));
      	mysql_close(dbHandler);
      	status = OS_ERROR_SYSTEM_FAILURE;
		goto EXIT;
    }

	osStartTick(MAS_DB_POLL_TIME, masDbOnTimeout, NULL);

EXIT:
	return status;
}


osStatus_e masDbQuerySMSByUser(osPointerLen_t* user)
{
	osStatus_e status =OS_STATUS_OK;

	if(!user)
	{
		logError("null pointer, user=%p.", user);
		return OS_ERROR_NULL_POINTER;
	}

	if(user->l > SIP_DB_USERNAME_SIZE)
	{
		logError("user size(%d) exceeds SIP_DB_USERNAME_SIZE(%d).", user->l, SIP_DB_USERNAME_SIZE);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

    char query[MAS_DB_QUERY_LEN];
	int len = osPrintf_buffer(query, MAS_DB_QUERY_LEN, "select smsId, content, username, caller from user inner join sms using (userId) where userName ='%r'", user);
	query[len] = 0;
logError("to-remove, sms, query=%s", query);

	if(mysql_query(dbHandler, query))
	{
		logError("fails to mysql_query, error=%s.", mysql_error(dbHandler));
		status = OS_ERROR_SYSTEM_FAILURE;
		goto EXIT;
	}

	MYSQL_RES *result = mysql_store_result(dbHandler);
    if (result == NULL)
    {
        goto EXIT;
  	}

	//for now assume the stored SMS is not much. need to do throttle when the traffic is big to-do
	bool isContinue = true;
    bool isStore = true;
	MYSQL_ROW row;
	while((row = mysql_fetch_row(result)) && isContinue)
	{
logError("to-remove, sms, smsId=%s, content=%s, username=%s, caller=%s", row[0], row[1], row[2], row[3]);
		masDbSmsHandler(row, &isContinue, &isStore);
		if(!isStore)
		{
			//remove the entry
			masDbDeleteSms(strtoul(row[MASDB_SMS_QUERY_SMSID], NULL, 10));
		}
	}

	mysql_free_result(result);

EXIT:
	return status;
}


void masDbOnTimeout(uint64_t timerId, void* ptr)
{
	osStatus_e status = OS_STATUS_OK;
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);

    char query[MAS_DB_QUERY_LEN]={};
	sprintf(query, "select smsId, content, username, caller, timerN, expire, dropTime from sms inner join user using (userId) where expire <=%ld", tp.tv_sec);

    if(mysql_query(dbHandler, query))
    {
        logError("fails to mysql_query, error=%s.", mysql_error(dbHandler));
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

    MYSQL_RES *result = mysql_store_result(dbHandler);
    if (result == NULL || mysql_num_rows(result) <= 0)
    {
    	mysql_free_result(result);
        goto EXIT;
    }

    //for now assume the stored SMS is not many. need to do throttle when the traffic is big. to-do
    bool isContinue = true;
	bool isStore = true;
    MYSQL_ROW row;
    while((row = mysql_fetch_row(result)) && isContinue)
    {
		logInfo("fetch a SMS from DB, smsId=%s, sms=\n%s", row[MASDB_SMS_QUERY_SMSID], row[MASDB_SMS_QUERY_CONTENT]);
 
		masDbSmsHandler(row, &isContinue, &isStore);
		//if immediately knows SMS is not delivered, restore the SMS
		if(isStore)
		{
			uint32_t nMul = atoi(row[MASDB_SMS_QUERY_TIMERN]);
			size_t expiry = strtoul(row[MASDB_SMS_QUERY_EXPIRE], NULL, 10);
			size_t dropTime = strtoul(row[MASDB_SMS_QUERY_DROPTIME], NULL, 10);

			status = masDbUpdateSms(strtoul(row[0], NULL, 10), nMul, expiry, dropTime);
		}
	}

    mysql_free_result(result);
EXIT:
    return;
}


//if userId= -1, get the userId first
osStatus_e masDbStoreSms(ssize_t userId, osPointerLen_t* user, osPointerLen_t* caller, osPointerLen_t* sms)
{
	osStatus_e status = OS_STATUS_OK;

	if(!user || !caller || !sms)
	{
		logError("null pointer, user=%p, caller=%p, sms=%p.", user, caller, sms);
		status = OS_ERROR_NULL_POINTER;
		return status;
	}

	if(userId == -1)
	{
		status = masDbSetUser(user, &userId);
		if(status != OS_STATUS_OK)
		{
			logError("fails to set user for %r", user);
			goto EXIT;
		}
	}

    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);

	char query[MAS_DB_QUERY_LEN];
	size_t expire = tp.tv_sec + MAS_DB_SMS_TIMER_T1;
	size_t dropTime = tp.tv_sec + MAS_DB_SMS_MAX_TIMER_M;
	int len = osPrintf_buffer(query, MAS_DB_QUERY_LEN, "insert into sms (content, caller, userId, timerN, expire, dropTime) values ('%r', '%r', %ld, %d, %lu, %lu)", sms, caller, userId, MAS_DB_SMS_TIMER_T1, expire, dropTime);
    if(len < 0)
    {
        logError("fails to create a mysql insert query for user(%r) from(%r), userId=%ld", user, caller, userId);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
    query[len] = 0;

	if(mysql_query(dbHandler, query))
    {
        logError("fails to mysql_query, error=%s.", mysql_error(dbHandler));
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

#ifdef DEBUG
	len = osPrintf_buffer(query, MAS_DB_QUERY_LEN, "select smsId from sms where userId=%ld and expire=%lu", userId, expire);
    if(len >= 0)
    {
    	query[len] = 0;

    	if(mysql_query(dbHandler, query) == 0)
    	{
    		MYSQL_RES *result = mysql_store_result(dbHandler);
    		if (result != NULL && mysql_num_rows(result) > 0)
    		{
    			MYSQL_ROW row;
    			if((row = mysql_fetch_row(result)))
    			{
					logInfo("store a SMS (from: %r to: %r) into DB, smsId=%s, sms=\n%r", caller, user, row[0], sms);
					mysql_free_result(result);
					goto EXIT;
				}
				else
				{
					mysql_free_result(result);
				}
			}
		}
	}
#endif

    logInfo("store a SMS (from: %r to: %r) into DB, userId=%ld, sms=\n%r", caller, user, userId, sms);

EXIT:
    return status;
}


osStatus_e masDbDeleteSms(size_t smsId)
{
    osStatus_e status = OS_STATUS_OK;

    char query[MAS_DB_QUERY_LEN];
    int len = snprintf(query, MAS_DB_QUERY_LEN, "delete from sms where smsId=%lu", smsId);
    if(len < 0)
    {
        logError("fails to create a mysql delete query for smsId(%lu)", smsId);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
    query[len] = 0;

	if(mysql_query(dbHandler, query))
    {
        logError("fails to mysql_query, error=%s.", mysql_error(dbHandler));
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

EXIT:
    return status;
}


osStatus_e masDbUpdateSms(size_t smsId, uint32_t oldTimerN, size_t oldExpiry, size_t dropTime)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    size_t expiry = oldExpiry + oldTimerN;
    if(expiry >= dropTime)
    {
        masDbDeleteSms(smsId);
        goto EXIT;
    }

	uint32_t timerN = oldTimerN;
    if(timerN < MAS_DB_SMS_TIMER_T2)
    {
    	timerN *= 2;
    	if(timerN >= MAS_DB_SMS_TIMER_T2)
        {
        	timerN = MAS_DB_SMS_TIMER_T2;
        }
    }

    char query[MAS_DB_QUERY_LEN];
    int len = snprintf(query, MAS_DB_QUERY_LEN, "update sms set timerN=%u, expire=%lu where smsId=%lu", timerN, expiry, smsId);
    if(len < 0)
    {
        logError("fails to create a mysql update for smsId(%lu)", smsId);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
    query[len] = 0;

logError("to-remove, query=%s", query);
    if(mysql_query(dbHandler, query))
    {
        logError("fails to mysql_query, error=%s.", mysql_error(dbHandler));
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

EXIT:
	DEBUG_END;
    return status;
}


//first check if the user is already in the table, if not, store it.
osStatus_e masDbSetUser(osPointerLen_t* user, size_t* userId)
{
	osStatus_e status = OS_STATUS_OK;

	if(!user)
	{
		logError("null pointer, user.");
		return OS_ERROR_NULL_POINTER;
	}

    if(user->l > SIP_DB_USERNAME_SIZE)
    {
        logError("user size(%d) exceeds SIP_DB_USERNAME_SIZE(%d).", user->l, SIP_DB_USERNAME_SIZE);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	if(masDbGetUserId(user, userId) != OS_STATUS_OK)
	{
    	char query[MAS_DB_QUERY_LEN];
    	int len = osPrintf_buffer(query, MAS_DB_QUERY_LEN, "insert into user (username) values('%r')", user);
    	if(len < 0)
    	{
        	logError("fails to create insert query for user(%r).", user);
        	status = OS_ERROR_INVALID_VALUE;
        	goto EXIT;
    	}

    	if(mysql_query(dbHandler, query))
    	{
        	logError("fails to mysql_query, error=%s.", mysql_error(dbHandler));
        	status = OS_ERROR_SYSTEM_FAILURE;
        	goto EXIT;
    	}

		status = masDbGetUserId(user, userId);
	}

EXIT:
	return status;
}


osStatus_e masDbGetUserId(osPointerLen_t* user, size_t* userId)
{
    osStatus_e status = OS_STATUS_OK;

    if(!user)
    {
        logError("null pointer, user.");
        return OS_ERROR_NULL_POINTER;
    }

    if(user->l > SIP_DB_USERNAME_SIZE)
    {
        logError("user size(%d) exceeds SIP_DB_USERNAME_SIZE(%d).", user->l, SIP_DB_USERNAME_SIZE);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    char query[MAS_DB_QUERY_LEN];
    int len = osPrintf_buffer(query, MAS_DB_QUERY_LEN, "select userId from user where username='%r'", user);
    if(len < 0)
    {
        logError("fails to create select query for user(%r).", user);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    if(mysql_query(dbHandler, query))
    {
        logError("fails to mysql_query, error=%s.", mysql_error(dbHandler));
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

    MYSQL_RES *result = mysql_store_result(dbHandler);
    if (result == NULL)
    {
        logError("mysql_store_result returns NULL.");
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

    MYSQL_ROW row;
    if(mysql_num_rows(result) > 0)
    {
        row = mysql_fetch_row(result);
        *userId =strtoul(row[0], NULL, 10);
    }
	else
	{
		status = OS_ERROR_INVALID_VALUE;
	}

    mysql_free_result(result);

EXIT:
	return status;
}	


static osStatus_e masDbSmsHandler(const MYSQL_ROW pRow, bool* isContinue, bool* isStore)
{
	DEBUG_BEGIN

	osStatus_e status = OS_STATUS_OK;
	*isContinue = true;
	*isStore = true;
	
	//first, check if user is registered
	osPointerLen_t userPL = {pRow[MASDB_SMS_QUERY_USER], strlen(pRow[MASDB_SMS_QUERY_USER])};
logError("to-remove, sms, userPL=%r", &userPL);
	tuRegState_e regState;
	sipUri_t* pCalledContactUser = masReg_getUserRegInfo(&userPL, &regState);
	if(!pCalledContactUser)
	{
		debug("user(%s) is not registered.", pRow[MASDB_SMS_QUERY_USER]);
		
		*isContinue = true;
		*isStore = true;
		status = OS_ERROR_INVALID_VALUE;	
		goto EXIT;
	}

	if(regState != MAS_REGSTATE_REGISTERED)
	{
        debug("user(%s) is de-registered.", pRow[MASDB_SMS_QUERY_USER]);

        *isContinue = true;
        *isStore = true;
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	//build a SIP request, the related DB info will be saved as part of the opaque TU data
	//smsId, content, user, caller, timerN, expire
    osPointerLen_t caller = {pRow[MASDB_SMS_QUERY_CALLER], strlen(pRow[MASDB_SMS_QUERY_CALLER])};
    sipTransInfo_t sipTransInfo;
	size_t protocolUpdatePos;
	osPointerLen_t sms = {pRow[MASDB_SMS_QUERY_CONTENT], strlen(pRow[MASDB_SMS_QUERY_CONTENT])};
	osMBuf_t* pSipBuf = masSip_buildRequest(&userPL, &caller, pCalledContactUser, &sms, &sipTransInfo.transId.viaId, &protocolUpdatePos);

    masInfo_t* pMasInfo = osmalloc(sizeof(masInfo_t), masInfo_cleanup);
	if(!pMasInfo)
    {
        logError("fails to allocate memory for uacData.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    pMasInfo->pSrcTransId = NULL;
    pMasInfo->smsType = MAS_SMS_TYPE_DB;
	pMasInfo->uacData.dbSmsId = strtoul(pRow[MASDB_SMS_QUERY_SMSID], NULL, 10);
	osPL_init((osPointerLen_t*) &pMasInfo->uacData.user);
	osPL_init((osPointerLen_t*) &pMasInfo->uacData.caller);
	osPL_init((osPointerLen_t*) &pMasInfo->uacData.sms);
    pMasInfo->regId = masReg_addAppInfo(&userPL, pMasInfo);
    if(!pMasInfo->regId)
    {
		logError("the called user(%r) is not registered.", &userPL);
		osfree(pMasInfo);
		osMBuf_dealloc(pSipBuf);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
	}

	logInfo("SIP Request Message=\n%M", pSipBuf);

    sipTransMsg_t sipTransMsg;
    sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_REQUEST;
	sipTransMsg.isTpDirect = false;
	sipTransMsg.appType = SIPTU_APP_TYPE_MAS;

    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.pSipMsg = pSipBuf;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.reqCode = SIP_METHOD_MESSAGE;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.isRequest = true;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.hdrStartPos = 0;

    sipTransInfo.isRequest = true;
    sipTransInfo.transId.reqCode = SIP_METHOD_MESSAGE;
    sipTransMsg.request.pTransInfo = &sipTransInfo;
	osIpPort_t osPeer = {{pCalledContactUser->hostport.host, false, false}, pCalledContactUser->hostport.portValue};
	osConvertPLton(&osPeer, true, &sipTransMsg.request.sipTrMsgBuf.tpInfo.peer);
	sipConfig_getHost1(&sipTransMsg.request.sipTrMsgBuf.tpInfo.local);
    sipTransMsg.request.sipTrMsgBuf.tpInfo.protocolUpdatePos = protocolUpdatePos;
    sipTransMsg.pTransId = NULL;
    sipTransMsg.pSenderId = pMasInfo;

    status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

EXIT:
	DEBUG_END;
	return status;
}
