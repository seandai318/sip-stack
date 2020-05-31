#include <string.h>

#include "osDebug.h"

#include "tcm.h"
#include "sipTransportIntf.h"



/*extract Content-Length and EOM from a newly received message piece
  pSipMsgBuf_pos = end of bytes processed.
  pSipMsgBuf->end = end of bytes received, except when a sip packet is found, which pSipMsgBuf->end = end of the sip packet
  if a sip packet does not contain Content-Length header, assume Content length = 0
  pNextStart: if there is extra bytes, the position in the buf that the extra bytes starts
  return value: -1: expect more read() for the current  sip packet, 0: exact sip packet, >1 extra bytes for next sip packet.
 */
ssize_t sipTpAnalyseMsg(osMBuf_t* pSipMsg, sipTpMsgState_t* pMsgState, size_t chunkLen, size_t* pNextStart)
{
    if(!pSipMsg || !pMsgState)
    {
        logError("null pointer, pSipMsg=%p, pMsgState=%p.", pSipMsg, pMsgState);
        return -1;
    }

    pSipMsg->end += chunkLen;

    ssize_t remaining = -1;
    *pNextStart = 0;

    //first check if is waiting for content
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
                    remaining = chunkLen - pMsgState->clValue - i - 1;
                    goto EXIT;
                }
            }

            *pNextStart = pSipMsg->end+chunkLen;
            remaining = 0;
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
                    remaining = chunkLen + pSipMsg->pos - i - 1;
                    if(pMsgState->clValue <= 0)
                    {
                        pMsgState->clValue = 0;
                        pSipMsg->pos = i+1;
                        pSipMsg->end = pSipMsg->pos;
                        *pNextStart = pSipMsg->end;
                        goto EXIT;
                    }
                    else
                    {
                        //the remaining bytes more than content length
logError("to-remove, TCPCL, remaining=%ld, pMsgState->clValue=%ld", remaining,  pMsgState->clValue);
                        if(remaining >= pMsgState->clValue)
                        {
                            pSipMsg->pos = i + 1 + pMsgState->clValue;
                            pSipMsg->end = pSipMsg->pos;
                            remaining -= pMsgState->clValue;
                            for (int j=0; j<remaining; j++)
                            {
                                if(pSipMsg->buf[pSipMsg->end+j] >= 'A' && pSipMsg->buf[pSipMsg->end+j] <= 'Z')
                                {
                                    *pNextStart = pSipMsg->end+j;
                                    remaining -= j;
                                    goto EXIT;
                                }
                            }
                            remaining = 0;
                            goto EXIT;
                        }
                        else
                        {
                            //the remaining bytes less than content length
                            pMsgState->clValue -= remaining;
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

                //if the remaining message is not big enough to contain "Content-Length"
                if(i+14 >= pSipMsg->end)
                {
                    pMsgState->eomPattern = SIP_TRANSPORT_EOM_RN;
                    pSipMsg->pos = i;
                    goto EXIT;
                }

                //if the remaining message is big enough to contain "Content-Length", but this header is not a Content-Lenth header
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
                        //do not understand why set clValueState=0? shall make it > 0? do since break happens, whatever value makes no difference
                        clValueState = 0;
                        j--;
logError("to-remove, TCPCL, cl=%d", pMsgState->clValue);
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

                //if there is no break out from the above "for(j= i+14; j<end; j++)" loop, means the CL header will be continued in the next read(), retrieve to the beginning of CL header
                if(j == pSipMsg->end)
                {
                    pSipMsg->pos = i;
                    pMsgState->eomPattern = SIP_TRANSPORT_EOM_RN;
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
        }
    }

EXIT:
    //buf is full, but still not found the end of a sip packet, treat this as a bad packet.
    if(remaining == -1 && pSipMsg->end == pSipMsg->size)
    {
        remaining == 0;
        pMsgState->isBadMsg = true;
    }

    return remaining;
}


osStatus_e tpProcessSipMsg(tpTcm_t* pTcm, int tcpFd, ssize_t len, bool* isForwardMsg)
{
    osStatus_e status = OS_STATUS_OK;
	*isForwardMsg = false;

    //mdebug(LM_TRANSPORT, "received TCP message, len=%d from fd(%d), bufLen=%ld, msg=\n%r", len, events[i].data.fd, bufLen, &dbgPL);
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
    ssize_t remaining = sipTpAnalyseMsg(pTcm->msgConnInfo.pMsgBuf, &pTcm->msgConnInfo.sipState, len, &nextStart);
    if(remaining < 0)
    {
        mdebug(LM_TRANSPORT, "remaining=%d, less than 0.", remaining);
        goto EXIT;
    }
    else
    {
        osMBuf_t* pCurSipBuf = pTcm->msgConnInfo.pMsgBuf;
        bool isBadMsg = pTcm->msgConnInfo.sipState.isBadMsg;
        //if isBadMsg, drop the current received sip message, and reuse the pSipMsgBuf
        if(!tpTcmBufInit(pTcm, isBadMsg ? false : true))
        {
            logError("fails to init a TCM pSipMsgBuf.");
            status = OS_ERROR_SYSTEM_FAILURE;
            goto EXIT;
        }

        //copy the next sip message pieces that have been read into the newly allocated pSipMsgBuf
        if(remaining > 0)
        {
            memcpy(pTcm->msgConnInfo.pMsgBuf->buf, &pCurSipBuf->buf[nextStart], remaining);
        }
        pTcm->msgConnInfo.pMsgBuf->end = remaining;
        pTcm->msgConnInfo.pMsgBuf->pos = 0;

        if(!isBadMsg)
        {
            pTcm->msgConnInfo.isUsed = true;
			*isForwardMsg = true;
            //tpServerForwardMsg(TRANSPORT_APP_TYPE_SIP, pCurSipBuf, tcpFd, &pTcm->peer);
        }
        else
        {
            mdebug(LM_TRANSPORT, "received a bad msg, drop.");
        }
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

