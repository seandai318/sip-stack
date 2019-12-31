#ifndef _SIP_HDR_HASH_H
#define _SIP_HDR_HASH_H


typedef struct gperfSipHdrName {
    const char* name;
    int nameCode;
} gperfSipHdrName_t;


unsigned int gperfSipHdrHash(register const char *str, register size_t len);
struct gperfSipHdrName* gperfSipHdrLookup(register const char *str, register size_t len);



#endif
