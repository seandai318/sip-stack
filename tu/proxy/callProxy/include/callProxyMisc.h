#ifndef _CALL_PROXY_MISC_H
#define _CALL_PROXY_MISC_H


#include "osList.h"
#include "sipMsgFirstLine.h"


osStatus_e callProxy_addTrInfo(osListPlus_t* pList, sipRequest_e reqCode, uint32_t seqNum, void* uasId, void* uacId, bool isPrimary);


#endif

