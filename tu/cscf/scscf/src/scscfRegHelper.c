/********************************************************************************************
 * Copyright (C) 2019-2021, Sean Dai
 *
 * @file scscfHelper.c
 * misc scscf helper functions
 ********************************************************************************************/

#include "scscfRegistrar.h"


osPointerLen_t* scscfReg_getNoBarImpu(scscfRegIdentity_t ueList[], uint8_t ueNum, bool isTelPreferred)
{
    osPointerLen_t* pImpu = NULL;

    for(int i=0; i<ueNum; i++)
    {
        if(!ueList[i].isImpi && !ueList[i].impuInfo.isBarred)
        {
            if(isTelPreferred)
            {
                if(ueList[i].impuInfo.impu.p[0] == 't' || ueList[i].impuInfo.impu.p[0] == 'T')
                {
                    pImpu = &ueList[i].impuInfo.impu;
                    goto EXIT;
                }
                else if(!pImpu)
                {
                    pImpu = &ueList[i].impuInfo.impu;
                }
            }
            else
            {
                pImpu = &ueList[i].impuInfo.impu;
                goto EXIT;
            }
        }
    }

EXIT:
    if (!pImpu)
    {
        logError("fails to find nobarred impu.");
    }
    return pImpu;
}


//find the first barred user
osPointerLen_t* scscfReg_getAnyBarredUser(void* pRegId, osPointerLen_t user[], int userNum)
{
    if(!pRegId || !user)
    {
        return NULL;
    }

    scscfRegInfo_t* pRegInfo = pRegId;
    uint8_t regInfoUENum;
    scscfRegIdentity_t ueList[SCSCF_MAX_ALLOWED_IMPU_NUM+1];
    for(int i=0; i<userNum; i++)
    {
        for(int j=0; j<pRegInfo->regInfoUENum; j++)
        {
            if(pRegInfo->ueList[j].isImpi)
            {
                continue;
            }
            if(osPL_cmp(&user[i], &pRegInfo->ueList[j].impuInfo.impu) == 0)
            {
                if(pRegInfo->ueList[j].impuInfo.isBarred)
                {
                    return &pRegInfo->ueList[j].impuInfo.impu;
                }

                break;
            }
        }
    }

    return NULL;
}


bool scscfReg_isUserBarred(void* pRegId, osPointerLen_t* pUser)
{
    bool isBarred = false;

    if(!pRegId || !pUser)
    {
        isBarred = true;
        goto EXIT;
    }

    scscfRegInfo_t* pRegInfo = pRegId;
    uint8_t regInfoUENum;
    scscfRegIdentity_t ueList[SCSCF_MAX_ALLOWED_IMPU_NUM+1];
    for(int j=0; j<pRegInfo->regInfoUENum; j++)
    {
        if(pRegInfo->ueList[j].isImpi)
        {
            continue;
        }

        if(osPL_cmp(pUser, &pRegInfo->ueList[j].impuInfo.impu) == 0)
        {
            if(pRegInfo->ueList[j].impuInfo.isBarred)
            {
                isBarred = true;
            }

            break;
        }
    }

EXIT:
    return isBarred;
}


//isSipUri: true, request to find one sip uri
//          false, request to find one tel uri
bool scscfReg_getOneNoBarUri(scscfRegInfo_t* pRegInfo, bool isSipUri, osPointerLen_t* noBarredUser)
{
	bool isFound = false;
    osPointerLen_t* pImpu = NULL;
	
    for(int i=0; i<pRegInfo->regInfoUENum; i++)
    {
		if(pRegInfo->ueList[i].isImpi)
        {
            continue;
        }

        if(pRegInfo->ueList[i].impuInfo.isBarred)
        {
			continue;
		}

        if(isSipUri)
        {
            if(pRegInfo->ueList[i].impuInfo.impu.p[0] == 's' || pRegInfo->ueList[i].impuInfo.impu.p[0] == 'S')
            {
                *noBarredUser = pRegInfo->ueList[i].impuInfo.impu;
                isFound = true;
                goto EXIT;
            }
		}
		else
		{
            if(pRegInfo->ueList[i].impuInfo.impu.p[0] == 't' || pRegInfo->ueList[i].impuInfo.impu.p[0] == 'T')
            {
                *noBarredUser = pRegInfo->ueList[i].impuInfo.impu;
				isFound = true;
                goto EXIT;
			}
		}
	}
			
EXIT:
	return isFound;
}
