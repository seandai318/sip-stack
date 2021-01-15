#ifndef __SIP_TU_DEST_MGR_H
#define __SIP_TU_DEST_MGR_H


#define SIP_TU_DEST_MAX_SUPPORTED_TP_TYPE_NUM   5       	//in theory the value is 2, TCP, UDP.  As documented in the sipTuDestMgr.c, make it bigger just in case
#define SIP_TU_DEST_QUARANTINE_TIME				600000		//in msec, quarantine timer, 10 min 
#define SIP_TU_DEST_KEEP_ALIVE_TIME				36000000	//in msec, sipTuDestRecord_t keep alive timer, 10 hour
typedef struct {
    bool isQuarantined;
    transportType_e tpType;
    struct sockaddr_in destAddr;
//  sipTuDestAddr_t destAddr;
    int priority;
    int weight;
	uint64_t qTimerId;	//quarantine timer when isQuarantined = true
} sipTuDestInfo_t;


//The meaning of kaTimerId is overloaded.  for dest gotten fron dns query, this parameter will have a value.  for dest that is locally set, this value is 0.  so we can use this value to decide if who set the dest.
typedef struct {
    osVPointerLen_t destName;   //dns qName.  Will show the qNme of the highest used qType
    osList_t destList;          //each entry contains sipTuDestInfo_t, each element is sorted based on priority, higher priority(smaller priority value) first
    uint64_t kaTimerId;  //a timer to prevent app uses one query then does not use it again, when this tiemr expires, the associated record will be freed
} sipTuDestRecord_t;


typedef struct {
    transportType_e tpType;
    int priority;
    int weight;
    int port;
} sipTuDestSrvInfo_t;


#endif
