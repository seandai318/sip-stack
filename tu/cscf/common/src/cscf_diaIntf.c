#include "diaIntf.h"
#include "diaCxSar.h"
#include "diaMsg.h"


osStatus_e diaCx_initSAR(osPointerLen_t* pImpi, osPointerLen_t* pImpu, dia3gppServerAssignmentType_e sarType, diaNotifyApp_h appCallback, void* appData)
{
	osStatus_e status = OS_STATUS_OK;

    osVPointerLen_t userName = {{NULL, 0}, false, false};
	if(pImpi)
	{
		osPL_setStr(&userName.pl, pImpi->p, pImpi->l);
	}
    osVPointerLen_t pubId = {{*pImpu, false, false};
    osVPointerLen_t* pServerName = scscfConfig_getServerName();
    osVPointerLen_t* pHssHost = dia_getActiveDest(scscf_addr);
	if(!pHssHost)
	{
		logInfo("the connection towards HSS is not available for impu(%r).", &pImpu->pl);
		status = OS_ERROR_NETWORK_FAILURE;
		goto EXIT;
	}

    diaCxSarInfo_t sarInfo;
    sarInfo.serverAssignmentType = sarType;
    sarInfo.userDataAvailable = DIA_3GPP_CX_USER_DATA_NOT_AVAILABLE;
    sarInfo.multiRegInd = DIA_3GPP_CX_MULTI_REG_IND_NO_EXIST;
    sarInfo.pRestorationInfo = NULL;

    diaAvp_supportedFeature_t sf;
    sf.fl[0].vendorId = DIA_AVP_VENDOR_3GPP;
    sf.fl[0].featureListId = 1;
    sf.fl[0].featureList = 1 << DIA_CX_FEATURE_LIST_ID_SIFC;
    sf.flNum = 1;

    diaHdrSessInfo_t diaHdrSessInfo;
	osMBuf_t* pMBuf = diaBuildSar(pImpi ? &userName : NULL, &pubId, &serverName, &HssHost, &sarInfo, &sf, NULL, &diaHdrSessInfo);
	if(!pMBuf)
	{
		logError("fails to diaBuildSar for impu(%r).", pImpu);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	status = diaSendAppMsg(DIA_INTF_TYPE_CX, pMBuf, &diaHdrSessInfo.sessionId.pl, appCallback, appData);
	osMBuf_dealloc(pBuf);
	if(status != OS_STATUS_OK)
	{
		logError("fails to dia_sendAppMsg() for impu(%r) for SAR.", pImpu);
		goto EXIT;
	}

EXIT:
	return status;
}


#if 0
typedef struct diaMsgDecoded {
    diaCmdCode_e cmdCode;
    uint8_t cmdFlag;
    uint32_t len;
    uint32_t appId;
    uint32_t h2hId;
    uint32_t e2eId;
    osList_t avpList;       //each element contains diaAvp_t
} diaMsgDecoded_t;
#endif
#if 0
void cscf_onDiaResponse(diaMsgDecoded_t* pDiaDecoded)
{
	switch(pDecoded->cmdCode)
	{
		case DIA_CMD_CODE_UAR:
        case DIA_CMD_CODE_LIR:
			icscf_onDiaResponse(pDecoded->cmdCode, pDiaDecoded);
			break;
		case DIA_CMD_CODE_MAR:
		case DIA_CMD_CODE_SAR:
        case DIA_CMD_CODE_LIR:
        case DIA_CMD_CODE_PPR:
        case DIA_CMD_CODE_RTR:
		{
			scscfReg_onDiaResponse(pDecoded->cmdCode, pDiaDecoded);
			break;
		default:
			logError("cscf received a diamsg that it does not recognize(%r), ignore.", pDecoded->cmdCode);
			break;
	}
} 	
#endif
