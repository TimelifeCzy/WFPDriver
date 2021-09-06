/*
*	Ӧ�ò����ݽ���
*/
#include "public.h"
#include "devctrl.h"
#include "datalinkctx.h"
#include "establishedctx.h"
#include "HlprDriverAlpc.h"

#define NF_TCP_PACKET_BUF_SIZE 8192
#define NF_UDP_PACKET_BUF_SIZE 2 * 65536

typedef struct _SHARED_MEMORY
{
	PMDL					mdl;
	PVOID					userVa;
	PVOID					kernelVa;
	UINT64					bufferLength;
} SHARED_MEMORY, * PSHARED_MEMORY;

static LIST_ENTRY					g_IoQueryHead;
static NPAGED_LOOKASIDE_LIST		g_IoQueryList;
static LIST_ENTRY					g_pendedIoRequests;
static KSPIN_LOCK					g_sIolock;
static PVOID						g_ioThreadObject = NULL;
static KEVENT						g_ioThreadEvent;
static BOOLEAN						g_shutdown = FALSE;

static SHARED_MEMORY g_inBuf;
static SHARED_MEMORY g_outBuf;

typedef struct _NF_QUEUE_ENTRY
{
	LIST_ENTRY		entry;		// Linkage
	int				code;		// IO code
} NF_QUEUE_ENTRY, * PNF_QUEUE_ENTRY;

void devctrl_ioThread(IN PVOID StartContext);
void devctrl_AlpcThread(IN PVOID StartContext);

NTSTATUS devctrl_create(PIRP irp, PIO_STACK_LOCATION irpSp)
{
	KLOCK_QUEUE_HANDLE lh;
	NTSTATUS 	status = STATUS_SUCCESS;
	HANDLE		pid = PsGetCurrentProcessId();

	UNREFERENCED_PARAMETER(irpSp);

	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}

void devctrl_freeSharedMemory(PSHARED_MEMORY pSharedMemory)
{
	if (pSharedMemory->mdl)
	{
		__try
		{
			if (pSharedMemory->userVa)
			{
				MmUnmapLockedPages(pSharedMemory->userVa, pSharedMemory->mdl);
			}
			if (pSharedMemory->kernelVa)
			{
				MmUnmapLockedPages(pSharedMemory->kernelVa, pSharedMemory->mdl);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}

		MmFreePagesFromMdl(pSharedMemory->mdl);
		IoFreeMdl(pSharedMemory->mdl);

		memset(pSharedMemory, 0, sizeof(SHARED_MEMORY));
	}
}

NTSTATUS devctrl_createSharedMemory(PSHARED_MEMORY pSharedMemory, UINT64 len)
{
	PMDL  mdl;
	PVOID userVa = NULL;
	PVOID kernelVa = NULL;
	PHYSICAL_ADDRESS lowAddress;
	PHYSICAL_ADDRESS highAddress;

	memset(pSharedMemory, 0, sizeof(SHARED_MEMORY));

	lowAddress.QuadPart = 0;
	highAddress.QuadPart = 0xFFFFFFFFFFFFFFFF;

	mdl = MmAllocatePagesForMdl(lowAddress, highAddress, lowAddress, (SIZE_T)len);
	if (!mdl)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	__try
	{
		kernelVa = MmGetSystemAddressForMdlSafe(mdl, HighPagePriority);
		if (!kernelVa)
		{
			MmFreePagesFromMdl(mdl);
			IoFreeMdl(mdl);
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		//
		// The preferred way to map the buffer into user space
		//
		userVa = MmMapLockedPagesSpecifyCache(mdl,          // MDL
			UserMode,     // Mode
			MmCached,     // Caching
			NULL,         // Address
			FALSE,        // Bugcheck?
			HighPagePriority); // Priority
		if (!userVa)
		{
			MmUnmapLockedPages(kernelVa, mdl);
			MmFreePagesFromMdl(mdl);
			IoFreeMdl(mdl);
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}

	//
	// If we get NULL back, the request didn't work.
	// I'm thinkin' that's better than a bug check anyday.
	//
	if (!userVa || !kernelVa)
	{
		if (userVa)
		{
			MmUnmapLockedPages(userVa, mdl);
		}
		if (kernelVa)
		{
			MmUnmapLockedPages(kernelVa, mdl);
		}
		MmFreePagesFromMdl(mdl);
		IoFreeMdl(mdl);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	//
	// Return the allocated pointers
	//
	pSharedMemory->mdl = mdl;
	pSharedMemory->userVa = userVa;
	pSharedMemory->kernelVa = kernelVa;
	pSharedMemory->bufferLength = MmGetMdlByteCount(mdl);

	return STATUS_SUCCESS;
}

NTSTATUS devctrl_openMem(PDEVICE_OBJECT DeviceObject, PIRP irp, PIO_STACK_LOCATION irpSp)
{
	PVOID ioBuffer = NULL;
	ioBuffer = irp->AssociatedIrp.SystemBuffer;
	if (!ioBuffer)
	{
		ioBuffer = irp->UserBuffer;
	}
	ULONG outputBufferLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
	int size = sizeof(NF_BUFFERS);
	if (ioBuffer && (outputBufferLength >= sizeof(NF_BUFFERS)))
	{
		NTSTATUS 	status;

		for (;;)
		{
			if (!g_inBuf.mdl)
			{
				status = devctrl_createSharedMemory(&g_inBuf, NF_UDP_PACKET_BUF_SIZE * 50);
				if (!NT_SUCCESS(status))
				{
					break;
				}
			}

			if (!g_outBuf.mdl)
			{
				status = devctrl_createSharedMemory(&g_outBuf, NF_UDP_PACKET_BUF_SIZE * 2);
				if (!NT_SUCCESS(status))
				{
					break;
				}
			}

			status = STATUS_SUCCESS;

			break;
		}

		if (!NT_SUCCESS(status))
		{
			devctrl_freeSharedMemory(&g_inBuf);
			devctrl_freeSharedMemory(&g_outBuf);
		}
		else
		{
			PNF_BUFFERS pBuffers = (PNF_BUFFERS)ioBuffer;

			pBuffers->inBuf = (UINT64)g_inBuf.userVa;
			pBuffers->inBufLen = g_inBuf.bufferLength;
			pBuffers->outBuf = (UINT64)g_outBuf.userVa;
			pBuffers->outBufLen = g_outBuf.bufferLength;

			irp->IoStatus.Status = STATUS_SUCCESS;
			irp->IoStatus.Information = sizeof(NF_BUFFERS);
			IoCompleteRequest(irp, IO_NO_INCREMENT);

			return STATUS_SUCCESS;
		}
	}

	irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_UNSUCCESSFUL;
}

VOID devctrl_cancelRead(IN PDEVICE_OBJECT deviceObject, IN PIRP irp)
{
	KLOCK_QUEUE_HANDLE lh;

	UNREFERENCED_PARAMETER(deviceObject);

	IoReleaseCancelSpinLock(irp->CancelIrql);

	sl_lock(&g_sIolock, &lh);

	RemoveEntryList(&irp->Tail.Overlay.ListEntry);

	sl_unlock(&lh);

	irp->IoStatus.Status = STATUS_CANCELLED;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
}

NTSTATUS devctrl_read1(PIRP irp, PIO_STACK_LOCATION irpSp)
{
	NTSTATUS status = STATUS_SUCCESS;
	KLOCK_QUEUE_HANDLE lh;

	for (;;)
	{
		if (irp->MdlAddress == NULL)
		{
			KdPrint((DPREFIX"devctrl_read: NULL MDL address\n"));
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		if (MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority) == NULL ||
			irpSp->Parameters.Read.Length < sizeof(NF_READ_RESULT))
		{
			KdPrint((DPREFIX"devctrl_read: Invalid request\n"));
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		sl_lock(&g_sIolock, &lh);

		IoSetCancelRoutine(irp, devctrl_cancelRead);

		if (irp->Cancel &&
			IoSetCancelRoutine(irp, NULL))
		{
			status = STATUS_CANCELLED;
		}
		else
		{
			// pending����
			IoMarkIrpPending(irp);
			InsertTailList(&g_pendedIoRequests, &irp->Tail.Overlay.ListEntry);
			status = STATUS_PENDING;
		}

		sl_unlock(&lh);

		// ������¼�
		KeSetEvent(&g_ioThreadEvent, IO_NO_INCREMENT, FALSE);

		break;
	}

	if (status != STATUS_PENDING)
	{
		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
	}

	return status;
}

NTSTATUS devctrl_read(PIRP irp, PIO_STACK_LOCATION irpSp)
{
	KLOCK_QUEUE_HANDLE lh;
	NTSTATUS 	status = STATUS_SUCCESS;
	HANDLE		pid = PsGetCurrentProcessId();

	UNREFERENCED_PARAMETER(irpSp);

	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}

ULONG devctrl_processRequest(ULONG bufferSize)
{
	PNF_DATA pData = (PNF_DATA)g_outBuf.kernelVa;

	if (bufferSize < (sizeof(NF_DATA) + pData->bufferSize - 1))
	{
		return 0;
	}

	switch (pData->code)
	{
	default:
		break;
	}
	return 0;
}

NTSTATUS devctrl_write(PIRP irp, PIO_STACK_LOCATION irpSp)
{
	PNF_READ_RESULT pRes;
	ULONG bufferLength = irpSp->Parameters.Write.Length;

	pRes = (PNF_READ_RESULT)MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);
	if (!pRes || bufferLength < sizeof(NF_READ_RESULT))
	{
		irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		KdPrint((DPREFIX"devctrl_write invalid irp\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	irp->IoStatus.Information = devctrl_processRequest((ULONG)pRes->length);
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS devctrl_close(PIRP irp, PIO_STACK_LOCATION irpSp)
{
	KLOCK_QUEUE_HANDLE lh;
	NTSTATUS 	status = STATUS_SUCCESS;
	HANDLE		pid = PsGetCurrentProcessId();

	UNREFERENCED_PARAMETER(irpSp);

	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}

NTSTATUS devctrl_setmonitor(IRP* irp, int flag)
{
	// ���ô�ӡ��ǩ
	g_monitorflag = flag;
	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS devctrl_dispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP irp)
{
	PIO_STACK_LOCATION irpSp;

	UNREFERENCED_PARAMETER(DeviceObject);

	irpSp = IoGetCurrentIrpStackLocation(irp);
	ASSERT(irpSp);

	KdPrint((DPREFIX"devctrl_dispatch mj=%d\n", irpSp->MajorFunction));

	switch (irpSp->MajorFunction)
	{
	case IRP_MJ_CREATE:
		return devctrl_create(irp, irpSp);

	case IRP_MJ_READ:
	{
		return devctrl_read1(irp, irpSp);
	}

	case IRP_MJ_WRITE:
		return devctrl_write(irp, irpSp);

	case IRP_MJ_CLOSE:
		return devctrl_close(irp, irpSp);

	case IRP_MJ_DEVICE_CONTROL:
		switch (irpSp->Parameters.DeviceIoControl.IoControlCode)
		{
		case CTL_DEVCTRL_ENABLE_MONITOR:
			return devctrl_setmonitor(irp, 1);

		case CTL_DEVCTRL_STOP_MONITOR:
			return devctrl_setmonitor(irp, 0);

		case CTL_DEVCTRL_OPEN_SHAREMEM:
		{	
			return devctrl_openMem(DeviceObject, irp, irpSp);
		}
		default:
			break;
		}
	}

	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = sizeof(NF_BUFFERS);
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS devctrl_init()
{
	HANDLE threadHandle;
	NTSTATUS status = STATUS_SUCCESS;

	// Init List
	InitializeListHead(&g_pendedIoRequests);
	InitializeListHead(&g_IoQueryHead);
	ExInitializeNPagedLookasideList(&g_IoQueryList, NULL, NULL, 0, sizeof(NF_QUEUE_ENTRY), 'NFQU', 0);
	KeInitializeSpinLock(&g_sIolock);
	
	// Init I/O handler Thread
	KeInitializeEvent(
		&g_ioThreadEvent,
		SynchronizationEvent,
		FALSE
	);

	status = PsCreateSystemThread(
		&threadHandle,
		THREAD_ALL_ACCESS,
		NULL,
		NULL,
		NULL,
		devctrl_ioThread,
		NULL
	);

	//status = PsCreateSystemThread(
	//	&threadHandle,
	//	THREAD_ALL_ACCESS,
	//	NULL,
	//	NULL,
	//	NULL,
	//	devctrl_AlpcThread,
	//	NULL
	//);

	if (NT_SUCCESS(status))
	{
		KPRIORITY priority = HIGH_PRIORITY;

		ZwSetInformationThread(threadHandle, ThreadPriority, &priority, sizeof(priority));

		status = ObReferenceObjectByHandle(
			threadHandle,
			0,
			NULL,
			KernelMode,
			&g_ioThreadObject,
			NULL
		);
		ASSERT(NT_SUCCESS(status));

		ZwClose(threadHandle);
	}

	return status;
}

NTSTATUS devctrl_free()
{
	PNF_QUEUE_ENTRY pQuery = NULL;
	KLOCK_QUEUE_HANDLE lh;
	sl_lock(&g_sIolock, &lh);
	while (!IsListEmpty(&g_IoQueryHead))
	{
		pQuery = RemoveHeadList(&g_IoQueryHead);
		sl_unlock(&lh);

		ExFreeToNPagedLookasideList(&g_IoQueryList, pQuery);
		pQuery = NULL;
		sl_lock(&g_sIolock, &lh);
	}
	sl_unlock(&lh);
	ExDeleteNPagedLookasideList(&g_IoQueryList);

	// thread
	if (g_ioThreadObject)
	{
		KeSetEvent(&g_ioThreadEvent, IO_NO_INCREMENT, FALSE);

		KeWaitForSingleObject(
			g_ioThreadObject,
			Executive,
			KernelMode,
			FALSE,
			NULL
		);

		ObDereferenceObject(g_ioThreadObject);
		g_ioThreadObject = NULL;
	}

	devctrl_freeSharedMemory(&g_inBuf);
	devctrl_freeSharedMemory(&g_outBuf);

	return STATUS_SUCCESS;
}

void devctrl_setShutdown()
{
	KLOCK_QUEUE_HANDLE lh;

	sl_lock(&g_sIolock, &lh);
	g_shutdown = TRUE;
	sl_unlock(&lh);
}

BOOLEAN	devctrl_isShutdown()
{
	BOOLEAN		res;
	KLOCK_QUEUE_HANDLE lh;

	sl_lock(&g_sIolock, &lh);
	res = g_shutdown;
	sl_unlock(&lh);

	return res;
}

NTSTATUS devctrl_pushDataLinkCtxBuffer(int code)
{
	NTSTATUS status = STATUS_SUCCESS;
	PNF_QUEUE_ENTRY pQuery = NULL;
	KLOCK_QUEUE_HANDLE lh;
	// Send to I/O(Read) Buffer
	switch (code)
	{
	case NF_DATALINK_PACKET:
	{
		pQuery = ExAllocateFromNPagedLookasideList(&g_IoQueryList);
		if (!pQuery)
		{
			status = STATUS_UNSUCCESSFUL;
			break;
		}
		pQuery->code = code;
		sl_lock(&g_sIolock, &lh);
		InsertHeadList(&g_IoQueryHead, &pQuery->entry);
		sl_unlock(&lh);
	}
	break;
	default:
		break;
	}

	// keSetEvent
	KeSetEvent(&g_ioThreadEvent, IO_NO_INCREMENT, FALSE);

	return status;
}

NTSTATUS devctrl_pushFlowCtxBuffer(int code)
{
	NTSTATUS status = STATUS_SUCCESS;
	PNF_QUEUE_ENTRY pQuery = NULL;
	KLOCK_QUEUE_HANDLE lh;
	// Send to I/O(Read) Buffer
	switch (code)
	{
	case NF_FLOWCTX_PACKET:
	{
		pQuery = ExAllocateFromNPagedLookasideList(&g_IoQueryList);
		if (!pQuery)
		{
			status = STATUS_UNSUCCESSFUL;
			break;
		}
		pQuery->code = code;
		sl_lock(&g_sIolock, &lh);
		InsertHeadList(&g_IoQueryHead, &pQuery->entry);
		sl_unlock(&lh);
	}
	break;
	default:
		break;
	}
	// keSetEvent
	KeSetEvent(&g_ioThreadEvent, IO_NO_INCREMENT, FALSE);
	return status;
}

NTSTATUS devtrl_popDataLinkData(UINT64* pOffset)
{
	NTSTATUS status = STATUS_SUCCESS;
	PNF_DATALINK_DATA pdatalinkbuf = NULL;
	PNF_DATALINK_BUFFER pEntry = NULL;
	KLOCK_QUEUE_HANDLE lh;
	PNF_DATA	pData;
	UINT64		dataSize = 0;
	ULONG		pPacketlens = 0;

	pdatalinkbuf = datalink_get();
	if (!pdatalinkbuf)
		return STATUS_UNSUCCESSFUL;
	
	sl_lock(&pdatalinkbuf->lock, &lh);
	
	do {

		if (IsListEmpty(&pdatalinkbuf->pendedPackets))
			break;
			
		pEntry = RemoveHeadList(&pdatalinkbuf->pendedPackets);
		dataSize = g_inBuf.bufferLength - *pOffset;
		if ((g_inBuf.bufferLength - *pOffset) < dataSize)
		{
			status = STATUS_NO_MEMORY;
			break;
		}

		pPacketlens = pEntry->dataLength;
		if (!pPacketlens)
		{
			return STATUS_NO_MEMORY;
		}
		dataSize = sizeof(NF_DATA) - 1 + pPacketlens;

		pData = (PNF_DATA)((char*)g_inBuf.kernelVa + *pOffset);

		pData->code = NF_DATALINK_PACKET;
		pData->id = 0;

		if (pEntry->dataBuffer != NULL) {
			memcpy(pData->buffer, &pEntry->dataBuffer, pEntry->dataLength);
		}
		
		*pOffset += dataSize;

	} while (FALSE);

	sl_unlock(&lh);

	if (pEntry)
	{
		if (NT_SUCCESS(status))
		{
			datalinkctx_packfree(pEntry);
		}
		else
		{
			sl_lock(&pdatalinkbuf->lock, &lh);
			InsertHeadList(&pdatalinkbuf->pendedPackets, &pEntry->pEntry);
			sl_unlock(&lh);
		}
	}
	return STATUS_SUCCESS;
}

NTSTATUS devtrl_popFlowestablishedData(UINT64* pOffset)
{
	NTSTATUS status = STATUS_SUCCESS;
	PNF_FLOWESTABLISHED_DATA pestablishedbuf = NULL;
	PNF_FLOWESTABLISHED_BUFFER pEntry = NULL;
	KLOCK_QUEUE_HANDLE lh;
	PNF_DATA	pData;
	UINT64		dataSize = 0;
	ULONG		pPacketlens = 0;

	pestablishedbuf = establishedctx_get();
	if (!pestablishedbuf)
		return STATUS_UNSUCCESSFUL;

	sl_lock(&pestablishedbuf->lock, &lh);
	do {

		if (IsListEmpty(&pestablishedbuf->pendedPackets))
			break;

		pEntry = RemoveHeadList(&pestablishedbuf->pendedPackets);

		dataSize = g_inBuf.bufferLength - *pOffset;
		if ((g_inBuf.bufferLength - *pOffset) < dataSize)
		{
			status = STATUS_NO_MEMORY;
			break;
		}

		pPacketlens = pEntry->dataLength;
		if (!pPacketlens)
		{
			return STATUS_NO_MEMORY;
		}
		dataSize = sizeof(NF_DATA) - 1 + pPacketlens;

		pData = (PNF_DATA)((char*)g_inBuf.kernelVa + *pOffset);

		pData->code = NF_FLOWCTX_PACKET;
		pData->id = 0;

		if (pEntry->dataBuffer != NULL) {
			memcpy(pData->buffer, &pEntry->dataBuffer, pEntry->dataLength);
		}

		*pOffset += dataSize;
	} while (FALSE);

	sl_unlock(&lh);

	if (pEntry)
	{
		if (NT_SUCCESS(status))
		{
			 establishedctx_packfree(pEntry);
		}
		else
		{
			sl_lock(&pestablishedbuf->lock, &lh);
			InsertHeadList(&pestablishedbuf->pendedPackets, &pEntry->pEntry);
			sl_unlock(&lh);
		}
	}
	return STATUS_SUCCESS;
}

UINT64 devctrl_fillBuffer()
{
	PNF_QUEUE_ENTRY	pEntry;
	UINT64		offset = 0;
	NTSTATUS	status = STATUS_SUCCESS;
	KLOCK_QUEUE_HANDLE lh;

	DbgPrint(L"devctrl_fillBuffer");

	sl_lock(&g_sIolock, &lh);

	while (!IsListEmpty(&g_IoQueryHead))
	{
		pEntry = (PNF_QUEUE_ENTRY)RemoveHeadList(&g_IoQueryHead);
		sl_unlock(&lh);

		switch (pEntry->code)
		{
		case NF_DATALINK_PACKET:
		{
			status = devtrl_popDataLinkData(&offset);
		}
		break;
		case NF_FLOWCTX_PACKET:
		{
			// pop flowctx data
			status = devtrl_popFlowestablishedData(&offset);
		}
		break;
		default:
			ASSERT(0);
			status = STATUS_SUCCESS;
		}

		sl_lock(&g_sIolock, &lh);

		if (!NT_SUCCESS(status))
		{
			InsertHeadList(&g_IoQueryHead, &pEntry->entry);
			break;
		}
		ExFreeToNPagedLookasideList(&g_IoQueryList, pEntry);
	}

	sl_unlock(&lh);
	return offset;
}

void devctrl_serviceReads()
{
	PIRP                irp = NULL;
	PLIST_ENTRY         pIrpEntry;
	BOOLEAN             foundPendingIrp = FALSE;
	PNF_READ_RESULT		pResult;
	KLOCK_QUEUE_HANDLE lh;

	sl_lock(&g_sIolock, &lh);

	if (IsListEmpty(&g_pendedIoRequests) || IsListEmpty(&g_IoQueryHead))
	{
		sl_unlock(&lh);
		return;
	}

	pIrpEntry = g_pendedIoRequests.Flink;
	while (pIrpEntry != &g_pendedIoRequests)
	{
		irp = CONTAINING_RECORD(pIrpEntry, IRP, Tail.Overlay.ListEntry);

		if (IoSetCancelRoutine(irp, NULL))
		{
			// �Ƴ�
			RemoveEntryList(pIrpEntry);
			foundPendingIrp = TRUE;
			break;
		}
		else
		{
			KdPrint((DPREFIX"devctrl_serviceReads: skipping cancelled IRP\n"));
			pIrpEntry = pIrpEntry->Flink;
		}
	}

	sl_unlock(&lh);

	if (!foundPendingIrp)
	{
		return;
	}

	pResult = (PNF_READ_RESULT)MmGetSystemAddressForMdlSafe(irp->MdlAddress, HighPagePriority);
	if (!pResult)
	{
		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return;
	}

	pResult->length = devctrl_fillBuffer();

	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = sizeof(NF_READ_RESULT);
	IoCompleteRequest(irp, IO_NO_INCREMENT);

}

void devctrl_ioThread(
	IN PVOID StartContext
)
{
	PIRP                irp = NULL;
	PLIST_ENTRY         pIrpEntry;
	BOOLEAN             foundPendingIrp = FALSE;
	PNF_READ_RESULT		pResult;
	KLOCK_QUEUE_HANDLE lh;

	for (;;)
	{
		KeWaitForSingleObject(
			&g_ioThreadEvent,
			Executive,
			KernelMode,
			FALSE,
			NULL
		);

		if (devctrl_isShutdown())
		{
			break;
		}

		devctrl_serviceReads();
	}

	PsTerminateSystemThread(STATUS_SUCCESS);
}

void devctrl_AlpcThread(
	IN PVOID StartContext
)
{
	PIRP                irp = NULL;
	PLIST_ENTRY         pIrpEntry;
	BOOLEAN             foundPendingIrp = FALSE;
	PNF_READ_RESULT		pResult;
	KLOCK_QUEUE_HANDLE lh;
	PNF_QUEUE_ENTRY		pEntry;

	PNF_FLOWESTABLISHED_DATA pestablishedbuf = NULL;
	PNF_FLOWESTABLISHED_BUFFER pEntryFlowestablishedBuffer = NULL;
	
	PNF_DATALINK_DATA pdatalinkbuf = NULL;
	PNF_DATALINK_BUFFER pEntryDataLinkBuffer = NULL;

	PNF_DATA pData = NULL;

	NTSTATUS status = STATUS_SUCCESS;

	int buffer_total_lens = 0;

	for (;;)
	{
		KeWaitForSingleObject(
			&g_ioThreadEvent,
			Executive,
			KernelMode,
			FALSE,
			NULL
		);

		if (devctrl_isShutdown())
		{
			break;
		}

		sl_lock(&g_sIolock, &lh);

		if (!IsListEmpty(&g_IoQueryHead))
		{
			pEntry = RemoveHeadList(&g_IoQueryHead);
			sl_unlock(&lh);

			switch (pEntry->code)
			{
			case NF_DATALINK_PACKET:
			{
				pdatalinkbuf = datalink_get();
				if (!pdatalinkbuf)
					continue;

				sl_lock(&g_sIolock, &lh);
				do {

					if (IsListEmpty(&pdatalinkbuf->pendedPackets))
						break;
					buffer_total_lens = sizeof(NF_DATA) + pEntryDataLinkBuffer->dataLength;
					pData = ExAllocatePoolWithTag(NonPagedPool, buffer_total_lens, 'SESE');
					if (!pData)
						break;
					RtlSecureZeroMemory(pData, sizeof(NF_DATA));

					pEntryDataLinkBuffer = RemoveHeadList(&pdatalinkbuf->pendedPackets);

					pData->code = NF_DATALINK_PACKET;
					pData->id = 0;
					pData->bufferSize = pEntryDataLinkBuffer->dataLength;

					if (pEntryDataLinkBuffer->dataBuffer != NULL) {
						memcpy(pData->buffer, &pEntryDataLinkBuffer->dataBuffer, pEntryDataLinkBuffer->dataLength);
					}
				} while (FALSE);

				sl_unlock(&lh);

				if (pEntryDataLinkBuffer)
				{
					if (NT_SUCCESS(status))
					{
						datalinkctx_packfree(pEntryDataLinkBuffer);
						pEntryDataLinkBuffer = NULL;
					}
					else
					{
						sl_lock(&pdatalinkbuf->lock, &lh);
						InsertHeadList(&pdatalinkbuf->pendedPackets, &pEntryDataLinkBuffer->pEntry);
						sl_unlock(&lh);
					}
				}
			}
			break;
			case NF_FLOWCTX_PACKET:
			{
				// pop flowctx data
				pestablishedbuf = establishedctx_get();
				if (!pestablishedbuf)
					continue;

				sl_lock(&g_sIolock, &lh);
				do {

					if (IsListEmpty(&pestablishedbuf->pendedPackets))
					{
						status = STATUS_UNSUCCESSFUL;
						break;
					}
					
					pEntryFlowestablishedBuffer = RemoveHeadList(&pestablishedbuf->pendedPackets);
					sl_unlock(&lh);

					buffer_total_lens = sizeof(NF_DATA) + pEntryFlowestablishedBuffer->dataLength;
					pData = ExAllocatePoolWithTag(NonPagedPool, buffer_total_lens, 'SESE');
					if (pData == NULL)
					{
						status = STATUS_UNSUCCESSFUL;
						break;
					}
					RtlSecureZeroMemory(pData, sizeof(NF_DATA) + pEntryFlowestablishedBuffer->dataLength);

					pData->code = NF_FLOWCTX_PACKET;
					pData->bufferSize = pEntryFlowestablishedBuffer->dataLength;

					if (pEntryFlowestablishedBuffer->dataBuffer != NULL) {
						memcpy(pData->buffer, &pEntryFlowestablishedBuffer->dataBuffer, pEntryFlowestablishedBuffer->dataLength);
					}

				} while (FALSE);

				if (pEntryFlowestablishedBuffer)
				{
					if (NT_SUCCESS(status))
					{
						establishedctx_packfree(pEntryFlowestablishedBuffer);
						pEntryFlowestablishedBuffer = NULL;
					}
					else
					{
						sl_lock(&pdatalinkbuf->lock, &lh);
						InsertHeadList(&pestablishedbuf->pendedPackets, &pEntryFlowestablishedBuffer->pEntry);
						sl_unlock(&lh);
					}
				}
			}
			break;
			}
			
			// Clean Query Io Queue
			ExFreeToNPagedLookasideList(&g_IoQueryList, pEntry);
			pEntry = NULL;

			sl_lock(&g_sIolock, &lh);
		}
		
		sl_unlock(&lh);

		// Send Msg to Client(DLL)
		// IRPL BUG
		sl_lock(&g_sIolock, &lh);
		switch (pData->code)
		{
		case NF_DATALINK_PACKET:
		{
			AlpcSendDataLinkStructMsg(pData, buffer_total_lens);
		}
		break;
		case NF_FLOWCTX_PACKET:
		{
			AlpcSendflowEstablishedMsg(pData, buffer_total_lens);
		}
		break;
		}
		sl_unlock(&lh);

		// Clean pData Buffer
		if (pData)
		{
			ExFreePoolWithTag(pData, 'SESE');
			pData = NULL;
		}

		buffer_total_lens = 0;
	}

	PsTerminateSystemThread(STATUS_SUCCESS);
}