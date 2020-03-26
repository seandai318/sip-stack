#include <pthread.h>
#include <signal.h>

#include "osList.h"
#include "osDebug.h"
#include "osPL.h"
#include "osTimer.h"
#include "osTimerModule.h"
#include "osPreMemory.h"

#include "sipRegistrar.h"
#include "sipSMS.h"
#include "masConfig.h"
#include "sipTransIntf.h"
#include "sipTransportServer.h"
#include "sipTransportClient.h"
#include "masMgr.h"
#include "masDb.h"


static osStatus_e masMgr_onSipMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);


void masMgr_init()
{
    sipTU_attach(masMgr_onSipMsg);

	sipRegAction_t masRegActionData = {masRegAction, NULL};
	sipReg_attach(masSMS_matchHandler, &masRegActionData);
}

//this function only handles messages from sipTrans.  For other messages, like timeout, will be directly handled by each TU sub modules 
static osStatus_e masMgr_onSipMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	DEBUG_BEGIN
	osStatus_e status = OS_STATUS_OK;

	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(pSipTUMsg->pTUId ==  NULL && pSipTUMsg->sipMsgType != SIP_MSG_REQUEST)
	{
		logError("received a response message without TUid.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}
		
	switch(pSipTUMsg->pSipMsgBuf->reqCode)
	{
		case SIP_METHOD_REGISTER:
			masReg_onTUMsg(msgType, pSipTUMsg);
			break;
		case SIP_METHOD_MESSAGE:
		default:
			masSMS_onSipTUMsg(msgType, pSipTUMsg);
			break;
	}

EXIT:
	DEBUG_END
	return status;
}


bool masSMS_matchHandler(osListElement_t *le, void *arg)
{
	if(!le || !arg)
	{
		return false;
	}

	masInfo_t* pInfo = le->data;
	if(!pInfo)
	{
		return false;
	}

	sipTransId_t* trId = arg;

	if(osPL_cmp(&((sipTransId_t*)arg)->viaId.branchId, &((sipTransId_t*)pInfo->pSrcTransId)->viaId.branchId) == 0)
	{
		return true;
	}
//for now masInfo_t only stores the srcTransId, the dstTransId is not stored.
#if 0
	else if(osPL_cmp(&((sipTransId_t*)arg)->viaId.branchId, &((sipTransId_t*)pInfo->pDstTransId)->viaId.branchId) == 0)
	{
		return true;
	}
#endif
	return false;
}


void* masInfo_getTransId(masInfo_t* pMasInfo, void* pTransId, bool isSrcTransId)
{
    if(!pMasInfo)
    {
        return NULL;
    }

    if((isSrcTransId && pMasInfo->pSrcTransId == pTransId))
    {
        return pMasInfo->pDstTransId;
    }
    else if (!isSrcTransId && pMasInfo->pDstTransId != pTransId)
    {
        return pMasInfo->pSrcTransId;
    }

    return NULL;
}



void masInfo_cleanup(void *data)
{
	if(!data)
	{
		return;
	}

	masInfo_t* pMasInfo = data;
	osDPL_dealloc(&pMasInfo->uacData.user);
	osDPL_dealloc(&pMasInfo->uacData.caller);
	osDPL_dealloc(&pMasInfo->uacData.sms);
}


void masMgr_threadStartup(void* pData)
{
	masDbInit("demo");

	//to-do, when app has multiple threads, differentiate each thread with a unique id, the id will be stored in db, so that only thread that stored a sms will be timeout for that sms
}


void masRegAction(osPointerLen_t* user, void* pAppData)
{
	masDbQuerySMSByUser(user);
}

