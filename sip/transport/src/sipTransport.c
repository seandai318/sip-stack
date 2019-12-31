#include "osDebug.h"

#include "sipMsgFirstLine.h"
#include "sipTransMgr.h"

osStatus_e sipTransport_send(sipMsgType_e msgType, sipTransaction_t* pTrans)
{
	DEBUG_BEGIN

	switch(msgType)
	{
		case SIP_MSG_REQUEST:
		{
            osPointerLen_t msg = {pTrans->req.pSipMsg->buf, pTrans->resp.pSipMsg->end};
            debug("sip request, msg=\n%r", &msg);
			break;
        }	
		case SIP_MSG_RESPONSE:
		{
			osPointerLen_t msg = {pTrans->resp.pSipMsg->buf, pTrans->resp.pSipMsg->end};
			debug("sip response, msg=\n%r", &msg);
			break;
		}			
		default:
			logError("msgType (%d) is unexpected.", msgType);
			break;
	}

	DEBUG_END
	return OS_STATUS_OK;
}
	
