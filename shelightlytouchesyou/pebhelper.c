#include "pebhelper.h"

#include <winternl.h>


PLIST_ENTRY
PEB_GetInMemoryOrderModuleList(
	_In_ PPEB Peb
)
{
	return &Peb->Ldr->InMemoryOrderModuleList;
}

PUNICODE_STRING
LDR_DATA_GetFullDllName(
	_In_ PLDR_DATA_TABLE_ENTRY Ldr
)
{
	return &Ldr->FullDllName;
}

void *
LDR_DATA_GetModuleBase(
	_In_ PLDR_DATA_TABLE_ENTRY Ldr
)
{
	return Ldr->Reserved2[0];
}
