/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTcm.c
 ********************************************************/

#include <string.h>

#include "osDebug.h"

#include "tcm.h"
#include "sipTransportIntf.h"



/* This function delimits a TCP message for SIP. It identifies whether a pSipMsg contains a whole sip message or part of a
 * sip message.  The following is checked:
 * 1. if the pSipMsg contains the whole sip message.
 * 2. if the pSipMsg contains the whole sip message + extra bytes for the next sip message
 * 3. if the pSipMsg contains part of a sip message
 * 
 * For case 1 and 2, the pSipMsg can be sent to the up layter, but the extra bytes in the case 2 needs to be copied to the 
 * next pSipMsg (done in the caller of sipTpAnalyseMsg()).
 * 
 * The Content-Length is used to identify the length of a sip message to determine which of the above three cases the pSipMsg is 
 * in.  if a Content-Length does not exist, but the parse meets "\r\n\r\n", the end of a sip message is also reached.
 *
 * when this function returns, for case 1 & 2:
 * pSipMsgBuf->pos = 0;
 * pSipMsgBuf->end = end of the current sip message
 * for case 3:
 * pSipMsgBuf->pos = end of bytes processed.
 * pSipMsgBuf->end = end of bytes received.
 * if a sip packet does not contain Content-Length header, assume Content length = 0
 * pNextStart: if there is extra bytes for next sip message, the position in the buf that the extra bytes starts, otherwise, 0
 * return value: -1: expect more read() for the current sip packet, 0: exact sip packet, >1 extra bytes for next sip packet.
 */
ssize_t sipTpAnalyseMsg(osMBuf_t* pSipMsg, sipTpMsgState_t* pMsgState, size_t chunkLen, size_t* pNextStart)
{
    if(!pSipMsg || !pMsgState)
    {
        logError("null pointer, pSipMsg=%p, pMsgState=%p.", pSipMsg, pMsgState);
        return -1;
    }

    pSipMsg->end += chunkLen;

    ssize_t remainingBytes = -1;	//how many bytes are out of this sip message, equivlent to how many the next sip message are included in this received TCP message. if > 0, contains next sip message
    *pNextStart = 0;

    //first check if is waiting for content.  When clValue >=0, the sip message's content length has been identified in the last received TCP message 
	//this only happens when Content-Length is processed, but last TCP message stopped after RNRN (stopped in Content section).
	//for case that Content-Length is processed, but the last TCP message stopped before reaching RNRN, the processing of the last TCP message
	//has reset the clValue = -1, and pos to the beginning of Content-Length header.
    if(pMsgState->clValue > 0)
    {
        if(chunkLen >= pMsgState->clValue)
        {
            //the received bytes more than content length
            pSipMsg->pos += pMsgState->clValue;
            pSipMsg->end = pSipMsg->pos;
            for (int i=0; i<chunkLen-pMsgState->clValue; i++)
            {
                if(pSipMsg->buf[pSipMsg->end+i] >= 'A' && pSipMsg->buf[pSipMsg->end+i] <= 'Z')
                {
                    *pNextStart = pSipMsg->end+i;
                    remainingBytes = chunkLen - pMsgState->clValue - i - 1;
                    goto EXIT;
                }
            }

            *pNextStart = pSipMsg->end+chunkLen;
            remainingBytes = 0;
            goto EXIT;
        }
        else
        {
            //the received bytes less than the content length
            pMsgState->clValue -= chunkLen;
            pSipMsg->pos = pSipMsg->end;

            goto EXIT;
        }
    }

    //check content-length and end of headers in the sip message header part
	int clPos = 0;	//points to the first char of "Content-Length", used for case when cl is resolved, but tcp message stopped before RNRN
    for(int i=pSipMsg->pos; i<pSipMsg->end; i++)
    {
        switch(pSipMsg->buf[i])
        {
            case '\r':
                //debug("i=%d, received CR, pattern=%d.", i, pMsgState->eomPattern);
                if(pMsgState->eomPattern == SIP_TRANSPORT_EOM_OTHER)
                {
                    pMsgState->eomPattern = SIP_TRANSPORT_EOM_R;
                }
                else if(pMsgState->eomPattern == SIP_TRANSPORT_EOM_RN)
                {
                    pMsgState->eomPattern = SIP_TRANSPORT_EOM_RNR;
                }
                else
                {
                    logError("received two continuous CR.");
                    pMsgState->isBadMsg = true;
                    pMsgState->eomPattern == SIP_TRANSPORT_EOM_OTHER;
                }
                break;
            case '\n':
                //debug("i=%d, received LR, patterm=%d.", i, pMsgState->eomPattern);
                if(pMsgState->eomPattern == SIP_TRANSPORT_EOM_R)
                {
                    pMsgState->eomPattern = SIP_TRANSPORT_EOM_RN;
                }
                else if(pMsgState->eomPattern == SIP_TRANSPORT_EOM_RNR)
                {
                    pMsgState->eomPattern = SIP_TRANSPORT_EOM_RNRN;
                    remainingBytes = chunkLen + pSipMsg->pos - i - 1;
                    if(pMsgState->clValue <= 0)
                    {
						//find "\r\n\r\n", if Content-Length=0 or does not present, this is the end of a sip message
                        pMsgState->clValue = 0;
                        pSipMsg->pos = i+1;
						if(pSipMsg->end > pSipMsg->pos)
						{
							*pNextStart = pSipMsg->pos;
						}
		
						//the sip message delimit is done, set proper pos/end for the current pSipMsg
                        pSipMsg->end = pSipMsg->pos;
						pSipMsg->pos = 0;
                        goto EXIT;
                    }
                    else
                    {
                        //the remainingBytes bytes more than content length
                        if(remainingBytes >= pMsgState->clValue)
                        {
							mdebug(LM_TRANSPORT, "there are more bytes received than the current sip message, remainingBytes=%ld", remainingBytes);

							//the sip message delimit is done, set proper pos/end for the current pSipMsg
                            pSipMsg->pos = 0;
                            pSipMsg->end = i + 1 + pMsgState->clValue;

                            remainingBytes -= pMsgState->clValue;
                            //get rid of any garbage between this sip message and next sip message.  The first char of next sip message be A-Z
                            for (int j=0; j<remainingBytes; j++)
                            {
                                if(pSipMsg->buf[pSipMsg->end+j] >= 'A' && pSipMsg->buf[pSipMsg->end+j] <= 'Z')
                                {
                                    *pNextStart = pSipMsg->end+j;
                                    remainingBytes -= j;
                                    goto EXIT;
                                }
                            }
                            
							remainingBytes = 0;
                            goto EXIT;
                        }
                        else
                        {
                            //the remainingBytes bytes less than content length, part of this sip message will be in next TCP message
                            pMsgState->clValue -= remainingBytes;
							remainingBytes = -1;
                            pSipMsg->pos = pSipMsg->end;
                            goto EXIT;
                        }
                    }
                }
                else
                {
                    logError("received a LF but there in no preceeding CR.");
                    pMsgState->isBadMsg = true;
                    pMsgState->eomPattern == SIP_TRANSPORT_EOM_OTHER;
                }
                break;
            case 'C':
            {
                //debug("i=%d, received C, patterm=%d.", i, pMsgState->eomPattern);
                //if content-value has already been identified, or not a beginning of a header, bypass
                if(pMsgState->clValue >= 0 || pMsgState->eomPattern != SIP_TRANSPORT_EOM_RN)
                {
                    pMsgState->eomPattern = SIP_TRANSPORT_EOM_OTHER;
                    break;
                }

                pMsgState->eomPattern = SIP_TRANSPORT_EOM_OTHER;

                //if the nextSipMsgBytes message is not big enough to contain "Content-Length"
                if(i+14 >= pSipMsg->end)
                {
                    pMsgState->eomPattern = SIP_TRANSPORT_EOM_RN;
                    pSipMsg->pos = i;
                    goto EXIT;
                }

                //if the nextSipMsgBytes message is big enough to contain "Content-Length", but this header is not a Content-Lenth header
                if(pSipMsg->buf[i+9] != 'e')
                {
                    break;
                }

                //a potential Content-Length candidate
                bool isCLMatch = true;
                char* cl="ontent-Length";
                for(int j=1; j<14; j++)
                {
                    if(pSipMsg->buf[i+j] != cl[j-1])
                    {
                        isCLMatch = false;
                        break;
                    }
                }
                if(!isCLMatch)
                {
                    break;
                }

                //has found the Content-Length header, next step is to get the cl value
				clPos = i;

                //clValueState=-1, right after content-header name, clValueState=0, right after ':', clValueState>0, cl value is found
                int clValueState = -1;
                int j;
                for(j= i+14; j<pSipMsg->end; j++)
                {
                    if(pSipMsg->buf[j] == ' ' || pSipMsg->buf[j] == '\t')
                    {
                        if(clValueState > 0)
                        {
                            break;
                        }
                        continue;
                    }
                    else if(pSipMsg->buf[j] == ':')
                    {
                        clValueState = 0;
                    }
                    else if(pSipMsg->buf[j] == '\r' && pMsgState->clValue >= 0)
                    {
                        //done with content-length parsing, set back 1 char to make the position in front of '\r'
                        j--;
						mdebug(LM_TRANSPORT, "Content-Length=%d", pMsgState->clValue);
                        break;
                    }
                    else if(pSipMsg->buf[j] >= '0' && pSipMsg->buf[j] <= '9')
                    {
                        if(pMsgState->clValue == -1)
                        {
                            clValueState = j;
                            pMsgState->clValue = 0;
                        }
                        pMsgState->clValue = pMsgState->clValue*10 + (pSipMsg->buf[j] - '0');
                    }
                    else
                    {
                        //the content-length header value contains no digit char, pretend this is not a content-length header, the message will be discarded later when no content-length header is found
                        pMsgState->isBadMsg = true;
                        break;
                    }
                }

                if(j == pSipMsg->end)
                {
					//retrieved the whole TCP message, in the CL header, but the value has not identified, i.e., the CL header will be continued in the next read().  return bck to the beginning of the CL header
                    pSipMsg->pos = i;
                    pMsgState->eomPattern = SIP_TRANSPORT_EOM_RN;
					goto EXIT;
                }
                else
                {
                    //next iteration starts from j
                    i = j;
                }

                break;
            }
            default:
                //debug("i=%d, received %c, patterm=%d.", i, pSipMsg->buf[i], pMsgState->eomPattern);
                pMsgState->eomPattern = SIP_TRANSPORT_EOM_OTHER;
                break;
        }	//switch(pSipMsg->buf[i])
    }	//for(int i=pSipMsg->pos; i<pSipMsg->end; i++)

	//if getting here, and clValue >=0, meaning cl is resolved, but \r\n\r\n has not reached, get back to the beginning of CL header, and redo cl resolving in next TCP message
	if(pMsgState->clValue >= 0)
	{
		pMsgState->clValue = -1;
		pSipMsg->pos = clPos;
		pMsgState->eomPattern = SIP_TRANSPORT_EOM_RN;
		remainingBytes == -1;
	}

EXIT:
    //buf is full, but still not found the end of a sip packet, treat this as a bad packet.
    if(remainingBytes == -1 && pSipMsg->end == pSipMsg->size)
    {
        remainingBytes == 0;
        pMsgState->isBadMsg = true;
    }

    return remainingBytes;
}


osStatus_e tpProcessSipMsg(tpTcm_t* pTcm, int tcpFd, ssize_t len, bool* isForwardMsg)
{
    osStatus_e status = OS_STATUS_OK;
	*isForwardMsg = false;

    //basic sanity check to remove leading \r\n.  here we also ignore other invalid chars, 3261 only says \r\n
    if(pTcm->msgConnInfo.pMsgBuf->pos == 0 && (pTcm->msgConnInfo.pMsgBuf->buf[0] < 'A' || pTcm->msgConnInfo.pMsgBuf->buf[0] > 'Z'))
    {
        mdebug(LM_TRANSPORT, "received pkg proceeded with invalid chars, char[0]=0x%x, len=%d.", pTcm->msgConnInfo.pMsgBuf->buf[0], len);
        if(sipTpSafeGuideMsg(pTcm->msgConnInfo.pMsgBuf, len))
        {
            goto EXIT;
        }
    }

    size_t nextStart = 0;
    ssize_t nextSipMsgBytes = sipTpAnalyseMsg(pTcm->msgConnInfo.pMsgBuf, &pTcm->msgConnInfo.sipState, len, &nextStart);

    //if isBadMsg, drop the current received sip message, and reuse the pSipMsgBuf
    if(pTcm->msgConnInfo.sipState.isBadMsg)
    {
        mlogInfo(LM_TRANSPORT, "receive a invalid message, ignore.");

		tpTcmBufInit(pTcm, false);
        pTcm->msgConnInfo.pMsgBuf->pos = 0;
        pTcm->msgConnInfo.pMsgBuf->end = 0;
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    if(nextSipMsgBytes < 0)
    {
        mdebug(LM_TRANSPORT, "nextSipMsgBytes=%d, less than 0, the sip message to be completed in next TCP read.", nextSipMsgBytes);
        goto EXIT;
    }
    else
    {
        osMBuf_t* pCurSipBuf = pTcm->msgConnInfo.pMsgBuf;
        if(!tpTcmBufInit(pTcm, true))
        {
            logError("fails to init a TCM pSipMsgBuf.");
            status = OS_ERROR_SYSTEM_FAILURE;
            goto EXIT;
        }

        //copy the next sip message pieces that have been read into the newly allocated pSipMsgBuf
        if(nextSipMsgBytes > 0)
        {
            memcpy(pTcm->msgConnInfo.pMsgBuf->buf, &pCurSipBuf->buf[nextStart], nextSipMsgBytes);
        }
        pTcm->msgConnInfo.pMsgBuf->end = nextSipMsgBytes;
        pTcm->msgConnInfo.pMsgBuf->pos = 0;
        pTcm->msgConnInfo.isUsed = true;
		*isForwardMsg = true;
    }

EXIT:
    return status;
}


//return value: true: discard msg, false, keep msg
bool sipTpSafeGuideMsg(osMBuf_t* sipBuf, size_t len)
{
    if(sipBuf->pos != 0 || (sipBuf->buf[0] >= 'A' && sipBuf->buf[0] <= 'Z'))
    {
        return false;
    }

    int i;
    for (int i=0; i<len; i++)
    {
        if(sipBuf->buf[i] >= 'A' && sipBuf->buf[i] <= 'Z')
        {
            memmove(sipBuf->buf, &sipBuf->buf[i], len-i);
            sipBuf->pos = len-i;
            return false;
        }
    }

    return true;
}


//if msgType != SIP_TRANS_MSG_TYPE_TX_TCP_READY, set tcpfd=-1
void sipTpNotifyTcpConnUser(osListPlus_t* pList, transportStatus_e connStatus, int tcpfd, struct sockaddr_in* pPeer)
{
	sipTransportStatusMsg_t sipTpMsg;
    sipTransMsgType_e msgType;

	sipTpMsg.tcpFd = tcpfd;
	if(connStatus == SIP_TRANS_MSG_TYPE_TX_FAILED)
	{
		sipTpMsg.tcpFd = -1;
	}

	switch(connStatus)
	{
		case TRANSPORT_STATUS_TCP_SERVER_OK:
			goto EXIT;
			break;
		case TRANSPORT_STATUS_TCP_OK:
            msgType = SIP_TRANS_MSG_TYPE_TX_TCP_READY;
            sipTpMsg.tcpFd = tcpfd;
            break;
		default:
            msgType = SIP_TRANS_MSG_TYPE_TX_FAILED;
            sipTpMsg.tcpFd = -1;
			break;
	}
     
    if(pList->first)
    {
        sipTpMsg.pTransId = pList->first;
        sipTrans_onMsg(msgType, &sipTpMsg, 0);
    }

    if(pList->num > 1)
    {
        osListElement_t* pLE = pList->more.head;
        while(pLE)
        {
            sipTpMsg.pTransId = pLE->data;
            sipTrans_onMsg(msgType, &sipTpMsg, 0);
            pLE = pLE->next;
        }
    }

EXIT:
    //clear appIdList
    osListPlus_clear(pList);
}

