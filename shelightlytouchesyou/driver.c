#include "driver.h"

#include <ntifs.h>
#include <ntddk.h>
#include <wdf.h>

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_UNLOAD DriverUnload;
EVT_WDF_IO_QUEUE_IO_DEFAULT EvtDeviceIoDefault;

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
		goto M_END;

	status = CreateCDODevice(driver);

M_END:
	return status;
}

VOID
DriverUnload(
	_In_ WDFDRIVER Driver
)
{
	UNREFERENCED_PARAMETER(Driver);
}

NTSTATUS
ProcessRequestVersion(
	_Inout_ struct CResponseVersion *VResponse,
	_Out_ PULONG WroteBytes
)
{
	const struct CResponseVersion version = { DRIVER_VERSION };

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
	ULONG wroteBytes;

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

		case CTL_RequestGetModuleBase:
			{
				status = STATUS_NOT_IMPLEMENTED;
				goto M_END;
			}

		default:
			status = STATUS_NOT_IMPLEMENTED;
			goto M_END;
	}

M_END:
	WdfRequestComplete(Request, status);
}


