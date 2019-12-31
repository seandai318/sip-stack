#include "osList.h"
#include "osDebug.h"
#include "osPL.h"

#include "sipRegistrar.h"
#include "sipSMS.h"
#include "masConfig.h"
#include "sipTransIntf.h"


static osStatus_e masMgr_onSipMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static bool masSMS_matchHandler(osListElement_t *le, void *arg);


void masInit()
{
	masReg_init(MAS_REG_HASH_SIZE, masSMS_matchHandler);
	
    sipTU_attach(masMgr_onSipMsg);
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
			masReg_onSipMsg(msgType, pSipTUMsg);
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


static bool masSMS_matchHandler(osListElement_t *le, void *arg)
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
	else if(osPL_cmp(&((sipTransId_t*)arg)->viaId.branchId, &((sipTransId_t*)pInfo->pDstTransId)->viaId.branchId) == 0)
	{
		return true;
	}

	return false;
}
