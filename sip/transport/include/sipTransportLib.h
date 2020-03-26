#ifndef _SIP_TRANSPORT_LIB_H
#define _SIP_TRANSPORT_LIB_H

bool sipTpSafeGuideMsg(osMBuf_t* sipBuf, size_t len);
ssize_t sipTpAnalyseMsg(sipTpBuf_t* pSipTpBuf, size_t chunkLen, size_t* pNextStart);
osStatus_e sipTpConvertPLton(sipTransportIpPort_t* pIpPort, bool isIncPort, struct sockaddr_in* pSockAddr);


#endif

