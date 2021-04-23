#include "peb64.h"

#include <ntdef.h>
#include <ntddk.h>

typedef struct _PEB64_LDR_DATA {
	char Reserved1[8];
	PVOID Reserved2[3];
	LIST_ENTRY InMemoryOrderModuleList;
} PEB64_LDR_DATA, *PPEB64_LDR_DATA;

typedef struct _RTL_DRIVE_LETTER_CURDIR
{
	USHORT Flags;
	USHORT Length;
	ULONG TimeStamp;
	STRING DosPath;
} RTL_DRIVE_LETTER_CURDIR, *PRTL_DRIVE_LETTER_CURDIR;

typedef struct _PROCESS_BASIC_INFORMATION_WOW64
{
	NTSTATUS ExitStatus;
	ULONG64  PebBaseAddress;
	ULONG64  AffinityMask;
	KPRIORITY BasePriority;
	ULONG64  UniqueProcessId;
	ULONG64  InheritedFromUniqueProcessId;

} PROCESS_BASIC_INFORMATION_WOW64, *PPROCESS_BASIC_INFORMATION_WOW64;

typedef struct _UNICODE_STRING_WOW64 {
	USHORT Length;
	USHORT MaximumLength;
	PVOID64 Buffer;
} UNICODE_STRING_WOW64;

// PEB 64:
typedef struct _PEB64 {
	char Reserved[16];
	PVOID64 ImageBaseAddress;
	PPEB64_LDR_DATA Ldr;
	PVOID64 ProcessParameters;
} PEB64, *PPEB64;

typedef struct _CURDIR64
{
	UNICODE_STRING_WOW64 DosPath;
	HANDLE Handle;
} CURDIR64, *PCURDIR64;

typedef struct _RTL_USER_PROCESS_PARAMETERS64
{
	ULONG MaximumLength;                            // Should be set before call RtlCreateProcessParameters
	ULONG Length;                                   // Length of valid structure
	ULONG Flags;                                    // Currently only PPF_NORMALIZED (1) is known:
													//  - Means that structure is normalized by call RtlNormalizeProcessParameters
	ULONG DebugFlags;
	PVOID64 ConsoleHandle;                          // HWND to console window associated with process (if any).
	ULONG ConsoleFlags;
	DWORD64 StandardInput;
	DWORD64 StandardOutput;
	DWORD64 StandardError;
	CURDIR64 CurrentDirectory;                      // Specified in DOS-like symbolic link path, ex: "C:/WinNT/SYSTEM32"

	UNICODE_STRING_WOW64 DllPath;                   // DOS-like paths separated by ';' where system should search for DLL files.
	UNICODE_STRING_WOW64 ImagePathName;             // Full path in DOS-like format to process'es file image.
	UNICODE_STRING_WOW64 CommandLine;               // Command line
	PVOID64 Environment;                            // Pointer to environment block (see RtlCreateEnvironment)
	ULONG StartingX;
	ULONG StartingY;
	ULONG CountX;
	ULONG CountY;
	ULONG CountCharsX;
	ULONG CountCharsY;
	ULONG FillAttribute;                            // Fill attribute for console window
	ULONG WindowFlags;
	ULONG ShowWindowFlags;
	UNICODE_STRING_WOW64 WindowTitle;
	UNICODE_STRING_WOW64 DesktopInfo;               // Name of WindowStation and Desktop objects, where process is assigned
	UNICODE_STRING_WOW64 ShellInfo;
	UNICODE_STRING_WOW64 RuntimeData;
	RTL_DRIVE_LETTER_CURDIR CurrentDirectores[0x20];
	ULONG EnvironmentSize;
} RTL_USER_PROCESS_PARAMETERS64, *PRTL_USER_PROCESS_PARAMETERS64;

typedef NTSTATUS(NTAPI *WOW64_QUERY_INFO_PROCESS) (
	_In_ HANDLE ProcessHandle,
	_In_ PROCESSINFOCLASS ProcessInformationClass,
	_Out_ PVOID ProcessInformation,
	_In_ ULONG ProcessInformationLength,
	_Out_opt_ PULONG ReturnLength
);

static WOW64_QUERY_INFO_PROCESS NtWow64QueryInformationProcess64;

NTSTATUS
InitStaticFuncsPEBWOW64()
{
	NTSTATUS status = STATUS_SUCCESS;

	PAGED_CODE();

	DECLARE_CONST_UNICODE_STRING(RoutineName, L"NtWow64QueryInformationProcess64");
	NtWow64QueryInformationProcess64 = (WOW64_QUERY_INFO_PROCESS)MmGetSystemRoutineAddress((PUNICODE_STRING)&RoutineName);

	if (NtWow64QueryInformationProcess64 == NULL)
		status = STATUS_UNSUCCESSFUL;

	return status;
}

NTSTATUS
GetProcessPEBWow64(
	_In_ HANDLE hProcess,
	_Out_ PPEB64 *peb
)
{
	NTSTATUS status;
	PROCESS_BASIC_INFORMATION_WOW64 BasicInfo;
	ULONG returenLength = 0;

	PAGED_CODE();

	status = NtWow64QueryInformationProcess64(hProcess,
		ProcessBasicInformation,
		&BasicInfo,
		sizeof(BasicInfo),
		&returenLength
	);

	if (!NT_SUCCESS(status))
		goto M_ERR;

	*peb = (PPEB64)BasicInfo.PebBaseAddress;

M_ERR:
	return status;
}

PLIST_ENTRY
PEB64_GetInMemoryOrderModuleList(
	_In_ PPEB64 Peb
)
{
	return &Peb->Ldr->InMemoryOrderModuleList;
}
