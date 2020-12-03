#ifndef _SCSCF_REGISTRAR_H
#define _SCSCF_REGISTRAR_H


#include "sipHdrMisc.h"
#include "sipTUIntf.h"
#include "sipTU.h"

#include "cscfConfig.h"


typedef enum {
	SCSCF_REG_STATE_NOT_REGISTERED,		//HSS has no UE server assignment record
	SCSCF_REG_STATE_UN_REGISTERED,		//HSS has UE server assignment record due to SCSCF received a no-register request for the UE
	SCSCF_REG_STATE_REGISTERED,			//HSS has UE server assignment record due to SCSCF received REGISTER request from the UE
} scscfRegState_e;


typedef enum {
	SCSCF_REG_SAR_INVALID,
	SCSCF_REG_SAR_REGISTER,
	SCSCF_REG_SAR_RE_REGISTER,
	SCSCF_REG_SAR_DE_REGISTER,
	SCSCF_REG_SAR_UN_REGISTER,
} scscfRegSarRegType_e;


typedef enum {
	CHG_ADDR_TYPE_INVALID,
	CHG_ADDR_TYPE_CCF,
	CHG_ADDR_TYPE_ECF,
} scscfChgAddrType_e;


typedef struct {
	scscfChgAddrType_e chgAddrType;
	osVPointerLen_t chgAddr;
} scscfChgInfo_t;


typedef struct {
	osPointerLen_t impu;
	bool isBarred;
} scscfImpuInfo_t;


typedef struct {
	bool isImpi;
	union {
		osPointerLen_t	impi;
		scscfImpuInfo_t impuInfo;
	};
	osListElement_t*	pRegHashLE;	//points to hash listElement that contains this identity
} scscfRegIdentity_t;

#if 0
typedef struct {
	osPointerLen_t impi;
	osList_t impuList;			//each entry contains impuInfo_t
	sIfcIdList_t sIfcIdList;	
//	osList_t sIfcId;			//each entry contains a int, sorted from small to bigger
} scscfUserProfile_t;


typedef struct {
    uint32_t sIfcId[SCSCF_MAX_ALLOWED_SIFC_ID_NUM];
    uint32_t sIfcIdNum;
} sIfcIdList_t;
#endif

#if 0
typedef struct {
    osPointerLen_t impi;
    scscfImpuInfo_t impuInfo;
	sIfcIdList_t sIfcIdList;
    uint32_t impuNum;
} scscfRegUserProfile_t;
#endif

typedef struct {
    osDPointerLen_t user;       //it keeps its own memory, not point to other place like sipMsgBuf
//    sipMsgBuf_t* sipRegMsg;
    sipHdrDecoded_t* pContact;    //for now we only allow one contact per user
	uint32_t regExpire;
	sipTUMsg_t* pSipTUMsg;
} scscfRegContactInfo_t;


typedef struct {
    bool isRspFailed;           //a temporary state to indicate if response sending was failed.  This is needed as masReg_onSipMsg, response tr handling, response tp handling are a chain of call, by the time sipTrans_onMsg() returns, many times the tr/tp handling state is already known, and tr has already notified masReg about the state.  tr set this value when error happened, masReg will use this value to do proper thing
	struct sockaddr_in sipLocalHost;	//the address that receives the sip REGISTER.  for I/S cscf co-existance case, sipLocalHost is ICSCF address. if SCSCF receives a SIP REGISTER from remote, this address is empty 
	sipTUMsg_t* pSipTUMsg;
	sipMsgDecodedRawHdr_t* pReqDecodedRaw;
} scscfRegMsgInfo_t;


typedef enum {
	SCSCF_REG_WORK_STATE_NONE,
	SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_REG_RESPONSE,
	SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_NW_DEREG_RESPONSE,
	SCSCF_REG_WORK_STATE_WAIT_MAA,
	SCSCF_REG_WORK_STATE_WAIT_SAA,
} scscfRegWorkState_e;


typedef struct {
	sipTuAddr_t asAddr;
	osPointerLen_t asUri;
	sipPointerLen_t callId;
} scscfAsRegInfo_t;
 

//this data structure stores info for short period of time, like during the UE initial registration procedure, etc.
typedef struct {
	scscfRegWorkState_e	regWorkState;	//indicating in what stage a registration is
    scscfRegSarRegType_e sarRegType;    //when not in SAR procedure, this shall be set to SCSCF_REG_SAR_INVALID
	osPointerLen_t impi;
	osPointerLen_t impu;
	sipTUMsg_t* pTUMsg;
	sipMsgDecodedRawHdr_t* pReqDecodedRaw;
    osListElement_t* pRegHashLE;    //points to hash element of this data structure, this is redundant to what's in ueList, here for convenience
	osPointerLen_t* pAs;			//the application server from ifc query, need to store for dns query
	osListElement_t* pLastIfc;		//points to the last ifc that was used to find the AS
	struct sockaddr_in sipLocalHost;	//if reg was recived via icscf, this stores icscf address, otherwise, scscf address
} scscfRegTempWorkInfo_t;


typedef struct {
	scscfRegState_e regState;
	bool isLR;					//loose-route indication.. For now, always do loose-route
//	scscfRegIdentity_t	identity;
	osList_t ueList;			//a list of UE identities for a UE, including impu and impi, each element is a scscfRegIdentity_t
	osList_t sessDatalist;		//a list of sessions waiting for user profile.  only relevent when waiting for SAA and state = SCSCF_REG_STATE_UN_REGISTERED
	scscfRegTempWorkInfo_t tempWorkInfo;    //store info to assist registration procedure
		//scscfRegMsgInfo_t regMsgInfo;	//when received a sip REGISTER message from UE
	scscfUserProfile_t userProfile;
	scscfChgInfo_t hssChgInfo;
	scscfRegContactInfo_t ueContactInfo;	//contact info, expiry, etc.
//	scscfRegTempWorkInfo_t tempWorkInfo;	//store info to assist registration procedure
    osList_t asRegInfoList; //stores the asReg proxyInfo, each entry contains a scscfAsRegInfo_t block.  Used to send deregister for network initiated 3rd party deregistration
	uint64_t expiryTimerId;
	uint64_t purgeTimerId;
} scscfRegInfo_t;


void scscfReg_onTimeout(uint64_t timerId, void* data);
void scscfReg_createSubHash(scscfRegInfo_t* pRegInfo);
void scscfReg_deleteSubHash(scscfRegInfo_t* pRegInfo);
void scscfRegTempWorkInfo_cleanup(scscfRegTempWorkInfo_t* pTempWorkInfo);

#endif
