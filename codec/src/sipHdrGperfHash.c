/* ANSI-C code produced by gperf version 3.1 */
/* Command-line: gperf -L ANSI-C -N gperfSipHdrLookup -H gperfSipHdrHash -D -t -K name --ignore-case test.gperf  */
/* Computed positions: -k'1,3,7,$' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gperf@gnu.org>."
#endif

#line 1 "test.gperf"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "sipHeader.h"
#include "sipHdrGperfHash.h"



//#line 113 "test.gperf"
//struct gperfSipHdrName {
//	const char* name;
//	int nameCode;
//};

#define TOTAL_KEYWORDS 103
#define MIN_WORD_LENGTH 2
#define MAX_WORD_LENGTH 29
#define MIN_HASH_VALUE 4
#define MAX_HASH_VALUE 212
/* maximum key range = 209, duplicates = 0 */

#ifndef GPERF_DOWNCASE
#define GPERF_DOWNCASE 1
static unsigned char gperf_downcase[256] =
  {
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
     30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
     45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
     60,  61,  62,  63,  64,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106,
    107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121,
    122,  91,  92,  93,  94,  95,  96,  97,  98,  99, 100, 101, 102, 103, 104,
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
    135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
    150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164,
    165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
    180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
    195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
    210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
    225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254,
    255
  };
#endif

#ifndef GPERF_CASE_STRCMP
#define GPERF_CASE_STRCMP 1

static int
gperf_case_strncmp (register const char *s1, register const char *s2, size_t len)
{
  for (int i=0; i<len;i++)
    {
      unsigned char c1 = gperf_downcase[(unsigned char)s1[i]];
      unsigned char c2 = gperf_downcase[(unsigned char)s2[i]];
      if (c1 != c2)
        return (int)c1 - (int)c2;
    }

    return 0;
}

#endif

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif

unsigned int
gperfSipHdrHash (register const char *str, register size_t len)
{
  static unsigned char asso_values[] =
    {
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213,  20, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213,  20,  65,  10,   0,   0,
       20,  90,  45,  40,  65,   0,  50,  60,   0,  55,
       25,   0,   5,   0,   0,  40,  10,  20,  20,  10,
        0, 213, 213, 213, 213, 213, 213,  20,  65,  10,
        0,   0,  20,  90,  45,  40,  65,   0,  50,  60,
        0,  55,  25,   0,   5,   0,   0,  40,  10,  20,
       20,  10,   0, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213, 213, 213, 213, 213,
      213, 213, 213, 213, 213, 213
    };
  register unsigned int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)str[6]];
      /*FALLTHROUGH*/
      case 6:
      case 5:
      case 4:
      case 3:
        hval += asso_values[(unsigned char)str[2]];
      /*FALLTHROUGH*/
      case 2:
      case 1:
        hval += asso_values[(unsigned char)str[0]];
        break;
    }
  return hval + asso_values[(unsigned char)str[len - 1]];
}

struct gperfSipHdrName *
gperfSipHdrLookup (register const char *str, register size_t len)
{
	//printf("debug, hello\n");
  static struct gperfSipHdrName wordlist[] =
    {
//#line 138 "test.gperf"
      {"DATE", SIP_HDR_DATE},
//#line 141 "test.gperf"
      {"EVENT", SIP_HDR_EVENT},
//#line 200 "test.gperf"
      {"RSEQ", SIP_HDR_RSEQ},
//#line 195 "test.gperf"
      {"REQUIRE", SIP_HDR_REQUIRE},
//#line 137 "test.gperf"
      {"CSEQ", SIP_HDR_CSEQ},
//#line 206 "test.gperf"
      {"SESSION-EXPIRES", SIP_HDR_SESSION_EXPIRES},
//#line 204 "test.gperf"
      {"SERVER", SIP_HDR_SERVER},
//#line 131 "test.gperf"
      {"CONTACT", SIP_HDR_CONTACT},
//#line 205 "test.gperf"
      {"SERVICE-ROUTE", SIP_HDR_SERVICE_ROUTE},
//#line 185 "test.gperf"
      {"RACK", SIP_HDR_RACK},
//#line 139 "test.gperf"
      {"ENCRYPTION", SIP_HDR_ENCRYPTION},
//#line 136 "test.gperf"
      {"CONTENT-TYPE", SIP_HDR_CONTENT_TYPE},
//#line 194 "test.gperf"
      {"REQUEST-DISPOSITION", SIP_HDR_REQUEST_DISPOSITION},
//#line 201 "test.gperf"
      {"SECURITY-CLIENT", SIP_HDR_SECURITY_CLIENT},
//#line 134 "test.gperf"
      {"CONTENT-LANGUAGE", SIP_HDR_CONTENT_LANGUAGE},
//#line 197 "test.gperf"
      {"RESPONSE-KEY", SIP_HDR_RESPONSE_KEY},
//#line 132 "test.gperf"
      {"CONTENT-DISPOSITION", SIP_HDR_CONTENT_DISPOSITION},
//#line 202 "test.gperf"
      {"SECURITY-SERVER", SIP_HDR_SECURITY_SERVER},
//#line 186 "test.gperf"
      {"REASON", SIP_HDR_REASON},
//#line 142 "test.gperf"
      {"EXPIRES", SIP_HDR_EXPIRES},
//#line 211 "test.gperf"
      {"SUPPORTED", SIP_HDR_SUPPORTED},
//#line 203 "test.gperf"
      {"SECURITY-VERIFY", SIP_HDR_SECURITY_VERIFY},
//#line 118 "test.gperf"
      {"ACCEPT", SIP_HDR_ACCEPT,},
//#line 192 "test.gperf"
      {"REPLACES", SIP_HDR_REPLACES},
//#line 198 "test.gperf"
      {"RETRY-AFTER", SIP_HDR_RETRY_AFTER},
//#line 196 "test.gperf"
      {"RESOURCE-PRIORITY", SIP_HDR_RESOURCE_PRIORITY},
//#line 174 "test.gperf"
      {"P-SERVED-USER", SIP_HDR_P_SERVED_USER},
//#line 168 "test.gperf"
      {"P-DCS-REDIRECT", SIP_HDR_P_DCS_REDIRECT},
//#line 164 "test.gperf"
      {"P-DCS-TRACE-PARTY-ID", SIP_HDR_P_DCS_TRACE_PARTY_ID},
//#line 190 "test.gperf"
      {"REFERRED-BY", SIP_HDR_REFERRED_BY},
//#line 187 "test.gperf"
      {"RECORD-ROUTE", SIP_HDR_RECORD_ROUTE},
//#line 173 "test.gperf"
      {"P-REFUSED-URI-LIST", SIP_HDR_P_REFUSED_URI_LIST},
//#line 145 "test.gperf"
      {"HIDE", SIP_HDR_HIDE},
//#line 199 "test.gperf"
      {"ROUTE", SIP_HDR_ROUTE},
//#line 126 "test.gperf"
      {"ANSWER-MODE", SIP_HDR_ANSWER_MODE},
//#line 161 "test.gperf"
      {"P-CALLED-PARTY-ID", SIP_HDR_P_CALLED_PARTY_ID},
//#line 218 "test.gperf"
      {"VIA", SIP_HDR_VIA},
//#line 176 "test.gperf"
      {"P-VISITED-NETWORK-ID", SIP_HDR_P_VISITED_NETWORK_ID},
//#line 220 "test.gperf"
      {"WWW-AUTHENTICATE", SIP_HDR_WWW_AUTHENTICATE},
//#line 214 "test.gperf"
      {"TO", SIP_HDR_TO},
//#line 147 "test.gperf"
      {"IDENTITY", SIP_HDR_IDENTITY},
//#line 158 "test.gperf"
      {"P-ANSWER-STATE", SIP_HDR_P_ANSWER_STATE},
//#line 215 "test.gperf"
      {"TRIGGER-CONSENT", SIP_HDR_TRIGGER_CONSENT},
//#line 119 "test.gperf"
      {"ACCEPT-CONTACT", SIP_HDR_ACCEPT_CONTACT,},
//#line 121 "test.gperf"
      {"ACCEPT-LANGUAGE", SIP_HDR_ACCEPT_LANGUAGE,},
//#line 155 "test.gperf"
      {"MIN-SE", SIP_HDR_MIN_SE},
//#line 129 "test.gperf"
      {"CALL-ID", SIP_HDR_CALL_ID},
//#line 169 "test.gperf"
      {"P-EARLY-MEDIA", SIP_HDR_P_EARLY_MEDIA},
//#line 135 "test.gperf"
      {"CONTENT-LENGTH", SIP_HDR_CONTENT_LENGTH},
//#line 209 "test.gperf"
      {"SUBJECT", SIP_HDR_SUBJECT},
//#line 128 "test.gperf"
      {"AUTHORIZATION", SIP_HDR_AUTHORIZATION},
//#line 177 "test.gperf"
      {"PATH", SIP_HDR_PATH},
//#line 159 "test.gperf"
      {"P-ASSERTED-IDENTITY", SIP_HDR_P_ASSERTED_IDENTITY},
//#line 171 "test.gperf"
      {"P-PREFERRED-IDENTITY", SIP_HDR_P_PREFERRED_IDENTITY},
//#line 180 "test.gperf"
      {"PRIV-ANSWER-MODE", SIP_HDR_PRIV_ANSWER_MODE},
//#line 125 "test.gperf"
      {"ALLOW-EVENTS", SIP_HDR_ALLOW_EVENTS},
//#line 179 "test.gperf"
      {"PRIORITY", SIP_HDR_PRIORITY},
//#line 122 "test.gperf"
      {"ACCEPT-RESOURCE-PRIORITY", SIP_HDR_ACCEPT_RESOURCE_PRIORITY,},
//#line 167 "test.gperf"
      {"P-DCS-LAES", SIP_HDR_P_DCS_LAES},
//#line 189 "test.gperf"
      {"REFER-TO", SIP_HDR_REFER_TO},
//#line 165 "test.gperf"
      {"P-DCS-OSPS", SIP_HDR_P_DCS_OSPS},
//#line 181 "test.gperf"
      {"PRIVACY", SIP_HDR_PRIVACY},
//#line 193 "test.gperf"
      {"REPLY-TO", SIP_HDR_REPLY_TO},
//#line 127 "test.gperf"
      {"AUTHENTICATION-INFO", SIP_HDR_AUTHENTICATION_INFO},
//#line 124 "test.gperf"
      {"ALLOW", SIP_HDR_ALLOW},
//#line 154 "test.gperf"
      {"MIN-EXPIRES", SIP_HDR_MIN_EXPIRES},
//#line 152 "test.gperf"
      {"MAX-FORWARDS", SIP_HDR_MAX_FORWARDS},
//#line 184 "test.gperf"
      {"PROXY-REQUIRE", SIP_HDR_PROXY_REQUIRE},
//#line 188 "test.gperf"
      {"REFER-SUB", SIP_HDR_REFER_SUB},
//#line 175 "test.gperf"
      {"P-USER-DATABASE", SIP_HDR_P_USER_DATABASE},
//#line 208 "test.gperf"
      {"SIP-IF-MATCH", SIP_HDR_SIP_IF_MATCH},
//#line 191 "test.gperf"
      {"REJECT-CONTACT", SIP_HDR_REJECT_CONTACT},
//#line 216 "test.gperf"
      {"UNSUPPORTED", SIP_HDR_UNSUPPORTED},
//#line 148 "test.gperf"
      {"IDENTITY-INFO", SIP_HDR_IDENTITY_INFO},
//#line 150 "test.gperf"
      {"JOIN", SIP_HDR_JOIN},
//#line 140 "test.gperf"
      {"ERROR-INFO", SIP_HDR_ERROR_INFO},
//#line 160 "test.gperf"
      {"P-ASSOCIATED-URI", SIP_HDR_P_ASSOCIATED_URI},
//#line 172 "test.gperf"
      {"P-PROFILE-KEY", SIP_HDR_P_PROFILE_KEY},
//#line 213 "test.gperf"
      {"TIMESTAMP", SIP_HDR_TIMESTAMP},
//#line 133 "test.gperf"
      {"CONTENT-ENCODING", SIP_HDR_CONTENT_ENCODING},
//#line 182 "test.gperf"
      {"PROXY-AUTHENTICATE", SIP_HDR_PROXY_AUTHENTICATE},
//#line 183 "test.gperf"
      {"PROXY-AUTHORIZATION", SIP_HDR_PROXY_AUTHORIZATION},
//#line 157 "test.gperf"
      {"P-ACCESS-NETWORK-INFO", SIP_HDR_P_ACCESS_NETWORK_INFO},
//#line 146 "test.gperf"
      {"HISTORY-INFO", SIP_HDR_HISTORY_INFO},
//#line 210 "test.gperf"
      {"SUBSCRIPTION-STATE", SIP_HDR_SUBSCRIPTION_STATE},
//#line 130 "test.gperf"
      {"CALL-INFO", SIP_HDR_CALL_INFO},
//#line 123 "test.gperf"
      {"ALERT-INFO", SIP_HDR_ALERT_INFO},
//#line 170 "test.gperf"
      {"P-MEDIA-AUTHORIZATION", SIP_HDR_P_MEDIA_AUTHORIZATION},
//#line 212 "test.gperf"
      {"TARGET-DIALOG", SIP_HDR_TARGET_DIALOG},
//#line 143 "test.gperf"
      {"FLOW-TIMER", SIP_HDR_FLOW_TIMER},
//#line 153 "test.gperf"
      {"MIME-VERSION", SIP_HDR_MIME_VERSION},
//#line 151 "test.gperf"
      {"MAX-BREADTH", SIP_HDR_MAX_BREADTH},
//#line 178 "test.gperf"
      {"PERMISSION-MISSING", SIP_HDR_PERMISSION_MISSING},
//#line 144 "test.gperf"
      {"FROM", SIP_HDR_FROM},
//#line 217 "test.gperf"
      {"USER-AGENT", SIP_HDR_USER_AGENT},
//#line 207 "test.gperf"
      {"SIP-ETAG", SIP_HDR_SIP_ETAG},
//#line 163 "test.gperf"
      {"P-CHARGING-VECTOR", SIP_HDR_P_CHARGING_VECTOR},
//#line 162 "test.gperf"
      {"P-CHARGING-FUNCTION-ADDRESSES", SIP_HDR_P_CHARGING_FUNCTION_ADDRESSES},
//#line 120 "test.gperf"
      {"ACCEPT-ENCODING", SIP_HDR_ACCEPT_ENCODING,},
//#line 156 "test.gperf"
      {"ORGANIZATION", SIP_HDR_ORGANIZATION},
//#line 166 "test.gperf"
      {"P-DCS-BILLING-INFO", SIP_HDR_P_DCS_BILLING_INFO},
//#line 149 "test.gperf"
      {"IN-REPLY-TO", SIP_HDR_IN_REPLY_TO},
//#line 219 "test.gperf"
      {"WARNING", SIP_HDR_WARNING}
    };

  static signed char lookup[] =
    {
       -1,  -1,  -1,  -1,   0,   1,  -1,  -1,  -1,   2,
       -1,  -1,   3,  -1,   4,   5,   6,   7,   8,   9,
       10,  -1,  11,  -1,  12,  13,  14,  15,  -1,  16,
       17,  18,  19,  -1,  20,  21,  22,  -1,  23,  -1,
       -1,  24,  25,  26,  27,  28,  29,  30,  31,  32,
       33,  34,  35,  36,  -1,  37,  38,  39,  40,  41,
       42,  -1,  -1,  -1,  43,  44,  45,  46,  47,  48,
       -1,  -1,  49,  50,  51,  -1,  -1,  -1,  -1,  52,
       53,  54,  55,  56,  57,  58,  -1,  -1,  59,  -1,
       60,  -1,  61,  62,  63,  64,  65,  66,  67,  68,
       69,  -1,  70,  -1,  71,  -1,  72,  -1,  73,  74,
       75,  76,  -1,  77,  78,  -1,  79,  -1,  80,  81,
       -1,  82,  83,  84,  85,  86,  87,  -1,  88,  -1,
       89,  -1,  90,  -1,  -1,  -1,  91,  -1,  92,  93,
       94,  -1,  -1,  95,  -1,  -1,  -1,  96,  -1,  -1,
       -1,  -1,  -1,  -1,  97,  98,  -1,  99,  -1,  -1,
       -1,  -1,  -1, 100,  -1,  -1,  -1,  -1,  -1,  -1,
       -1,  -1,  -1,  -1,  -1,  -1, 101,  -1,  -1,  -1,
       -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
       -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
       -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
       -1,  -1, 102
    };

	//printf("len=%ld, MAX_WORD_LENGTH=%d, MIN_WORD_LENGTH=%d", len, MAX_WORD_LENGTH, MIN_WORD_LENGTH);
  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register unsigned int key = gperfSipHdrHash (str, len);

	  //printf("debug, key=%d, MAX_HASH_VALUE=%d\n", key, MAX_HASH_VALUE);
      if (key <= MAX_HASH_VALUE)
        {
          register int index = lookup[key];

		  //printf("debug, index=%d\n", index);
          if (index >= 0)
            {
              register const char *s = wordlist[index].name;
   			  //printf("debug, s=%s\n", s);

			  if (strlen(s)== len && (((unsigned char)*str ^ (unsigned char)*s) & ~32) == 0 && !gperf_case_strncmp (str, s, len))
                return &wordlist[index];
            }
        }
    }
  return 0;
}
