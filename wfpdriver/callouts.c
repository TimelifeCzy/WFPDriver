#include "public.h"
#include "devctrl.h"
#include "datalinkctx.h"
#include "establishedctx.h"
#include "callouts.h"

#include <fwpmk.h>

#pragma warning(push)
#pragma warning(disable:4201)       // unnamed struct/union

#include <fwpsk.h>

#pragma warning(pop)

#include <ws2ipdef.h>
#include <in6addr.h>
#include <ip2string.h>

#define INITGUID
#include <guiddef.h>


static GUID		g_providerGuid;

static GUID		g_calloutGuid_flow_established_v4;
static GUID		g_calloutGuid_flow_established_v6;
static GUID		g_calloutGuid_inbound_mac_etherent;
static GUID		g_calloutGuid_outbound_mac_etherent;
static GUID		g_calloutGuid_inbound_mac_native;
static GUID		g_calloutGuid_outbound_mac_native;

static UINT32	g_calloutId_flow_established_v4;
static UINT32	g_calloutId_flow_established_v6;
static UINT32	g_calloutId_inbound_mac_etherent;
static UINT32	g_calloutId_outbound_mac_etherent;
static UINT32	g_calloutId_inbound_mac_native;
static UINT32	g_calloutId_outbound_mac_native;

static GUID		g_sublayerGuid;
static HANDLE	g_engineHandle = NULL;

DWORD g_monitorflag = 0;

/*
* Callouts Buffer - Established Layer
*/
typedef struct _NF_CALLOUT_FLOWESTABLISHED_INFO
{
	ADDRESS_FAMILY addressFamily;
#pragma warning(push)
#pragma warning(disable: 4201) //NAMELESS_STRUCT_UNION
	union
	{
		FWP_BYTE_ARRAY16 localAddr;
		UINT32 ipv4LocalAddr;
	};
#pragma warning(pop)

	UINT8 protocol;

	UINT64 flowId;
	UINT16 layerId;
	UINT32 calloutId;

	UINT8* toRemoteAddr;
	UINT16 toRemotePort;

	WCHAR  processPath[260];
	UINT64 processId;

	LONG refCount;
}NF_CALLOUT_FLOWESTABLISHED_INFO, * PNF_CALLOUT_FLOWESTABLISHED_INFO;

/*
* Callouts Buffer - DataLink Layer
*/
typedef struct _NF_CALLOUT_MAC_INFO
{
	int code;
}NF_CALLOUT_MAC_INFO, * PNF_CALLOUT_MAC_INFO;

static NPAGED_LOOKASIDE_LIST	g_callouts_flowCtxPacketsLAList;
static KSPIN_LOCK				g_callouts_flowspinlock;

static NPAGED_LOOKASIDE_LIST	g_callouts_datalinkPacktsList;
static KSPIN_LOCK				g_callouts_datalinkspinlock;

#define NFSDK_SUBLAYER_NAME L"Dark Sublayer"
#define NFSDK_RECV_SUBLAYER_NAME L"Dark Recv Sublayer"
#define NFSDK_PROVIDER_NAME L"Dark Provider"

/*	=============================================
				callouts callbacks
	============================================= */
VOID 
helper_callout_classFn_flowEstablished(
	_In_ const FWPS_INCOMING_VALUES0* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES0* inMetaValues,
	_Inout_opt_ void* layerData,
	_In_opt_ const void* classifyContext,
	_In_ const FWPS_FILTER3* filter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT0* classifyOut
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	KLOCK_QUEUE_HANDLE lh;
	PNF_CALLOUT_FLOWESTABLISHED_INFO flowContextLocal = NULL;
	
	// �رռ�ص�ʱ�򣬲����κβ���
	if (g_monitorflag == 0)
	{
		classifyOut->actionType = FWP_ACTION_PERMIT;
		if (filter->flags & FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT)
		{
			classifyOut->flags &= ~FWPS_RIGHT_ACTION_WRITE;
		}
		return;
	}

	flowContextLocal = (PNF_CALLOUT_FLOWESTABLISHED_INFO)ExAllocateFromNPagedLookasideList(&g_callouts_flowCtxPacketsLAList);
	if (flowContextLocal == NULL)
	{
		status = STATUS_NO_MEMORY;
		goto Exit;
	}
	RtlSecureZeroMemory(flowContextLocal, sizeof(NF_CALLOUT_FLOWESTABLISHED_INFO));

	flowContextLocal->refCount = 1;
	// ����Э��
	flowContextLocal->addressFamily= 
		(inFixedValues->layerId == FWPS_LAYER_ALE_FLOW_ESTABLISHED_V4) ? AF_INET : AF_INET6;

	flowContextLocal->flowId = inMetaValues->flowHandle;
	flowContextLocal->layerId = FWPS_LAYER_OUTBOUND_MAC_FRAME_ETHERNET;
	flowContextLocal->calloutId = &g_calloutId_outbound_mac_etherent;

	if (flowContextLocal->addressFamily == AF_INET)
	{
			flowContextLocal->ipv4LocalAddr =
			RtlUlongByteSwap(
				inFixedValues->incomingValue\
				[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_LOCAL_ADDRESS].value.uint32
			);
		flowContextLocal->protocol =
			inFixedValues->incomingValue\
			[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V4_IP_PROTOCOL].value.uint8;
	}
	else
	{
		RtlCopyMemory(
			(UINT8*)&flowContextLocal->localAddr,
			inFixedValues->incomingValue\
			[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V6_IP_LOCAL_ADDRESS].value.byteArray16,
			sizeof(FWP_BYTE_ARRAY16)
		);
		flowContextLocal->protocol =
			inFixedValues->incomingValue\
			[FWPS_FIELD_ALE_FLOW_ESTABLISHED_V6_IP_PROTOCOL].value.uint8;
	}

	// pid - path
	flowContextLocal->processId = inMetaValues->processId;
	memcpy(flowContextLocal->processPath, inMetaValues->processPath->data, inMetaValues->processPath->size);

	// Context Layer to Mac DataLink Layer
	// ������֧����
	//sl_lock(&g_flowspinlock, &lh);
	//status = FwpsFlowAssociateContext(
	//	flowContextLocal->flowId,
	//	flowContextLocal->layerId,
	//	flowContextLocal->calloutId,
	//	(UINT64)flowHead
	//);
	//sl_unlock(&lh);
	//if (!NT_SUCCESS(status))
	//{
	//	goto Exit;
	//}

	DbgBreakPoint();
	establishedctx_pushflowestablishedctx(flowContextLocal, sizeof(NF_CALLOUT_FLOWESTABLISHED_INFO));
	
	classifyOut->actionType = FWP_ACTION_PERMIT;
	if (filter->flags & FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT) 
	{
		classifyOut->flags &= ~FWPS_RIGHT_ACTION_WRITE;
	}

Exit:
	if (flowContextLocal)
	{
		sl_lock(&g_callouts_flowspinlock, &lh);
		ExFreeToNPagedLookasideList(&g_callouts_flowCtxPacketsLAList, flowContextLocal);
		flowContextLocal = NULL;
		sl_unlock(&lh);
	}

	if (!NT_SUCCESS(status))
	{
		classifyOut->actionType = FWP_ACTION_BLOCK;
		classifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE;
	}
}

NTSTATUS 
helper_callout_notifyFn_flowEstablished(
	_In_ FWPS_CALLOUT_NOTIFY_TYPE notifyType,
	_In_ const GUID* filterKey,
	_Inout_ FWPS_FILTER3* filter
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	UNREFERENCED_PARAMETER(notifyType);
	UNREFERENCED_PARAMETER(filter);
	UNREFERENCED_PARAMETER(filterKey);
	return status;
}


VOID
helper_callout_classFn_mac(
	_In_ const FWPS_INCOMING_VALUES* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
	_Inout_opt_ void* layerData,
	_In_opt_ const void* classifyContext,
	_In_ const FWPS_FILTER* filter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT* classifyOut
)
{
	PNF_CALLOUT_MAC_INFO pdatalink_info = NULL;
	KLOCK_QUEUE_HANDLE lh;
	NTSTATUS status = STATUS_SUCCESS;

	// �رռ�ص�ʱ�򣬲����κβ���
	if (g_monitorflag == 0)
	{
		classifyOut->actionType = FWP_ACTION_PERMIT;
		if (filter->flags & FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT)
		{
			classifyOut->flags &= ~FWPS_RIGHT_ACTION_WRITE;
		}
		return;
	}

	if (!layerData)
	{
		status = STATUS_UNSUCCESSFUL;
		goto Exit;
	}

	if (FWPS_LAYER_INBOUND_MAC_FRAME_ETHERNET == inFixedValues->layerId ||
		FWPS_LAYER_OUTBOUND_MAC_FRAME_ETHERNET == inFixedValues->layerId
		)
	{
		pdatalink_info = ExAllocateFromNPagedLookasideList(&g_callouts_datalinkPacktsList);
		if (!pdatalink_info)
		{
			status = STATUS_NO_MEMORY;
			goto Exit;
		}
		
		RtlSecureZeroMemory(pdatalink_info, sizeof(PNF_CALLOUT_MAC_INFO));

		/*
			Mac Buffer Save
		*/
		pdatalink_info->code = 1;

		// push_data to datalink --> devctrl --> read I/O complate to r3
		DbgBreakPoint();
		datalinkctx_pushdata(pdatalink_info, sizeof(PNF_CALLOUT_MAC_INFO));
	}

	classifyOut->actionType = FWP_ACTION_PERMIT;
	if (filter->flags & FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT)
	{
		classifyOut->flags &= ~FWPS_RIGHT_ACTION_WRITE;
	}

Exit:

	if (pdatalink_info)
	{
		sl_lock(&g_callouts_datalinkspinlock, &lh);
		ExFreeToNPagedLookasideList(&g_callouts_datalinkPacktsList, pdatalink_info);
		pdatalink_info = NULL;
		sl_unlock(&lh);
	}

	if (!NT_SUCCESS(status))
	{
		classifyOut->actionType = FWP_ACTION_BLOCK;
		classifyOut->flags |= FWPS_CLASSIFY_OUT_FLAG_ABSORB;
		classifyOut->rights &= ~FWPS_RIGHT_ACTION_WRITE;
	}

}

NTSTATUS
helper_callout_notifyFn_mac(
	_In_ FWPS_CALLOUT_NOTIFY_TYPE notifyType,
	_In_ const GUID* filterKey,
	_Inout_ const FWPS_FILTER* filter
)
{
	NTSTATUS status = STATUS_SUCCESS;
	UNREFERENCED_PARAMETER(filter);
	UNREFERENCED_PARAMETER(filterKey);
	return status;
}

VOID helper_callout_deleteFn_mac(
	IN UINT16 layerId,
	IN UINT32 calloutId,
	IN UINT64 flowContext
)
{
	KLOCK_QUEUE_HANDLE lh;

	UNREFERENCED_PARAMETER(layerId);
	UNREFERENCED_PARAMETER(calloutId);
	UNREFERENCED_PARAMETER(flowContext);
}

/*	=============================================
				callouts Ctrl
	============================================= */
NTSTATUS
helper_callout_registerCallout(
	IN OUT void* deviceObject,
	IN  FWPS_CALLOUT_CLASSIFY_FN classifyFunction,
	IN  FWPS_CALLOUT_NOTIFY_FN notifyFunction,
	IN  FWPS_CALLOUT_FLOW_DELETE_NOTIFY_FN flowDeleteFunction,
	IN  GUID const* calloutKey,
	IN  UINT32 flags,
	OUT UINT32* calloutId
)
{
	NTSTATUS status = STATUS_SUCCESS;
	FWPS_CALLOUT fwpcallout;
	RtlSecureZeroMemory(&fwpcallout, sizeof(FWPS_CALLOUT));

	fwpcallout.calloutKey = *calloutKey;
	fwpcallout.classifyFn = classifyFunction;
	fwpcallout.notifyFn = notifyFunction;
	fwpcallout.flowDeleteFn = flowDeleteFunction;
	fwpcallout.flags = flags;

	status = FwpsCalloutRegister1(deviceObject, (FWPS_CALLOUT1*)&fwpcallout, calloutId);
	return status;
}

NTSTATUS callout_addFlowEstablishedFilter(
	const GUID* calloutKey, 
	const GUID* layer, 
	FWPM_SUBLAYER* subLayer)
{
	NTSTATUS status = STATUS_SUCCESS;
	FWPM_CALLOUT0 fwpcallout0;
	RtlSecureZeroMemory(&fwpcallout0,sizeof(FWPM_CALLOUT0));
	
	FWPM_FILTER fwpfilter;
	RtlSecureZeroMemory(&fwpfilter, sizeof(FWPM_FILTER));

	FWPM_DISPLAY_DATA displayData;
	RtlSecureZeroMemory(&displayData, sizeof(FWPM_DISPLAY_DATA));
	
	FWPM_FILTER_CONDITION filterConditions[3];
	RtlSecureZeroMemory(&filterConditions, sizeof(filterConditions));

	do
	{
		displayData.name = L"Data link Flow Established";
		displayData.description = L"Flow Established Callouts";

		fwpcallout0.displayData = displayData;
		fwpcallout0.applicableLayer = *layer;
		fwpcallout0.calloutKey = *calloutKey;
		fwpcallout0.flags = 0;

		status = FwpmCalloutAdd(g_engineHandle, &fwpcallout0, NULL, NULL);
		if (!NT_SUCCESS(status))
			break;
	
		fwpfilter.subLayerKey = subLayer->subLayerKey;
		fwpfilter.layerKey = *layer;
		fwpfilter.action.calloutKey = *calloutKey;
		fwpfilter.action.type = FWP_ACTION_CALLOUT_TERMINATING;
		fwpfilter.displayData.name = L"Flow Established Callout";
		fwpfilter.displayData.description = L"Flow Established Callout";
		fwpfilter.weight.type = FWP_EMPTY;

		// tcp
		filterConditions[0].conditionValue.type = FWP_UINT8;
		filterConditions[0].conditionValue.uint8 = IPPROTO_TCP;
		filterConditions[0].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
		filterConditions[0].matchType = FWP_MATCH_EQUAL;

		// udp
		filterConditions[1].conditionValue.type = FWP_UINT8;
		filterConditions[1].conditionValue.uint8 = IPPROTO_UDP;
		filterConditions[1].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
		filterConditions[1].matchType = FWP_MATCH_EQUAL;
		
		fwpfilter.filterCondition = filterConditions;
		fwpfilter.numFilterConditions = 2;

		status = FwpmFilterAdd(g_engineHandle, &fwpfilter, NULL, NULL);
		if (!NT_SUCCESS(status))
		{
			break;
		}

	} while (FALSE);

	return status;
}

NTSTATUS callout_addDataLinkMacFilter(
	const GUID* calloutKey,
	const GUID* layer,
	FWPM_SUBLAYER* subLayer,
	const int flaglayer
)
{
	NTSTATUS status = STATUS_SUCCESS;
	FWPM_CALLOUT0 fwpcallout0;
	RtlSecureZeroMemory(&fwpcallout0, sizeof(FWPM_CALLOUT0));

	FWPM_FILTER fwpfilter;
	RtlSecureZeroMemory(&fwpfilter, sizeof(FWPM_FILTER));

	FWPM_DISPLAY_DATA displayData;
	RtlSecureZeroMemory(&displayData, sizeof(FWPM_DISPLAY_DATA));

	FWPM_FILTER_CONDITION filterConditions[3];
	RtlSecureZeroMemory(&filterConditions, sizeof(filterConditions));

	int couts = 0;

	switch (flaglayer)
	{
	case 1:
		displayData.name = L"Sublayer Datalink inbound";
		displayData.description = L"Sublayer Datalink inbound desc";
		fwpfilter.displayData.name = L"Mac inbound Layer Filter";
		fwpfilter.displayData.description = L"Mac inbound Layer Filter desc";
		break;
	case 2:
		displayData.name = L"Sublayer Datalink outbound";
		displayData.description = L"Sublayer Datalink outbound desc";
		fwpfilter.displayData.name = L"Mac outbound Layer Filter";
		fwpfilter.displayData.description = L"Mac outbound Layer Filter desc";
		break;
	case 3:
		break;
	case 4:
		break;
	default:
		break;
	}

	do
	{
		fwpcallout0.displayData = displayData;
		fwpcallout0.applicableLayer = *layer;
		fwpcallout0.calloutKey = *calloutKey;
		fwpcallout0.flags = 0;

		status = FwpmCalloutAdd(g_engineHandle, &fwpcallout0, NULL, NULL);
		if (!NT_SUCCESS(status))
			break;

		fwpfilter.layerKey = *layer;
		fwpfilter.subLayerKey = subLayer->subLayerKey;
		fwpfilter.weight.type = FWP_EMPTY;
		fwpfilter.action.type = FWP_ACTION_CALLOUT_TERMINATING;
		fwpfilter.action.calloutKey = *calloutKey;

		/*
			DataLink Layer
		*/
		filterConditions[couts].conditionValue.type = FWP_UINT16;
		filterConditions[couts].conditionValue.uint16 = NDIS_ETH_TYPE_IPV4;
		filterConditions[couts].fieldKey = FWPM_CONDITION_ETHER_TYPE;
		filterConditions[couts].matchType = FWP_MATCH_EQUAL;
		couts++;

		filterConditions[couts].conditionValue.type = FWP_UINT16;
		filterConditions[couts].conditionValue.uint16 = NDIS_ETH_TYPE_IPV6;
		filterConditions[couts].fieldKey = FWPM_CONDITION_ETHER_TYPE;
		filterConditions[couts].matchType = FWP_MATCH_EQUAL;
		couts++;
		
		fwpfilter.filterCondition = filterConditions;
		fwpfilter.numFilterConditions = couts;

		status = FwpmFilterAdd(g_engineHandle, &fwpfilter, NULL, NULL);
		if (!NT_SUCCESS(status))
		{
			break;
		}

	} while (FALSE);

	return status;
}

NTSTATUS
callouts_addFilters()
{
	NTSTATUS status = STATUS_SUCCESS;
	FWPM_CALLOUT0 fwpcallout;
	SECURITY_DESCRIPTOR secur_tor;
	RtlSecureZeroMemory(&fwpcallout, sizeof(FWPM_CALLOUT0));
	RtlSecureZeroMemory(&secur_tor, sizeof(SECURITY_DESCRIPTOR));
	
	status = FwpmTransactionBegin(g_engineHandle, 0);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	// Add Subyler
	FWPM_SUBLAYER subLayer;
	RtlZeroMemory(&subLayer, sizeof(FWPM_SUBLAYER));

	subLayer.subLayerKey = g_sublayerGuid;
	subLayer.displayData.name = L"Mac SubLayer";
	subLayer.displayData.description = L"Established datalink SubLayer";
	subLayer.flags = 0;
	subLayer.weight = FWP_EMPTY;

	do {
		// register Sublayer
		status = FwpmSubLayerAdd(g_engineHandle, &subLayer, NULL);
		if (!NT_SUCCESS(status))
			break;

		status = callout_addFlowEstablishedFilter(
			&g_calloutGuid_flow_established_v4,
			&FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4,
			&subLayer
		);
		if (!NT_SUCCESS(status))
			break;

		status = callout_addFlowEstablishedFilter(
			&g_calloutGuid_flow_established_v6,
			&FWPM_LAYER_ALE_FLOW_ESTABLISHED_V6,
			&subLayer
		);
		if (!NT_SUCCESS(status))
			break;
		
		status = callout_addDataLinkMacFilter(
			&g_calloutGuid_inbound_mac_etherent,
			&FWPM_LAYER_INBOUND_MAC_FRAME_ETHERNET,
			&subLayer,
			1
		);
		if (!NT_SUCCESS(status))
			break;

		status = callout_addDataLinkMacFilter(
			&g_calloutGuid_outbound_mac_etherent,
			&FWPM_LAYER_OUTBOUND_MAC_FRAME_ETHERNET,
			&subLayer,
			2
		);
		if (!NT_SUCCESS(status))
			break;

		//// FWPM_LAYER_INBOUND_MAC_FRAME_ETHERNET
		//status = callout_addDataLinkMacFilter(&g_calloutGuid_inbound_mac_native, &FWPM_LAYER_INBOUND_MAC_FRAME_NATIVE, &subLayer,3);
		//if (!NT_SUCCESS(status))
		//	break;

		//// FWPM_LAYER_OUTBOUND_MAC_FRAME_ETHERNET
		//status = callout_addDataLinkMacFilter(&g_calloutGuid_outbound_mac_native, &FWPM_LAYER_OUTBOUND_MAC_FRAME_NATIVE, &subLayer,4);
		//if (!NT_SUCCESS(status))
		//	break;

		status = FwpmTransactionCommit(g_engineHandle);
		if (!NT_SUCCESS(status))
			break;

	} while (FALSE);
	
	if (!NT_SUCCESS(status))
	{
		FwpmTransactionAbort(g_engineHandle);
		return status;
	}

	return status;
}

NTSTATUS
callouts_registerCallouts(
	IN OUT void* deviceObject
)
{
	NTSTATUS status = STATUS_SUCCESS;

	status = FwpmTransactionBegin(g_engineHandle, 0);
	if (!NT_SUCCESS(status))
	{
		FwpmEngineClose(g_engineHandle);
		g_engineHandle = NULL;
		return status;
	}

	do
	{
		// FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4
		status = helper_callout_registerCallout(
			deviceObject,
			helper_callout_classFn_flowEstablished,
			helper_callout_notifyFn_flowEstablished,
			NULL,
			&g_calloutGuid_flow_established_v4,
			0,
			g_calloutId_flow_established_v4
		);

		// FWPM_LAYER_ALE_FLOW_ESTABLISHED_V6
		status = helper_callout_registerCallout(
			deviceObject,
			helper_callout_classFn_flowEstablished,
			helper_callout_notifyFn_flowEstablished,
			NULL,
			&g_calloutGuid_flow_established_v6,
			0,
			g_calloutId_flow_established_v6
		);

		// FWPM_LAYER_INBOUND_MAC_FRAME_ETHERNET
		status = helper_callout_registerCallout(
			deviceObject,
			helper_callout_classFn_mac,
			helper_callout_notifyFn_mac,
			helper_callout_deleteFn_mac,
			&g_calloutGuid_inbound_mac_etherent,
			0,
			g_calloutId_inbound_mac_etherent
		);

		// FWPM_LAYER_OUTBOUND_MAC_FRAME_ETHERNET
		status = helper_callout_registerCallout(
			deviceObject,
			helper_callout_classFn_mac,
			helper_callout_notifyFn_mac,
			helper_callout_deleteFn_mac,
			&g_calloutGuid_outbound_mac_etherent,
			0,
			g_calloutId_outbound_mac_etherent
		);

		//// FWPM_LAYER_INBOUND_MAC_FRAME_NATIVE
		//status = helper_callout_registerCallout(
		//	deviceObject,
		//	NULL,
		//	NULL,
		//	NULL,
		//	&g_calloutGuid_inbound_mac_native,
		//	0,
		//	&g_calloutId_inbound_mac_native
		//);

		//// FWPM_LAYER_OUTBOUND_MAC_FRAME_NATIVE
		//status = helper_callout_registerCallout(
		//	deviceObject,
		//	NULL,
		//	NULL,
		//	NULL,
		//	&g_calloutGuid_outbound_mac_native,
		//	0,
		//	&g_calloutId_outbound_mac_native
		//);

		status = FwpmTransactionCommit(g_engineHandle);
		if (!NT_SUCCESS(status))
		{
			break;
		}

	} while (FALSE);

	if (!NT_SUCCESS(status))
	{
		FwpmTransactionAbort(g_engineHandle);
		FwpmEngineClose(g_engineHandle);
		g_engineHandle = NULL;
	}

	return status;
}

BOOLEAN callout_init(
	PDEVICE_OBJECT deviceObject
)
{
	NTSTATUS status = STATUS_SUCCESS;
	DWORD dwStatus = 0;
	FWPM_SESSION0 session;
	RtlSecureZeroMemory(&session, sizeof(FWPM_SESSION0));

	// ����GUID
	ExUuidCreate(&g_calloutGuid_flow_established_v4);
	ExUuidCreate(&g_calloutGuid_flow_established_v6);
	ExUuidCreate(&g_calloutGuid_inbound_mac_etherent);
	ExUuidCreate(&g_calloutGuid_outbound_mac_etherent);
	ExUuidCreate(&g_calloutGuid_inbound_mac_native);
	ExUuidCreate(&g_calloutGuid_outbound_mac_native);
	ExUuidCreate(&g_providerGuid);
	ExUuidCreate(&g_sublayerGuid);

	// Init FlowEstablished 
	KeInitializeSpinLock(&g_callouts_flowspinlock);
	ExInitializeNPagedLookasideList(
		&g_callouts_flowCtxPacketsLAList,
		NULL,
		NULL,
		0,
		sizeof(NF_CALLOUT_FLOWESTABLISHED_INFO),
		'FLHD',
		0
	);

	// Init DataLink
	KeInitializeSpinLock(&g_callouts_datalinkspinlock);
	ExInitializeNPagedLookasideList(
		&g_callouts_datalinkPacktsList,
		NULL,
		NULL,
		0,
		sizeof(NF_CALLOUT_MAC_INFO),
		'FLHD',
		0
	);

	// Open Bfe Engin 
	session.flags = FWPM_SESSION_FLAG_DYNAMIC;
	status = FwpmEngineOpen(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &g_engineHandle);
	if (!NT_SUCCESS(status))
	{
		// KDbgPrint(DPREFIX"FwpmEnginOpen - callout.c line: 34 error");
		return status;
	}

	do {
		FWPM_PROVIDER provider;
		RtlZeroMemory(&provider, sizeof(provider));
		provider.displayData.description = NFSDK_PROVIDER_NAME;
		provider.displayData.name = NFSDK_PROVIDER_NAME;
		provider.providerKey = g_providerGuid;
		dwStatus = FwpmProviderAdd(g_engineHandle, &provider, NULL);
		if (dwStatus != 0)
		{
			status = STATUS_UNSUCCESSFUL;
			break;
		}

		// add/register callout
		status = callouts_registerCallouts(deviceObject);
		if (!NT_SUCCESS(status))
		{
			break;
		}

		// add sublyer & filterctx 
		status = callouts_addFilters();
		if (!NT_SUCCESS(status))
		{
			break;
		}
		
	} while (FALSE);

	if (!NT_SUCCESS(status))
	{
		// callout_free
		FwpmEngineClose(g_engineHandle);
		g_engineHandle = NULL;
	}

	return status;
}

VOID callout_free()
{
	PNF_CALLOUT_FLOWESTABLISHED_INFO	pcalloutsflowCtx = NULL;
	PNF_CALLOUT_MAC_INFO				pcalloutsDatalinkCtx = NULL;

	ExDeleteNPagedLookasideList(&g_callouts_flowCtxPacketsLAList);
	ExDeleteNPagedLookasideList(&g_callouts_datalinkPacktsList);

	// clean guid
	// FwpsCalloutUnregisterByKey(g_engineHandle, &g_calloutGuid_flow_established_v4);
	// FwpsCalloutUnregisterByKey(g_engineHandle, &g_calloutGuid_flow_established_v6);
	FwpsCalloutUnregisterByKey(g_engineHandle, &g_calloutGuid_inbound_mac_etherent);
	FwpsCalloutUnregisterByKey(g_engineHandle, &g_calloutGuid_outbound_mac_etherent);

	// clean SubLayer
	FwpmSubLayerDeleteByKey(g_engineHandle, &g_sublayerGuid);

	// clean c
	FwpmProviderContextDeleteByKey(g_engineHandle,&g_providerGuid);

	if (g_engineHandle)
	{
		FwpmEngineClose(g_engineHandle);
		g_engineHandle = NULL;
	}
}