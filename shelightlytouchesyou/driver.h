#ifndef _DRIVER_H_
#define _DRIVER_H_

#include <stddef.h>
#include <intsafe.h>

#define DRIVER_DEVICE_NAME L"shelightlytouchesyou"

#define DRIVER_VERSION ((UINT32)2)

#define CTL_RequestVersion            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0800, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define CTL_RequestReadProcessMemory  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0801, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define CTL_RequestWriteProcessMemory CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0802, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define CTL_RequestModuleBase         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0803, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

#ifdef __cplusplus
extern "C" {
#endif

struct CResponseVersion
{
	UINT32 Version;
};

struct CRequestReadProcessMemory
{
	void *pid;
	void *ptr;
	SIZE_T size;
};

/**
 * Must be inherited. Data to write lays right after this struct.
 * Must be packed to get right size of structure.
 *
 * Example:
 * #pragma pack(push, 1)
 * struct CRequestWriteProcessMemoryInt32 : CRequestWriteProcessMemory
 * {
 *     int data; // size = sizeof(int);
 * }
 * #pragma pack(pop)
 */
struct CRequestWriteProcessMemory
{
	void *pid;
	void *ptr;
	SIZE_T size;
};

/**
 * Must be inherited. Module name lays right after this struct.
 * Must be UTF-16 null terminated string.
 * `size` describes count of wchar_t without null terminator.
 * Must be packed to get right size of structure.
 *
 * Example:
 * #pragma pack(push, 1)
 * struct CRequestModuleBaseGame : CRequestModuleBase
 * {
 *     WCHAR Name[9]; // = L"Game.exe"; // size = 8;
 * }
 * #pragma pack(pop)
 */
struct CRequestModuleBase
{
	void *pid;
	SIZE_T size;
};

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_H_
