#ifndef _PEB_64_H_
#define _PEB_64_H_

#include <ntdef.h>

typedef struct _PEB64 *PPEB64;


NTSTATUS
InitStaticFuncsPEBWOW64();

NTSTATUS
GetProcessPEBWow64(
	_In_ HANDLE hProcess,
	_Out_ PPEB64 *peb
);

PLIST_ENTRY
PEB64_GetInMemoryOrderModuleList(
	_In_ PPEB64 Peb
);

#endif // _PEB_64_H_
