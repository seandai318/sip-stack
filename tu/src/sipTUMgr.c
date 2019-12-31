
typedef struct sipTUMsg {
	sipRequest_e reqCode;
	osListElement_t* pTransId;
	osListElement_t* pTUId;
	sipMsgBuf_t sipMsgBuf;
} sipTUMsg_t;
		

//this function only handles messages from sipTrans.  For other messages, like timeout, will be directly handled by each TU sub modules 
osStatus_e sipTU_onMsg(sipTUMsg_t* pSipTUMsg)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(pSipTUMsg->pTUId ==  NULL && !pSipTUMsg->sipMsgBuf.isRequest)
	{
		logError("received a response message without TUid.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}
		
	sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(pSipTUMsg->sipMsgBuf, NULL, 0);
	if(pReqDecodedRaw == NULL)
	{
		logError("fails to sipDecodeMsgRawHdr.");
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	switch(pSipTUMsg->sipMsgBuf->reqCode)
	{
		case SIP_METHOD_REGISTER:
			sipRegistrar_onMsg(pSipTUMsg, pReqDecodedRaw);
			break;
		case SIP_METHOD_MESSAGE:
			sipMessage_onMsg(pSipTUMsg, pReqDecodedRaw);
			break;
		case SIP_METHOD_INVITE:
		case SIP_METHOD_BYE:
		case SIP_METHOD_ACK:
		case SIP_METHOD_CANCEL:
			sipInvite_onMsg(pSipTUMsg, pReqDecodedRaw);
			break;
		default:
			logInfo("reqCode=%d, to-do.", pSipTUMsg->sipMsgBuf->reqCode);
			break;
	}

	sipSessionInfo_t sessionInfo;
	status = sipTU_decodeSessionInfo(pReqDecodedRaw, &sessionInfo);
	
