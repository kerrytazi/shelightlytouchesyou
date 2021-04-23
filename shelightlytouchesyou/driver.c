#include "driver.h"

#include "pebhelper.h"
#include "peb64.h"

#include <ntifs.h>
#include <ntddk.h>
#include <wdf.h>

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_UNLOAD DriverUnload;
EVT_WDF_IO_QUEUE_IO_DEFAULT EvtDeviceIoDefault;

typedef NTSTATUS(*QUERY_INFO_PROCESS) (
	_In_ HANDLE ProcessHandle,
	_In_ PROCESSINFOCLASS ProcessInformationClass,
	_Out_ PVOID ProcessInformation,
	_In_ ULONG ProcessInformationLength,
	_Out_opt_ PULONG ReturnLength
);

static QUERY_INFO_PROCESS ZwQueryInformationProcess;

NTSTATUS
CreateCDODevice(
	_In_ WDFDRIVER driverObject
)
{
	NTSTATUS status;
	PWDFDEVICE_INIT init;
	WDFDEVICE device;
	WDFQUEUE queue;
	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_IO_QUEUE_CONFIG ioQConfig;

	PAGED_CODE();

	init = WdfControlDeviceInitAllocate(driverObject, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);

	if (!init)
		return STATUS_INSUFFICIENT_RESOURCES;

	WdfDeviceInitSetIoType(init, WdfDeviceIoBuffered);

	DECLARE_CONST_UNICODE_STRING(MyDeviceName, L"\\Device\\" DRIVER_DEVICE_NAME);
	WdfDeviceInitAssignName(init, &MyDeviceName);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	// TODO: Set type
	// WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);

	status = WdfDeviceCreate(&init, &attributes, &device);

	if (!NT_SUCCESS(status))
		goto M_ERR;

	DECLARE_CONST_UNICODE_STRING(MyDosDeviceName, L"\\DosDevices\\" DRIVER_DEVICE_NAME);
	status = WdfDeviceCreateSymbolicLink(device, &MyDosDeviceName);

	if (!NT_SUCCESS(status))
		goto M_ERR;

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQConfig, WdfIoQueueDispatchSequential);
	ioQConfig.EvtIoDefault = EvtDeviceIoDefault;
	status = WdfIoQueueCreate(device, &ioQConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);

	if (!NT_SUCCESS(status))
		goto M_ERR;

	WdfControlFinishInitializing(device);
	return STATUS_SUCCESS;

M_ERR:
	WdfDeviceInitFree(init);
	return status;
}

NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT  DriverObject,
	_In_ PUNICODE_STRING RegistryPath
)
{
	NTSTATUS status;
	WDFDRIVER driver;
	WDF_DRIVER_CONFIG config;

	PAGED_CODE();

	WDF_DRIVER_CONFIG_INIT(&config, NULL);

	config.DriverInitFlags = WdfDriverInitNonPnpDriver;
	config.EvtDriverUnload = DriverUnload;

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		WDF_NO_OBJECT_ATTRIBUTES,
		&config,
		&driver
	);

	if (!NT_SUCCESS(status))
		goto M_ERR;

	DECLARE_CONST_UNICODE_STRING(RoutineName, L"ZwQueryInformationProcess");
	ZwQueryInformationProcess = (QUERY_INFO_PROCESS)MmGetSystemRoutineAddress((PUNICODE_STRING)&RoutineName);

	if (ZwQueryInformationProcess == NULL)
	{
		status = STATUS_UNSUCCESSFUL;
		goto M_ERR;
	}

	status = InitStaticFuncsPEBWOW64();

	if (!NT_SUCCESS(status))
		goto M_ERR;

	status = CreateCDODevice(driver);

M_ERR:
	return status;
}

VOID
DriverUnload(
	_In_ WDFDRIVER Driver
)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(Driver);
}

NTSTATUS
ProcessRequestVersion(
	_Inout_ struct CResponseVersion *VResponse,
	_Out_ PULONG WroteBytes
)
{
	const struct CResponseVersion version = { DRIVER_VERSION };

	PAGED_CODE();

	RtlCopyMemory(VResponse, &version, sizeof(struct CResponseVersion));

	*WroteBytes = sizeof(struct CResponseVersion);

	return STATUS_SUCCESS;
}

NTSTATUS
ProcessRequestReadProcessMemory(
	_Inout_ struct CRequestReadProcessMemory *RPMRequest,
	_Out_ PULONG WroteBytes
)
{
	NTSTATUS status;
	PEPROCESS process;
	KAPC_STATE apcState;
	ULONG writeBytesPending = (ULONG)RPMRequest->size;

	PAGED_CODE();

	status = PsLookupProcessByProcessId(RPMRequest->pid, &process);

	if (!NT_SUCCESS(status))
		goto M_ERR;

	KeStackAttachProcess(process, &apcState);

	__try
	{
		ProbeForRead(RPMRequest->ptr, RPMRequest->size, 1);
		RtlCopyMemory(RPMRequest, RPMRequest->ptr, RPMRequest->size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		status = STATUS_UNSUCCESSFUL;
	}

	KeUnstackDetachProcess(&apcState);

	ObDereferenceObject(process);

M_ERR:
	*WroteBytes = NT_SUCCESS(status) ? writeBytesPending : 0;

	return status;
}

NTSTATUS
ProcessRequestWriteProcessMemory(
	_Inout_ struct CRequestWriteProcessMemory *WPMRequest
)
{
	NTSTATUS status;
	PEPROCESS process;
	KAPC_STATE apcState;

	PAGED_CODE();

	status = PsLookupProcessByProcessId(WPMRequest->pid, &process);

	if (!NT_SUCCESS(status))
		goto M_ERR;

	KeStackAttachProcess(process, &apcState);

	__try
	{
		ProbeForWrite(WPMRequest->ptr, WPMRequest->size, 1);
		RtlCopyMemory(WPMRequest->ptr, WPMRequest + 1, WPMRequest->size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		status = STATUS_UNSUCCESSFUL;
	}

	KeUnstackDetachProcess(&apcState);

	ObDereferenceObject(process);

M_ERR:
	return status;
}

NTSTATUS
GetModuleHandleFromProcessPEB(
	_In_ PPEB Peb,
	_In_ PWCHAR ModuleName,
	_In_ SIZE_T ModuleNameSize,
	_Out_ void **Result
)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PLIST_ENTRY inMemoryOrderModuleList;
	PLIST_ENTRY tempListItem;
	UNICODE_STRING searchModuleName;
	PUNICODE_STRING processModuleName;

	PAGED_CODE();

	searchModuleName.Length = (USHORT)ModuleNameSize * 2;
	searchModuleName.MaximumLength = (USHORT)ModuleNameSize * 2;
	searchModuleName.Buffer = ModuleName;

	__try
	{
		inMemoryOrderModuleList = PEB_GetInMemoryOrderModuleList(Peb);
		tempListItem = inMemoryOrderModuleList;

		for (tempListItem = tempListItem->Flink; tempListItem != inMemoryOrderModuleList; tempListItem = tempListItem->Flink)
		{
			processModuleName = LDR_DATA_GetFullDllName((PLDR_DATA_TABLE_ENTRY)tempListItem);

			if (RtlEqualUnicodeString(&searchModuleName, processModuleName, TRUE) == TRUE)
			{
				*Result = LDR_DATA_GetModuleBase((PLDR_DATA_TABLE_ENTRY)tempListItem);

				status = STATUS_SUCCESS;
				break;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		status = STATUS_UNSUCCESSFUL;
	}

	return status;
}

NTSTATUS
GetProcessPEB(
	_In_ HANDLE hProcess,
	_Out_ PPEB *peb
)
{
	NTSTATUS status;
	PROCESS_BASIC_INFORMATION BasicInfo;
	ULONG returenLength = 0;

	PAGED_CODE();

	status = ZwQueryInformationProcess(hProcess,
		ProcessBasicInformation,
		&BasicInfo,
		sizeof(BasicInfo),
		&returenLength
	);

	if (!NT_SUCCESS(status))
		goto M_ERR;

	*peb = BasicInfo.PebBaseAddress;

M_ERR:
	return status;
}

NTSTATUS
ProcessRequestModuleBase(
	_Inout_ struct CRequestModuleBase *MBRequest,
	_Out_ PULONG WroteBytes
)
{
	NTSTATUS status;
	PEPROCESS process;
	HANDLE hProcess;
	PPEB peb;
	void *result;

	PAGED_CODE();

	status = PsLookupProcessByProcessId(MBRequest->pid, &process);

	if (!NT_SUCCESS(status))
		goto M_ERR;

	status = ObOpenObjectByPointer(process, 0, NULL, 0, 0, KernelMode, &hProcess);

	if (!NT_SUCCESS(status))
		goto M_ERR2;

	status = GetProcessPEB(hProcess, &peb);

	if (!NT_SUCCESS(status))
		goto M_ERR2;

	status = GetModuleHandleFromProcessPEB(peb, (PWCHAR)(MBRequest + 1), MBRequest->size, &result);

	if (!NT_SUCCESS(status))
		goto M_ERR2;

	RtlCopyMemory(MBRequest, &result, sizeof(result));

M_ERR2:
	ObDereferenceObject(process);

M_ERR:
	*WroteBytes = NT_SUCCESS(status) ? sizeof(result) : 0;

	return status;
}

NTSTATUS
GetModuleHandleFromProcessPEBWow64(
	_In_ PPEB64 Peb,
	_In_ PWCHAR ModuleName,
	_In_ SIZE_T ModuleNameSize,
	_Out_ void **Result
)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PLIST_ENTRY inMemoryOrderModuleList;
	PLIST_ENTRY tempListItem;
	UNICODE_STRING searchModuleName;
	PUNICODE_STRING processModuleName;

	PAGED_CODE();

	searchModuleName.Length = (USHORT)ModuleNameSize * 2;
	searchModuleName.MaximumLength = (USHORT)ModuleNameSize * 2;
	searchModuleName.Buffer = ModuleName;

	__try
	{
		inMemoryOrderModuleList = PEB64_GetInMemoryOrderModuleList(Peb);
		tempListItem = inMemoryOrderModuleList;

		for (tempListItem = tempListItem->Flink; tempListItem != inMemoryOrderModuleList; tempListItem = tempListItem->Flink)
		{
			processModuleName = LDR_DATA_GetFullDllName((PLDR_DATA_TABLE_ENTRY)tempListItem);

			if (RtlEqualUnicodeString(&searchModuleName, processModuleName, TRUE) == TRUE)
			{
				*Result = LDR_DATA_GetModuleBase((PLDR_DATA_TABLE_ENTRY)tempListItem);

				status = STATUS_SUCCESS;
				break;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		status = STATUS_UNSUCCESSFUL;
	}

	return status;
}

NTSTATUS
ProcessRequestModuleBaseWow64(
	_Inout_ struct CRequestModuleBase *MBRequest,
	_Out_ PULONG WroteBytes
)
{
	NTSTATUS status;
	PEPROCESS process;
	HANDLE hProcess;
	PPEB64 peb;
	void *result;

	PAGED_CODE();

	status = PsLookupProcessByProcessId(MBRequest->pid, &process);

	if (!NT_SUCCESS(status))
		goto M_ERR;

	status = ObOpenObjectByPointer(process, 0, NULL, 0, 0, KernelMode, &hProcess);

	if (!NT_SUCCESS(status))
		goto M_ERR2;

	status = GetProcessPEBWow64(hProcess, &peb);

	if (!NT_SUCCESS(status))
		goto M_ERR2;

	status = GetModuleHandleFromProcessPEBWow64(peb, (PWCHAR)(MBRequest + 1), MBRequest->size, &result);

	if (!NT_SUCCESS(status))
		goto M_ERR2;

	RtlCopyMemory(MBRequest, &result, sizeof(result));

M_ERR2:
	ObDereferenceObject(process);

M_ERR:
	*WroteBytes = NT_SUCCESS(status) ? sizeof(result) : 0;

	return status;
}

VOID
EvtDeviceIoDefault(
	_In_ WDFQUEUE   Queue,
	_In_ WDFREQUEST Request
)
{
	NTSTATUS status;
	WDF_REQUEST_PARAMETERS params;
	PIRP irp;
	void *buffer;
	ULONG ioControlCode;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(Queue);

	WDF_REQUEST_PARAMETERS_INIT(&params);

	WdfRequestGetParameters(
		Request,
		&params
	);

	if (params.Type != WdfRequestTypeDeviceControl)
	{
		status = STATUS_INVALID_PARAMETER_1;
		goto M_END;
	}

	ioControlCode = params.Parameters.DeviceIoControl.IoControlCode;

	if (METHOD_FROM_CTL_CODE(ioControlCode) != METHOD_BUFFERED)
	{
		status = STATUS_INVALID_PARAMETER_2;
		goto M_END;
	}

	irp = WdfRequestWdmGetIrp(Request);

	buffer = irp->AssociatedIrp.SystemBuffer;

	switch (ioControlCode)
	{
		case CTL_RequestVersion:
			{
				ULONG wroteBytes;

				if (params.Parameters.DeviceIoControl.OutputBufferLength != sizeof(struct CResponseVersion))
				{
					status = STATUS_INVALID_BUFFER_SIZE;
					goto M_END;
				}

				status = ProcessRequestVersion(buffer, &wroteBytes);

				irp->IoStatus.Information = wroteBytes;

				goto M_END;
			}

		case CTL_RequestReadProcessMemory:
			{
				ULONG wroteBytes;

				if (params.Parameters.DeviceIoControl.InputBufferLength != sizeof(struct CRequestReadProcessMemory))
				{
					status = STATUS_INVALID_BUFFER_SIZE;
					goto M_END;
				}

				if (params.Parameters.DeviceIoControl.OutputBufferLength != ((struct CRequestReadProcessMemory *)buffer)->size)
				{
					status = STATUS_INVALID_BUFFER_SIZE;
					goto M_END;
				}

				status = ProcessRequestReadProcessMemory(buffer, &wroteBytes);

				irp->IoStatus.Information = wroteBytes;

				goto M_END;
			}

		case CTL_RequestWriteProcessMemory:
			{
				if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(struct CRequestWriteProcessMemory))
				{
					status = STATUS_INVALID_BUFFER_SIZE;
					goto M_END;
				}

				if (params.Parameters.DeviceIoControl.InputBufferLength != sizeof(struct CRequestWriteProcessMemory) + ((struct CRequestWriteProcessMemory *)buffer)->size)
				{
					status = STATUS_INVALID_BUFFER_SIZE;
					goto M_END;
				}

				if (params.Parameters.DeviceIoControl.OutputBufferLength != 0)
				{
					status = STATUS_INVALID_BUFFER_SIZE;
					goto M_END;
				}

				status = ProcessRequestWriteProcessMemory(buffer);

				goto M_END;
			}

		case CTL_RequestModuleBase:
			{
				ULONG wroteBytes;

				if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(struct CRequestModuleBase))
				{
					status = STATUS_INVALID_BUFFER_SIZE;
					goto M_END;
				}

				if (params.Parameters.DeviceIoControl.InputBufferLength != sizeof(struct CRequestModuleBase) + (((struct CRequestModuleBase *)buffer)->size + 1) * sizeof(WCHAR))
				{
					status = STATUS_INVALID_BUFFER_SIZE;
					goto M_END;
				}

				if (params.Parameters.DeviceIoControl.OutputBufferLength != sizeof(PVOID))
				{
					status = STATUS_INVALID_BUFFER_SIZE;
					goto M_END;
				}

				if (IoIs32bitProcess(irp))
					status = ProcessRequestModuleBaseWow64(buffer, &wroteBytes);
				else
					status = ProcessRequestModuleBase(buffer, &wroteBytes);

				irp->IoStatus.Information = wroteBytes;

				goto M_END;
			}

		default:
			status = STATUS_NOT_IMPLEMENTED;
			goto M_END;
	}

M_END:
	WdfRequestComplete(Request, status);
}


