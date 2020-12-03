#include "sipTUIntf.h"

#include "cscfConfig.h"


void cscf_init(char* cscfConfigFolder)
{
    cscfConfig_init(cscfConfigFolder);

	icscf_init(ICSCF_HASH_SIZE);
    scscf_init(SCSCF_HASH_SIZE);

    sipTU_attach(SIPTU_APP_TYPE_CSCF, cscf_onTUMsg);
}


osStatus_e cscf_onTUReqMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	if(pMsg->sipMsgType != SIP_MSG_REQUEST)
	{
		logError("received no request message.");
		return OS_ERROR_INVALID_VALUE;
	}

	sipTUAppOnSipMsg_h appOnSipMsg = scscfReg_onTUMsg;

	if(cscf_isS(pSipTUMsg->pLocal))
	{
		if(pMsg->sipMsgBuf.reqCode == SIP_METHOD_REGISTER)
		{
			appOnSipMsg = scscfReg_onTUMsg;
		}
		else
		{
			appOnSipMsg = scscfSess_onMsg;
		}
	}
	else
	{
		appOnSipMsg = icscf_onTUMsg;
	}

	return appOnSipMsg(msgType, pSipTUMsg);
}
