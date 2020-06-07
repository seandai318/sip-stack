//#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>

#include "osDebug.h"
#include "osTimer.h"
#include "osMBuf.h"
#include "osConfig.h"

#include "sipConfig.h"
#include "sipUriparam.h"
#include "sipTransportIntf.h"
#include "tcm.h"
#include "transportLib.h"


#if 0
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

    sipTpMsgState_t msgState = *pMsgState;
//    osMBuf_t* pSipMsg = pSipTpBuf->pSipMsgBuf;
    pSipMsg->end += chunkLen;

    ssize_t remaining = -1;
    *pNextStart = 0;

    //first check if is waiting for content
    if(msgState.clValue > 0)
    {
        if(chunkLen >= msgState.clValue)
        {
            //the received bytes more than content length
            pSipMsg->pos += msgState.clValue;
            pSipMsg->end = pSipMsg->pos;
            for (int i=0; i<chunkLen-msgState.clValue; i++)
            {
                if(pSipMsg->buf[pSipMsg->end+i] >= 'A' && pSipMsg->buf[pSipMsg->end+i] <= 'Z')
                {
                    *pNextStart = pSipMsg->end+i;
                    remaining = chunkLen - msgState.clValue - i - 1;
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
            msgState.clValue -= chunkLen;
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
				//debug("i=%d, received CR, pattern=%d.", i, *pMsgState.eomPattern);
                if(*pMsgState.eomPattern == SIP_TRANSPORT_EOM_OTHER)
                {
                    *pMsgState.eomPattern = SIP_TRANSPORT_EOM_R;
                }
                else if(*pMsgState.eomPattern == SIP_TRANSPORT_EOM_RN)
                {
                    *pMsgState.eomPattern = SIP_TRANSPORT_EOM_RNR;
                }
                else
                {
                    logError("received two continuous CR.");
                    msgState.isBadMsg = true;
                    *pMsgState.eomPattern == SIP_TRANSPORT_EOM_OTHER;
                }
                break;
            case '\n':
				//debug("i=%d, received LR, patterm=%d.", i, *pMsgState.eomPattern);
                if(*pMsgState.eomPattern == SIP_TRANSPORT_EOM_R)
                {
                    *pMsgState.eomPattern = SIP_TRANSPORT_EOM_RN;
                }
                else if(*pMsgState.eomPattern == SIP_TRANSPORT_EOM_RNR)
                {
                    *pMsgState.eomPattern = SIP_TRANSPORT_EOM_RNRN;
                    remaining = chunkLen + pSipMsg->pos - i - 1;
                    if(msgState.clValue <= 0)
                    {
                        msgState.clValue = 0;
                        pSipMsg->pos = i+1;
                        pSipMsg->end = pSipMsg->pos;
                        *pNextStart = pSipMsg->end;
                        goto EXIT;
                    }
                    else
                    {
                        //the remaining bytes more than content length
logError("to-remove, TCPCL, remaining=%ld, msgState.clValue=%ld", remaining,  msgState.clValue);
                        if(remaining >= msgState.clValue)
                        {
                            pSipMsg->pos = i + 1 + msgState.clValue;
                            pSipMsg->end = pSipMsg->pos;
                            remaining -= msgState.clValue;
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
                            msgState.clValue -= remaining;
                            pSipMsg->pos = pSipMsg->end;
                            goto EXIT;
                        }
                    }
                }
                else
                {
                    logError("received a LF but there in no preceeding CR.");
                    msgState.isBadMsg = true;
                    *pMsgState.eomPattern == SIP_TRANSPORT_EOM_OTHER;
                }
                break;
            case 'C':
            {
                //debug("i=%d, received C, patterm=%d.", i, *pMsgState.eomPattern);
                //if content-value has already been identified, or not a beginning of a header, bypass
                if(msgState.clValue >= 0 || *pMsgState.eomPattern != SIP_TRANSPORT_EOM_RN)
                {
                    *pMsgState.eomPattern = SIP_TRANSPORT_EOM_OTHER;
                    break;
                }

                *pMsgState.eomPattern = SIP_TRANSPORT_EOM_OTHER;

                //if the remaining message is not big enough to contain "Content-Length"
                if(i+14 >= pSipMsg->end)
                {
                    *pMsgState.eomPattern = SIP_TRANSPORT_EOM_RN;
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
                    else if(pSipMsg->buf[j] == '\r' && msgState.clValue >= 0)
                    {
						//do not understand why set clValueState=0? shall make it > 0? do since break happens, whatever value makes no difference
                        clValueState = 0;
                        j--;
logError("to-remove, TCPCL, cl=%d", msgState.clValue);
                        break;
                    }
                    else if(pSipMsg->buf[j] >= '0' && pSipMsg->buf[j] <= '9')
                    {
                        if(msgState.clValue == -1)
                        {
                            clValueState = j;
                            msgState.clValue = 0;
                        }
                        msgState.clValue = msgState.clValue*10 + (pSipMsg->buf[j] - '0');
                    }
                    else
                    {
                        //the content-length header value contains no digit char, pretend this is not a content-length header, the message will be discarded later when no content-length header is found
                        msgState.isBadMsg = true;
                        break;
                    }
                }

                //if there is no break out from the above "for(j= i+14; j<end; j++)" loop, means the CL header will be continued in the next read(), retrieve to the beginning of CL header
                if(j == pSipMsg->end)
                {
                    pSipMsg->pos = i;
                    *pMsgState.eomPattern = SIP_TRANSPORT_EOM_RN;
                }
                else
                {
                    //next iteration starts from j
                    i = j;
                }

                break;
            }
            default:
                //debug("i=%d, received %c, patterm=%d.", i, pSipMsg->buf[i], *pMsgState.eomPattern);
                *pMsgState.eomPattern = SIP_TRANSPORT_EOM_OTHER;
                break;
        }
    }

EXIT:
    //buf is full, but still not found the end of a sip packet, treat this as a bad packet.
    if(remaining == -1 && pSipMsg->end == pSipMsg->size)
    {
        remaining == 0;
        *pMsgState.isBadMsg = true;
    }

    return remaining;
}


// pNextStart: if there is extra bytes, the position in the buf that the extra bytes starts
// return value: -1: expect more read() for the current dia packet, 0: exact dia packet, >1 extra bytes for next dia packet.
ssize_t diaTpAnalyseMsg(osMBuf_t* pDiaMsg, TpMsgState_t* pMsgState, size_t chunkLen, size_t* pNextStart)
{
	if(!pDiaMsg || !pMsgState)
	{
		logError("null pointer, pDiaMsg=%p, pMsgState=%p.", pDiaMsg, pMsgState);
		return -1;
	}

	int remaining = -1;
	*pNextStart = 0;
	if(msgState.msgLen == 0)
	{
        pMsgState->msgLen = htobe32(*(uint32_t*)pDiaBuf->buf) & 0xffffff;
		pMsgState->receivedBytes = 0;
	}

	if(chunkLen >= (pMsgState->msgLen - pMsgState->receivedBytes))
	{
		//reset state 
		pMsgState->receivedBytes = 0;
		pMsgState->msgLen = 0;

		remaining = chunkLen - (pMsgState->msgLen - pMsgState->receivedBytes);
		*pNextStart = pMsgState->msgLen - pMsgState->receivedBytes;
		goto EXIT;
	}
	else
	{
		pMsgState->receivedBytes += chunkLen;
		goto EXIT;
	}

EXIT:
	return remaining;
}	
#endif			

#if 0
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
#endif

osStatus_e tpConvertPLton(transportIpPort_t* pIpPort, bool isIncludePort, struct sockaddr_in* pSockAddr)
{
    osStatus_e status = OS_STATUS_OK;
    char ip[INET_ADDRSTRLEN]={};

    pSockAddr->sin_family = AF_INET;
    if(isIncludePort)
    {
        pSockAddr->sin_port = htons(pIpPort->port);
    }
    else
    {
        pSockAddr->sin_port = 0;
    }

    if(osPL_strcpy(&pIpPort->ip, ip, INET_ADDRSTRLEN) != 0)
    {
        logError("fails to perform osPL_strcpy for IP(%r).", pIpPort->ip);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    if(inet_pton(AF_INET, ip, &pSockAddr->sin_addr.s_addr) != 1)
    {
        logError("fails to perform inet_pton for IP(%s), errno=%d.", ip, errno);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

EXIT:
    return status;
}

osStatus_e tpCreateTcp(int tpEpFd, struct sockaddr_in* peer, struct sockaddr_in* local, int* sockfd, int* connStatus)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    //int sockfd;

    if((*sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
    {
        logError("could not open a TCP socket, errno=%d.", errno);
        status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }

#if 0	//use network address
    struct sockaddr_in src, dest;

    //do not bind port, use ephemeral port.
    status = tpConvertPLton(local, false, &src);
    if(status != OS_STATUS_OK)
    {
        logError("fails to perform tpConvertPLton for local.");
        goto EXIT;
    }

    status = tpConvertPLton(peer, true, &dest);
    if(status != OS_STATUS_OK)
    {
        logError("fails to perform tpConvertPLton for peer.");
        goto EXIT;
    }

    if(bind(*sockfd, (const struct sockaddr *)&src, sizeof(src)) < 0 )
    {
        logError("fails to bind for sockfd (%d), localIP=%r, localPort=%d, errno=%d", *sockfd, &local->ip, local->port, errno);
        close(*sockfd);
        status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }
#else
    //do not bind port, use ephemeral port.
    local->sin_port = 0;

    if(bind(*sockfd, (const struct sockaddr *)local, sizeof(struct sockaddr_in)) < 0 )
    {
        logError("fails to bind for sockfd(%d), local(%A), errno=%d", *sockfd, local, errno);
        close(*sockfd);
        status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }
#endif

    struct epoll_event event;
    event.events = EPOLLOUT | EPOLLRDHUP;       //after connection is established, events will be changed to EPOLLIN
    event.data.fd = *sockfd;
    epoll_ctl(tpEpFd, EPOLL_CTL_ADD, *sockfd, &event);

    *connStatus = connect(*sockfd, (struct sockaddr*)peer, sizeof(struct sockaddr_in));
    if(*connStatus != 0 && errno != EINPROGRESS)
    {
        logError("fails to connect() for peer(%A), errno=%d.", peer, errno);
        status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }

EXIT:
	DEBUG_END
    return status;
}


