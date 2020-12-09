#include "sipTUIntf.h"

#include "cscfConfig.h"
#include "scscfIntf.h"


static osStatus_e cscf_onTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);


void cscf_init(char* cscfConfigFolder, char* cxXsdFileName)
{
    cscfConfig_init(cscfConfigFolder, cxXsdFileName);

//	icscf_init(ICSCF_HASH_SIZE);
    scscfReg_init(SCSCF_HASH_SIZE);

    sipTU_attach(SIPTU_APP_TYPE_CSCF, cscf_onTUMsg);
}


static osStatus_e cscf_onTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	if(pSipTUMsg->sipMsgType != SIP_MSG_REQUEST)
	{
		logError("received no request message.");
		return OS_ERROR_INVALID_VALUE;
	}

	sipTUAppOnSipMsg_h appOnSipMsg = scscfReg_onTUMsg;

	if(cscf_isS(pSipTUMsg->pLocal))
	{
		if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_REGISTER)
		{
			appOnSipMsg = scscfReg_onTUMsg;
		}
		else
		{
		//	appOnSipMsg = scscfSess_onMsg;
			logError("to-remove, to be done.");
		}
	}
	else
	{
		//appOnSipMsg = icscf_onTUMsg;
		logError("to-remove, to be done.");
	}

	return appOnSipMsg(msgType, pSipTUMsg);
}
