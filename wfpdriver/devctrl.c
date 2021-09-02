/*
*	应用层数据交互
*/
#include "public.h"
#include "devctrl.h"
#include "datalinkctx.h"

typedef struct _SHARED_MEMORY
{
	PMDL					mdl;
	PVOID					userVa;
	PVOID					kernelVa;
	UINT64		bufferLength;
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

	//case IRP_MJ_READ:
	//	return devctrl_read(irp, irpSp);

	//case IRP_MJ_WRITE:
	//	return devctrl_write(irp, irpSp);

	//case IRP_MJ_CLOSE:
	//	return devctrl_close(irp, irpSp);
	}

	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS devctrl_init()
{
	HANDLE threadHandle;
	NTSTATUS status = STATUS_SUCCESS;
	// Init List
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

	return STATUS_SUCCESS;
}

NTSTATUS devctrl_free()
{
	PNF_QUEUE_ENTRY pQuery = NULL;

	while (!IsListEmpty(&g_IoQueryHead))
	{
		pQuery = RemoveHeadList(&g_IoQueryHead);
		ExFreeToNPagedLookasideList(&g_IoQueryList, pQuery);
		pQuery = NULL;
	}
	ExDeleteNPagedLookasideList(&g_IoQueryList);

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

NTSTATUS devctrl_pushDataLinkCtxBuffer(
	int code
)
{
	NTSTATUS status = STATUS_SUCCESS;
	PNF_QUEUE_ENTRY pQuery = NULL;
	KLOCK_QUEUE_HANDLE lh;
	// Send to I/O(Read) Buffer
	switch (code)
	{
	case NF_DATALINK_SEND:
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
	case NF_DATALINK_INJECT:
	{
	
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
	
	sl_lock(&g_sIolock, &lh);
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

		pData->code = NF_DATALINK_SEND;
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

UINT64 devctrl_fillBuffer()
{
	PNF_QUEUE_ENTRY	pEntry;
	UINT64		offset = 0;
	NTSTATUS	status = STATUS_SUCCESS;
	KLOCK_QUEUE_HANDLE lh;

	sl_lock(&g_sIolock, &lh);

	while (!IsListEmpty(&g_IoQueryHead))
	{
		pEntry = (PNF_QUEUE_ENTRY)RemoveHeadList(&g_IoQueryHead);
		sl_unlock(&lh);

		switch (pEntry->code)
		{
		case NF_DATALINK_SEND:
		{
			status = devtrl_popDataLinkData(&offset);
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

		sl_lock(&g_sIolock, &lh);

		if (!IsListEmpty(&g_IoQueryHead))
		{

			//
			//  Get the first pended Read IRP
			//
			pIrpEntry = g_pendedIoRequests.Flink;
			while (pIrpEntry != &g_pendedIoRequests)
			{
				irp = CONTAINING_RECORD(pIrpEntry, IRP, Tail.Overlay.ListEntry);

				//
				//  Check to see if it is being cancelled.
				//
				if (IoSetCancelRoutine(irp, NULL))
				{
					//
					//  It isn't being cancelled, and can't be cancelled henceforth.
					//
					RemoveEntryList(pIrpEntry);
					foundPendingIrp = TRUE;
					break;
				}
				else
				{
					//
					//  The IRP is being cancelled; let the cancel routine handle it.
					//
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
	}


	PsTerminateSystemThread(STATUS_SUCCESS);
}