#include "sipTUIntf.h"


static sipTUAppOnSipMsg_h sipTU_appOnMsg;

osStatus_e sipTU_onMsg(sipTUMsgType_e msgType, sipTUMsg_t* pMsg)
{
	return sipTU_appOnMsg(msgType, pMsg);
}


void sipTU_attach(sipTUAppOnSipMsg_h sipAppOnMsg)
{
	sipTU_appOnMsg = sipAppOnMsg;
}
