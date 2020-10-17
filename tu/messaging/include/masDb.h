/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file masDb.h
 ********************************************************/

#ifndef _MAS_DB_H
#define _MAS_DB_H


#include "osTypes.h"
#include "osPL.h"

#define MAS_DB_POLL_TIME		2000		//time to poll DB for any timeout, 2 sec
//#define MAS_DB_SMS_TIMER_T1		450			//sec, initial redelivery timer
#define MAS_DB_SMS_TIMER_T1     60         //sec, initial redelivery timer
#define MAS_DB_SMS_TIMER_T2		14400		//sec, 4 hours, cap of the redelivery timer interval
#define MAS_DB_SMS_MAX_TIMER_M	86400		//sec, 24 hours, after that, a SMS is to be discarded

//typedef void (*masProcessStoredSMS_h) (const MYSQL_ROW* pRow, bool* isContinue, bool* isStore);


osStatus_e masDbInit(char* dbName);
osStatus_e masDbQuerySMSByUser(osPointerLen_t* user);
//if userId=-1, this function calls masDbSetUser() to set the user in the table if not exist
osStatus_e masDbStoreSms(ssize_t userId, osPointerLen_t* user, osPointerLen_t* caller, osPointerLen_t* sms);
osStatus_e masDbDeleteSms(size_t smsId);
osStatus_e masDbGetUserId(osPointerLen_t* user, size_t* userId);
//first check if the user is already in the table, if yes, get userId and return, if not, store it.
osStatus_e masDbSetUser(osPointerLen_t* user, size_t* userId);


#endif
