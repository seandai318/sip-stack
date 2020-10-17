/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file masConfig.h
 ********************************************************/

#ifndef _MAS_CONFIG_H
#define _MAS_CONFIG_H


#define MAS_IN_IMS_NETWORK	false
//for memory testing, change MAS_REG_PURGE_TIMER to 10 sec
#ifdef PREMEM_DEBUG
#define MAS_REG_PURGE_TIMER 10
#else
#define MAS_REG_PURGE_TIMER	86400
#endif
#define MAS_REG_HASH_SIZE	10000

#define MAS_MAX_SMS_CONTENT_SIZE	2000

#define MAS_CONFIG_LISTENER_THREAD_NUM	1
#define MAS_CONFIG_TRANSACTION_THREAD_NUM	1


#endif

