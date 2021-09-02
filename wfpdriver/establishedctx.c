#include "public.h"
#include "establishedctx.h"

static NPAGED_LOOKASIDE_LIST	g_establishedList;
static KSPIN_LOCK				g_establishedlock;
static NF_FLOWESTABLISHED_DATA	g_flowesobj;

NTSTATUS establishedctx_init()
{
	NTSTATUS status = STATUS_SUCCESS;

	KeInitializeSpinLock(&g_establishedlock);
	ExInitializeNPagedLookasideList(
		&g_establishedList,
		NULL,
		NULL,
		0,
		sizeof(NF_FLOWESTABLISHED_BUFFER),
		'SWSW',
		0
	);

	KeInitializeSpinLock(&g_flowesobj.lock);
	InitializeListHead(&g_flowesobj.pendedPackets);

	return status;
}

VOID establishedctx_free()
{
	KLOCK_QUEUE_HANDLE lh;
	PNF_FLOWESTABLISHED_BUFFER p_flowdata = NULL;
	sl_lock(&g_establishedlock, &lh);
	while (!IsListEmpty(&g_flowesobj.pendedPackets))
	{
		p_flowdata = RemoveHeadList(&g_flowesobj.pendedPackets);
		sl_unlock(&lh);

		ExFreeToNPagedLookasideList(&g_establishedList, p_flowdata);
		sl_lock(&g_establishedlock, &lh);
	}
	sl_unlock(&lh);
	ExDeleteNPagedLookasideList(&g_establishedList);
}

NTSTATUS establishedctx_pushflowestablishedctx(PVOID64 pBuffer, int lens)
{
	NTSTATUS status = STATUS_SUCCESS;


	return status;
}

NTSTATUS establishedctx_popflowestablishedctx()
{
	NTSTATUS status = STATUS_SUCCESS;


	return status;
}

NF_FLOWESTABLISHED_DATA* establishedctx_get()
{
	return &g_flowesobj;
}