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
#include "masDb.h"


static osStatus_e masMgr_onSipMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static bool masSMS_matchHandler(osListElement_t *le, void *arg);
//to be called when threads that application uses are started. 
static void masMgr_threadStartup(void* pData);

int main(int argc, char* argv[])
{
	int timerWriteFd;
	int tpServerPipefd[MAS_CONFIG_LISTENER_THREAD_NUM][2];
	int tpClientPipefd[MAP_CONFIG_TRANSACTION_THREAD_NUM][2];

	osPreMem_init();

    osDbg_init(DBG_DEBUG, DBG_ALL);
	osDbg_mInit(LM_TIMER, DBG_INFO);
    osDbg_mInit(LM_TRANSPORT, DBG_DEBUG);

	osTimerModuleInit(&timerWriteFd);

	for(int i=0; i<MAS_CONFIG_LISTENER_THREAD_NUM; i++)
	{
		sipTransportServerInit(tpServerPipefd[i], SIP_CONFIG_LB_HASH_BUCKET_SIZE);
	}

	for(int i=0; i<MAP_CONFIG_TRANSACTION_THREAD_NUM; i++)
	{
		sipTransportClientInit(tpClientPipefd[i]);
	}

    masReg_init(MAS_REG_HASH_SIZE, masSMS_matchHandler);
    sipTU_attach(masMgr_onSipMsg);

    printf("5\n");

    //start threads
    pthread_t timerThread, sipTransThread[MAP_CONFIG_TRANSACTION_THREAD_NUM], sipListenThread[MAS_CONFIG_LISTENER_THREAD_NUM];

    if(pthread_create( &timerThread, NULL, osStartTimerModule, NULL) != 0)
    {
        logError("timer thread creation failed");
    }

    printf("6\n");

	//make sure the signal mask is after the creation of timerModule thread so that only timerModule thread will get the signal
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGRTMIN);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    sipTransportClientSetting_t clientSetting;
	clientSetting.timerfd = timerWriteFd;
	sipConfig_getHost(&clientSetting.local.ip, &clientSetting.local.port);
	clientSetting.appStartup = masMgr_threadStartup;
	clientSetting.appStartupData = NULL;
	for(int i=0; i<MAP_CONFIG_TRANSACTION_THREAD_NUM; i++)
	{
		clientSetting.ownIpcFd[0] = tpClientPipefd[i][0];
		clientSetting.ownIpcFd[1] = tpClientPipefd[i][1];
        if(pthread_create( &sipTransThread[i], NULL, sipTransportClientStart, (void*)&clientSetting) != 0)
        {
            logError("ip transaction thread creation failed");
        }
    }

    sipTransportServerSetting_t serverSetting;
	serverSetting.timerfd = timerWriteFd;
    sipConfig_getHost(&serverSetting.local.ip, &serverSetting.local.port);
	for(int i=0; i<MAP_CONFIG_TRANSACTION_THREAD_NUM; i++)
	{
		serverSetting.lbFd[i] = tpClientPipefd[i][1];
	}
    for(int i=0; i<MAS_CONFIG_LISTENER_THREAD_NUM; i++)
    {
		serverSetting.ownIpcFd[0] = tpServerPipefd[i][0];
		serverSetting.ownIpcFd[1] = tpServerPipefd[i][1];
        if(pthread_create( &sipListenThread[i], NULL, sipTransportServerStart, (void*)&serverSetting) != 0)
        {
            logError("sip listener thread creation failed");
        }
	}

    pthread_join(timerThread, NULL);

    for (int i=0; i < MAP_CONFIG_TRANSACTION_THREAD_NUM; i++)
    {
        pthread_join(sipTransThread[i], NULL);
		logError("transaction thread %d exited.", i);
    }

    for (int i=0; i < MAS_CONFIG_LISTENER_THREAD_NUM; i++)
    {
        pthread_join(sipListenThread[i], NULL);
    }

	return -1;
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
//for now masInfo_t only stores the srcTransId, the dstTransId is not stored.
#if 0
	else if(osPL_cmp(&((sipTransId_t*)arg)->viaId.branchId, &((sipTransId_t*)pInfo->pDstTransId)->viaId.branchId) == 0)
	{
		return true;
	}
#endif
	return false;
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
