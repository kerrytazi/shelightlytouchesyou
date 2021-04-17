#ifndef _PEB_HELPER_H_
#define _PEB_HELPER_H_

#include <sal.h>

typedef struct _PEB *PPEB;
typedef struct _LIST_ENTRY *PLIST_ENTRY;
typedef struct _LDR_DATA_TABLE_ENTRY *PLDR_DATA_TABLE_ENTRY;
typedef struct _UNICODE_STRING *PUNICODE_STRING;


PLIST_ENTRY
PEB_GetInMemoryOrderModuleList(
	_In_ PPEB Peb
);

PUNICODE_STRING
LDR_DATA_GetFullDllName(
	_In_ PLDR_DATA_TABLE_ENTRY Ldr
);

void *
LDR_DATA_GetModuleBase(
	_In_ PLDR_DATA_TABLE_ENTRY Ldr
);

#endif // _PEB_HELPER_H_
