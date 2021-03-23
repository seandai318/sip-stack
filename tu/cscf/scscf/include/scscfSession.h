#ifndef __SCSCF_SESSION_H
#define __SCSCF_SESSION_H

#include "scscfRegistrar.h"



typedef enum {
    SCSCF_SESS_STATE_INIT,		//a brand new request not associated with a session is just received
	SCSCF_SESS_STATE_MO_INIT,	//INIT for a brand new MO request not associated with a session, MO only
    SCSCF_SESS_STATE_SAR,       //perform SAR for UNREGISTERED case, for both MO & MT
    SCSCF_SESS_STATE_DNS_AS,  	//perform DNS query for AS address, send req to AS, for both MO and MT
	SCSCF_SESS_STATE_MO2MT_INIT,//done MO, start to perform MT.
	SCSCF_SESS_STATE_MT_INIT,   //INIT for a brand new MT request directly received from LB, not associated with a session, MT only
	SCSCF_SESS_STATE_DNS_ENUM,	//perform ENUM to determine if a MT user is in-net or outof net, MT only
	SCSCF_SESS_STATE_LIR,		//request ICSCF to perform LIR to determine local or geo SCSCF, MT only
	SCSCF_SESS_STATE_TO_LOCAL_CSCF,	//forward request to local CSCF (the same SCSCF program, but different thread), MT only
	SCSCF_SESS_STATE_TO_GEO_SCSCF,	//forward request to the GEO SCSCF, MT only
    SCSCF_SESS_STATE_TO_UE,     	//done AS, send req to UE, MT only
    SCSCF_SESS_STATE_TO_BREAKOUT,   //a outof network MT user, breakout, MT only
    SCSCF_SESS_STATE_ESTABLISHED,   //when a session is established.  
	SCSCF_SESS_STATE_CLOSING,		//when a session is closing, like BYE is received
} scscfSessState_e;


typedef struct {
	osListElement_t* pOdiHashElement;	//the hash element in the session hash when a odi is used as a hash key
	sipPointerLen_t odi;        		//the Original Dialog Identifier towards an AS
} scscfSessOdiInfo_t;


//the information for each individual request and related ifc
typedef struct {
    bool isMO;
	bool isInitial;					//this request has been forwarded by the SCSCF before
    bool isContinuedDH;				//the latest ifc AS default handling
	osPointerLen_t callId;			//a session's callId may change in the middle if one of the AS is B2BUA
	osPointerLen_t users[SCSCF_MAX_PAI_NUM];	//all users from a sip request.  mostly only one, but sometimes more when there are more than one PAI.  If a session includes both MO and MT, user will change when MO becomes MT.  For normal use, users[0] is used as the impu for the request.  Other users are there for the checking of barring aliases
	int userNum;					//the number of users in a request
    scscfSessOdiInfo_t lastOdiInfo; //the latest odi info
	sipTuAddr_t nextHop;			//next hop the request is forwarded to
    sipTUMsg_t* pSipTUMsg;          //the latest sip request received
    sipMsgDecodedRawHdr_t* pReqDecodedRaw;  //the latest received decoded sip message
    osListElement_t* pLastIfc;  //points to the last ifc that was used to find the AS
} scscfSesTempWorkInfo_t;


typedef struct {
    scscfSessState_e state;
	scscfSesTempWorkInfo_t tempWorkInfo;	//for each initial request, changes from one req to another
	scscfRegState_e userRegState;	//if a session contains both MO and MT, user will change, so does this state.  Be noted this state is taken when a MO or MT first started.  if during the session establishment, the user's reg state changes, the state here would not change
	sIfcIdList_t sIfcIdList;
	void* pRegInfo;				//obscure regInfo, used to pass to registrar to get reg related information
    osList_t proxyList;         //contains proxyInfo_t, the list of proxies in order,  All proxies for both MO and MT for a request
	osList_t callIdHashElemList;	//contains the hash element for callId, it is possible there are callId change in a call session
} scscfSessInfo_t;



#endif
