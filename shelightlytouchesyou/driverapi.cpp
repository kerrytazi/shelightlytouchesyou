#include "driverapi.hpp"
#include "driver.h"

#include <windows.h>
#include <tlhelp32.h>
#include <stdexcept>
#include <sstream>

static void *GetPidByName(const wchar_t *pWideProcName, size_t WideProcNameSize)
{
	void *Pid = nullptr;
	PROCESSENTRY32 Entry;
	Entry.dwSize = sizeof(PROCESSENTRY32);

	HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

	if (Process32First(Snapshot, &Entry) == TRUE)
	{
		while (Process32Next(Snapshot, &Entry) == TRUE)
		{
			if (_wcsnicmp(Entry.szExeFile, pWideProcName, WideProcNameSize) == 0)
			{
				Pid = reinterpret_cast<void *>(static_cast<size_t>(Entry.th32ProcessID));
			}
		}
	}

	CloseHandle(Snapshot);

	return Pid;
}

// Free allocated memory on exception
struct CDeleteOnExit
{
	void *ptr = nullptr;

	void *Alloc(size_t Size) { return ptr = ::operator new(Size); }

	~CDeleteOnExit()
	{
		::operator delete(ptr);
	}
};

// Optimize memory allocation for small buffers
struct CSmallDeleteOnExit
{
	char SmallBuffer[256];
	CDeleteOnExit Scope;

	void *Alloc(size_t Size)
	{
		if (Size < sizeof(SmallBuffer))
			return static_cast<void *>(SmallBuffer);

		return Scope.Alloc(Size);
	}

	template <typename T>
	T *Alloc(size_t Size)
	{
		return static_cast<T *>(Alloc(Size));
	}
};

CDriverHelper::CDriverHelper()
{
	m_DriverHandle = CreateFileW(L"\\\\.\\" DRIVER_DEVICE_NAME, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

	if (m_DriverHandle == INVALID_HANDLE_VALUE)
	{
		throw std::runtime_error("Can't connect to driver");
	}
}

CDriverHelper::~CDriverHelper()
{
	CloseHandle(m_DriverHandle);
}

uint32_t CDriverHelper::ReqVersion() const
{
	CResponseVersion Response;

	DWORD Wrote = 0;

	BOOL result = DeviceIoControl(m_DriverHandle, CTL_RequestVersion, NULL, 0, &Response, sizeof(Response), &Wrote, NULL);

	if (result == 0)
	{
		std::stringstream ss;
		ss << "ReqVersion Failed GetLastError = ";
		ss << GetLastError();

		throw std::runtime_error(ss.str());
	}

	if (Wrote != sizeof(Response))
	{
		std::stringstream ss;
		ss << "ReqVersion wrote = ";
		ss << Wrote;

		throw std::runtime_error(ss.str());
	}

	if (Response.Version != DRIVER_VERSION)
	{
		std::stringstream ss;
		ss << "ReqVersion version don't match = ";
		ss << Response.Version;

		throw std::runtime_error(ss.str());
	}

	return Response.Version;
}

void CDriverHelper::ReqReadProcessMemory(void *Pid, void *Addr, size_t Size, void *Out) const
{
	CRequestReadProcessMemory Request;
	Request.pid = Pid;
	Request.ptr = Addr;
	Request.size = Size;
	int Response = 0;

	DWORD Wrote = 0;

	BOOL result = DeviceIoControl(m_DriverHandle, CTL_RequestReadProcessMemory, &Request, sizeof(Request), Out, (DWORD)Size, &Wrote, NULL);

	if (result == 0)
	{
		std::stringstream ss;
		ss << "ReqReadProcessMemory Failed GetLastError = ";
		ss << GetLastError();

		throw std::runtime_error(ss.str());
	}

	if (Wrote != sizeof(Response))
	{
		std::stringstream ss;
		ss << "ReqReadProcessMemory wrote = ";
		ss << Wrote;

		throw std::runtime_error(ss.str());
	}
}

void CDriverHelper::ReqWriteProcessMemory(void *Pid, void *Addr, size_t Size, const void *From) const
{
	CSmallDeleteOnExit ScopeBuffer;
	size_t TotalSize = sizeof(CRequestWriteProcessMemory) + Size;

	CRequestWriteProcessMemory *pRequest = ScopeBuffer.Alloc<CRequestWriteProcessMemory>(TotalSize);

	pRequest->pid = Pid;
	pRequest->ptr = Addr;
	pRequest->size = Size;

	memcpy(pRequest + 1, From, Size);

	BOOL result = DeviceIoControl(m_DriverHandle, CTL_RequestWriteProcessMemory, pRequest, (DWORD)TotalSize, NULL, 0, NULL, NULL);

	if (result == 0)
	{
		std::stringstream ss;
		ss << "ReqWriteProcessMemory Failed GetLastError = ";
		ss << GetLastError();

		throw std::runtime_error(ss.str());
	}
}

void *CDriverHelper::ReqGetModuleBase(void *Pid, const wchar_t *pWideModuleName, size_t WideModuleNameSize) const
{
	CSmallDeleteOnExit ScopeBuffer;
	size_t StrWideSize = (WideModuleNameSize + 1) * sizeof(wchar_t);
	size_t TotalSize = sizeof(CRequestModuleBase) + StrWideSize;

	CRequestModuleBase *pRequest = ScopeBuffer.Alloc<CRequestModuleBase>(TotalSize);

	pRequest->pid = Pid;
	pRequest->size = WideModuleNameSize;

	memcpy(pRequest + 1, pWideModuleName, StrWideSize);

	DWORD Wrote = 0;
	void *Response = nullptr;

	BOOL result = DeviceIoControl(m_DriverHandle, CTL_RequestModuleBase, pRequest, (DWORD)TotalSize, &Response, sizeof(Response), &Wrote, NULL);

	if (result == 0)
	{
		std::stringstream ss;
		ss << "ReqGetModuleBase Failed GetLastError = ";
		ss << GetLastError();

		throw std::runtime_error(ss.str());
	}

	if (Wrote != sizeof(Response))
	{
		std::stringstream ss;
		ss << "ReqGetModuleBase wrote = ";
		ss << Wrote;

		throw std::runtime_error(ss.str());
	}

	return Response;
}

void *CDriverHelper::ReqGetModuleBase(void *Pid, const char *pModuleName, size_t ModuleNameSize) const
{
	CSmallDeleteOnExit ScopeBuffer;

	size_t WideModuleNameSize = MultiByteToWideChar(CP_UTF8, 0, pModuleName, (DWORD)ModuleNameSize, nullptr, 0);

	wchar_t *pWideModuleName = ScopeBuffer.Alloc<wchar_t>(WideModuleNameSize * 2);

	MultiByteToWideChar(CP_UTF8, 0, pModuleName, (DWORD)ModuleNameSize, pWideModuleName, (DWORD)WideModuleNameSize);

	return ReqGetModuleBase(Pid, pWideModuleName, WideModuleNameSize);
}

void *CDriverHelper::ReqGetModuleBase(void *Pid, const wchar_t *pWideModuleName) const
{
	size_t WideModuleNameSize = wcslen(pWideModuleName);
	return ReqGetModuleBase(Pid, pWideModuleName, WideModuleNameSize);
}

void *CDriverHelper::ReqGetModuleBase(void *Pid, const char *pModuleName) const
{
	size_t ModuleNameSize = strlen(pModuleName);
	return ReqGetModuleBase(Pid, pModuleName, ModuleNameSize);
}

CDriverProcessHelper::CDriverProcessHelper(const CDriverHelper &Helper, void *Pid) :
	m_Helper(Helper),
	m_ProcessPid(Pid)
{
}

CDriverProcessHelper::CDriverProcessHelper(const CDriverHelper &Helper, const wchar_t *pWideProcName, size_t WideProcNameSize) :
	m_Helper(Helper)
{
	m_ProcessPid = GetPidByName(pWideProcName, WideProcNameSize);

	if (m_ProcessPid == nullptr)
	{
		CSmallDeleteOnExit ScopeBuffer;

		size_t ProcNameSize = WideCharToMultiByte(CP_UTF8, 0, pWideProcName, (DWORD)WideProcNameSize, nullptr, 0, nullptr, nullptr);

		char *pProcName = ScopeBuffer.Alloc<char>(ProcNameSize);

		WideCharToMultiByte(CP_UTF8, 0, pWideProcName, (DWORD)WideProcNameSize, pProcName, (DWORD)ProcNameSize, nullptr, nullptr);

		std::stringstream ss;
		ss << "CDriverProcessHelper Can't find process by name ";
		ss << pProcName;

		throw std::runtime_error(ss.str());
	}
}

CDriverProcessHelper::CDriverProcessHelper(const CDriverHelper &Helper, const char *pProcName, size_t ProcNameSize) :
	m_Helper(Helper)
{
	CSmallDeleteOnExit ScopeBuffer;

	size_t WideProcNameSize = MultiByteToWideChar(CP_UTF8, 0, pProcName, (DWORD)ProcNameSize, nullptr, 0);

	wchar_t *pWideProcName = ScopeBuffer.Alloc<wchar_t>(WideProcNameSize * 2);

	MultiByteToWideChar(CP_UTF8, 0, pProcName, (DWORD)ProcNameSize, pWideProcName, (DWORD)WideProcNameSize);

	m_ProcessPid = GetPidByName(pWideProcName, WideProcNameSize);

	if (m_ProcessPid == nullptr)
	{
		std::stringstream ss;
		ss << "CDriverProcessHelper Can't find process by name ";
		ss << pProcName;

		throw std::runtime_error(ss.str());
	}
}

CDriverProcessHelper::CDriverProcessHelper(const CDriverHelper &Helper, const wchar_t *pWideProcName) :
	CDriverProcessHelper(Helper, pWideProcName, wcslen(pWideProcName))
{
}

CDriverProcessHelper::CDriverProcessHelper(const CDriverHelper &Helper, const char *pProcName) :
	CDriverProcessHelper(Helper, pProcName, strlen(pProcName))
{
}

void CDriverProcessHelper::ReadProcessMemory(void *Addr, size_t Size, void *Out) const
{
	m_Helper.ReqReadProcessMemory(m_ProcessPid, Addr, Size, Out);
}

void CDriverProcessHelper::WriteProcessMemory(void *Addr, size_t Size, const void *From) const
{
	m_Helper.ReqWriteProcessMemory(m_ProcessPid, Addr, Size, From);
}

void *CDriverProcessHelper::GetModuleBase(const wchar_t *pWideModuleName, size_t WideModuleNameSize) const
{
	return m_Helper.ReqGetModuleBase(m_ProcessPid, pWideModuleName, WideModuleNameSize);
}

void *CDriverProcessHelper::GetModuleBase(const char *pModuleName, size_t ModuleNameSize) const
{
	return m_Helper.ReqGetModuleBase(m_ProcessPid, pModuleName, ModuleNameSize);
}

void *CDriverProcessHelper::GetModuleBase(const wchar_t *pWideModuleName) const
{
	return m_Helper.ReqGetModuleBase(m_ProcessPid, pWideModuleName);
}

void *CDriverProcessHelper::GetModuleBase(const char *pModuleName) const
{
	return m_Helper.ReqGetModuleBase(m_ProcessPid, pModuleName);
}
