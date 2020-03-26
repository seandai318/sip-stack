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


typedef enum {
	MAS_SMS_TYPE_B2B,
	MAS_SMS_TYPE_PSTN,
	MAS_SMS_TYPE_DB,
} masSmsType_e;


typedef struct masUacData {
	size_t dbSmsId;
	osDPointerLen_t user;
	osDPointerLen_t caller;
	osDPointerLen_t sms;
} masUacData_t;


typedef struct masInfo {
//	osMBuf_t* pSipReqBuf;
	masSmsType_e smsType;
	masUacData_t uacData; 
	void* pSrcTransId;	//if b2b, pSrcTransId != NULL, otherwise, pSrcTransId == NULL
	void* pDstTransId;
	void* regId;		//store the reg hashLE for the UE
//	sipTransViaInfo_t viaId;
} masInfo_t;


void masInit();
void masInfo_cleanup(void *data);

#endif
