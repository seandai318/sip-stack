//#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
//#include <unistd.h>
#include <errno.h>
//#include <pthread.h>

#include "osDebug.h"
#include "osTimer.h"
#include "osMBuf.h"
#include "osConfig.h"
//#include "osResourceMgmt.h"

#include "sipConfig.h"
//#include "sipMsgFirstLine.h"
#include "sipUriparam.h"
//#include "sipTransMgr.h"
//#include "sipTransportMgr.h"
#include "sipTransportIntf.h"
#include "sipTcm.h"
#include "sipTransportLib.h"


/*extract Content-Length and EOM from a newly received message piece
  pSipMsgBuf_pos = end of bytes processed.
  pSipMsgBuf->end = end of bytes received, except when a sip packet is found, which pSipMsgBuf->end = end of the sip packet
  if a sip packet does not contain Content-Length header, assume Content length = 0
  return value: -1: expect more read() for the current  sip packet, 0: exact sip packet, >1 bytes for next sip packet.
 */
ssize_t sipTpAnalyseMsg(sipTpBuf_t* pSipTpBuf, size_t chunkLen, size_t* pNextStart)
{
    sipTpMsgState_t msgState = pSipTpBuf->state;
    osMBuf_t* pSipMsg = pSipTpBuf->pSipMsgBuf;
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
				//debug("i=%d, received CR, pattern=%d.", i, pSipTpBuf->state.eomPattern);
                if(pSipTpBuf->state.eomPattern == SIP_TRANSPORT_EOM_OTHER)
                {
                    pSipTpBuf->state.eomPattern = SIP_TRANSPORT_EOM_R;
                }
                else if(pSipTpBuf->state.eomPattern == SIP_TRANSPORT_EOM_RN)
                {
                    pSipTpBuf->state.eomPattern = SIP_TRANSPORT_EOM_RNR;
                }
                else
                {
                    logError("received two continuous CR.");
                    msgState.isBadMsg = true;
                    pSipTpBuf->state.eomPattern == SIP_TRANSPORT_EOM_OTHER;
                }
                break;
            case '\n':
				//debug("i=%d, received LR, patterm=%d.", i, pSipTpBuf->state.eomPattern);
                if(pSipTpBuf->state.eomPattern == SIP_TRANSPORT_EOM_R)
                {
                    pSipTpBuf->state.eomPattern = SIP_TRANSPORT_EOM_RN;
                }
                else if(pSipTpBuf->state.eomPattern == SIP_TRANSPORT_EOM_RNR)
                {
                    pSipTpBuf->state.eomPattern = SIP_TRANSPORT_EOM_RNRN;
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
                    pSipTpBuf->state.eomPattern == SIP_TRANSPORT_EOM_OTHER;
                }
                break;
            case 'C':
            {
                //debug("i=%d, received C, patterm=%d.", i, pSipTpBuf->state.eomPattern);
                //if content-value has already been identified, or not a beginning of a header, bypass
                if(msgState.clValue >= 0 || pSipTpBuf->state.eomPattern != SIP_TRANSPORT_EOM_RN)
                {
                    pSipTpBuf->state.eomPattern = SIP_TRANSPORT_EOM_OTHER;
                    break;
                }

                pSipTpBuf->state.eomPattern = SIP_TRANSPORT_EOM_OTHER;

                //if the remaining message is not big enough to contain "Content-Length"
                if(i+14 >= pSipMsg->end)
                {
                    pSipTpBuf->state.eomPattern = SIP_TRANSPORT_EOM_RN;
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
                    pSipTpBuf->state.eomPattern = SIP_TRANSPORT_EOM_RN;
                }
                else
                {
                    //next iteration starts from j
                    i = j;
                }

                break;
            }
            default:
                //debug("i=%d, received %c, patterm=%d.", i, pSipMsg->buf[i], pSipTpBuf->state.eomPattern);
                pSipTpBuf->state.eomPattern = SIP_TRANSPORT_EOM_OTHER;
                break;
        }
    }

EXIT:
    //buf is full, but still not found the end of a sip packet, treat this as a bad packet.
    if(remaining == -1 && pSipMsg->end == pSipMsg->size)
    {
        remaining == 0;
        pSipTpBuf->state.isBadMsg = true;
    }

    return remaining;
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


osStatus_e sipTpConvertPLton(sipTransportIpPort_t* pIpPort, bool isIncludePort, struct sockaddr_in* pSockAddr)
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

