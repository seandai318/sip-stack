#ifndef _MAS_MGR_H
#define _MAS_MGR_H

typedef enum {
	MAS_MODULE_SIP,
	MAS_MODULE_OTHER,
} masModuleType_e;

typedef struct {
	masModuleType_e moduleType;
	void* pData;
} masTimerData_t;


typedef struct masInfo {
//	osMBuf_t* pSipReqBuf;
	void* pSrcTransId;
	void* pDstTransId;
	void* regId;		//store the reg hashLE for the UE
//	sipTransViaInfo_t viaId;
} masInfo_t;


void masInit();


#endif
