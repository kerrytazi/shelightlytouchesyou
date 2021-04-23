#ifndef _DRIVER_HELPER_H_
#define _DRIVER_HELPER_H_

#include <cstdint>

class CDriverHelper
{
	void *m_DriverHandle;

	uint32_t ReqVersion() const;

public:

	CDriverHelper();
	~CDriverHelper();

	void ReqReadProcessMemory(void *Pid, void *Addr, size_t Size, void *Out) const;
	void ReqWriteProcessMemory(void *Pid, void *Addr, size_t Size, const void *From) const;
	void *ReqGetModuleBase(void *Pid, const wchar_t *pModuleName, size_t ModuleNameSize) const;

	void *ReqGetModuleBase(void *Pid, const char *pModuleName, size_t ModuleNameSize) const;
	void *ReqGetModuleBase(void *Pid, const wchar_t *pModuleName) const;
	void *ReqGetModuleBase(void *Pid, const char *pModuleName) const;
};

class CDriverProcessHelper
{
	const CDriverHelper &m_Helper;
	void *m_ProcessPid;

public:

	CDriverProcessHelper(const CDriverHelper &Helper, void *Pid);

	CDriverProcessHelper(const CDriverHelper &Helper, const wchar_t *pProcName, size_t ProcNameSize);

	CDriverProcessHelper(const CDriverHelper &Helper, const char *pProcName, size_t ProcNameSize);
	CDriverProcessHelper(const CDriverHelper &Helper, const wchar_t *pProcName);
	CDriverProcessHelper(const CDriverHelper &Helper, const char *pProcName);

	void ReadProcessMemory(void *Addr, size_t Size, void *Out) const;
	void WriteProcessMemory(void *Addr, size_t Size, const void *From) const;

	template <typename T>
	T Read(void *Addr) const
	{
		T Value;
		ReadProcessMemory(Addr, sizeof(T), &Value);
		return Value;
	}

	template <typename T>
	void Write(void *Addr, const T &Value) const
	{
		WriteProcessMemory(Addr, sizeof(T), &Value);
	}

	void *GetModuleBase(const wchar_t *pModuleName, size_t ModuleNameSize) const;

	void *GetModuleBase(const char *pModuleName, size_t ModuleNameSize) const;
	void *GetModuleBase(const wchar_t *pModuleName) const;
	void *GetModuleBase(const char *pModuleName) const;
};


#endif // _DRIVER_HELPER_H_
