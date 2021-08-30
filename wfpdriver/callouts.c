#include "public.h"
#include "callouts.h"

#include <fwpmk.h>

#pragma warning(push)
#pragma warning(disable:4201)       // unnamed struct/union

#include <fwpsk.h>

#pragma warning(pop)


static GUID		g_providerGuid;

static GUID		g_calloutGuid_flow_established_v4;
static GUID		g_calloutGuid_flow_established_v6;
static GUID		g_calloutGuid_inbound_mac_etherent;
static GUID		g_calloutGuid_outbound_mac_etherent;

static UINT32	g_calloutId_flow_established_v4;
static UINT32	g_calloutId_flow_established_v6;
static UINT32	g_calloutId_inbound_mac_etherent;
static UINT32	g_calloutId_outbound_mac_etherent;

static GUID		g_sublayerGuid;
static HANDLE	g_engineHandle = NULL;


#define NFSDK_SUBLAYER_NAME L"Dark Sublayer"
#define NFSDK_RECV_SUBLAYER_NAME L"Dark Recv Sublayer"
#define NFSDK_PROVIDER_NAME L"Dark Provider"


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


}

NTSTATUS 
helper_callout_notifyFn_flowEstablished(
	_In_ FWPS_CALLOUT_NOTIFY_TYPE notifyType,
	_In_ const GUID* filterKey,
	_Inout_ FWPS_FILTER3* filter
	)
{
	NTSTATUS status = STATUS_SUCCESS;


	return status;
}

VOID 
helper_callout_deletenotifyFn_flowEstablished(
	_In_ UINT16 layerId,
	_In_ UINT32 calloutId,
	_In_ UINT64 flowContext
	)
{


}


VOID
helper_callout_classFn_mac(
	_In_ const FWPS_INCOMING_VALUES0* inFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES0* inMetaValues,
	_Inout_opt_ void* layerData,
	_In_opt_ const void* classifyContext,
	_In_ const FWPS_FILTER3* filter,
	_In_ UINT64 flowContext,
	_Inout_ FWPS_CLASSIFY_OUT0* classifyOut
)
{


}

NTSTATUS
helper_callout_notifyFn_mac(
	_In_ FWPS_CALLOUT_NOTIFY_TYPE notifyType,
	_In_ const GUID* filterKey,
	_Inout_ FWPS_FILTER3* filter
)
{
	NTSTATUS status = STATUS_SUCCESS;


	return status;
}

VOID
helper_callout_deletenotifyFn_mac(
	_In_ UINT16 layerId,
	_In_ UINT32 calloutId,
	_In_ UINT64 flowContext
)
{


}



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
	FWPS_CALLOUT3 fwpcallout3;
	RtlSecureZeroMemory(&fwpcallout3, sizeof(fwpcallout3));

	fwpcallout3.calloutKey = *calloutKey;
	fwpcallout3.classifyFn = classifyFunction;
	fwpcallout3.flags = flags;
	fwpcallout3.flowDeleteFn = flowDeleteFunction;
	fwpcallout3.notifyFn = notifyFunction;

	status = FwpsCalloutRegister(deviceObject, (FWPS_CALLOUT3*)&fwpcallout3, calloutId);
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
			NULL,
			&g_calloutGuid_inbound_mac_etherent,
			0,
			g_calloutId_inbound_mac_etherent
		);

		// FWPM_LAYER_OUTBOUND_MAC_FRAME_ETHERNET
		status = helper_callout_registerCallout(
			deviceObject,
			helper_callout_classFn_mac,
			helper_callout_notifyFn_mac,
			NULL,
			&g_calloutGuid_outbound_mac_etherent,
			0,
			g_calloutId_outbound_mac_etherent
		);

		//// FWPM_LAYER_INBOUND_MAC_FRAME_NATIVE
		//status = helper_callout_registerCallout(
		//	NULL, 
		//	NULL, 
		//	NULL, 
		//	NULL, 
		//	NULL, 
		//	NULL, 
		//	NULL
		//);

		//// FWPM_LAYER_OUTBOUND_MAC_FRAME_NATIVE
		//status = helper_callout_registerCallout(
		//	NULL, 
		//	NULL, 
		//	NULL, 
		//	NULL, 
		//	NULL,
		//	NULL, 
		//	NULL
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


/*
	已建立连接层
*/
NTSTATUS callout_addFlowEstablishedFilter(
	const GUID* calloutKey, 
	const GUID* layer, 
	FWPM_SUBLAYER* subLayer)
{
	NTSTATUS status = STATUS_SUCCESS;
	FWPM_CALLOUT0 fwpcallout0;
	RtlSecureZeroMemory(&fwpcallout0,sizeof(FWPM_CALLOUT0));

	do
	{
		status = FwpmCalloutAdd(g_engineHandle, &fwpcallout0, NULL, NULL);
		if (!NT_SUCCESS(status))
			break;


		status = FwpmFilterAdd();
		if (!NT_SUCCESS(status))
		{
			break;
		}
	} while (FALSE);

	return status;
}

/*
	数据链路层 - MAC
*/
NTSTATUS callout_addDataLinkMacFilter(
	const GUID* calloutKey,
	const GUID* layer,
	FWPM_SUBLAYER* subLayer
)
{

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
	subLayer.displayData.name = "data link SubLayer";
	subLayer.displayData.description = "Mac Data";
	subLayer.flags = 0;
	subLayer.weight = FWP_EMPTY;

	do {
		// register Sublayer
		status = FwpmSubLayerAdd(g_engineHandle, &subLayer, NULL);
		if (!NT_SUCCESS(status))
			break;

		// FWPM_ALE_LAYER_ESTABLISHED_V4
		status = callout_addFlowEstablishedFilter(&g_calloutGuid_flow_established_v4, &FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4, &subLayer);
		if (!NT_SUCCESS(status))
			break;

		// FWPM_ALE_LAYER_ESTABLISHED_V6
		status = callout_addFlowEstablishedFilter(&g_calloutGuid_flow_established_v6, &FWPM_LAYER_ALE_FLOW_ESTABLISHED_V6, &subLayer);
		if (!NT_SUCCESS(status))
			break;
		
		// FWPM_LAYER_INBOUND_MAC_FRAME_ETHERNET
		status = callout_addDataLinkMacFilter(&g_calloutGuid_inbound_mac_etherent, &FWPM_LAYER_INBOUND_MAC_FRAME_ETHERNET, &subLayer);
		if (!NT_SUCCESS(status))
			break;

		// FWPM_LAYER_OUTBOUND_MAC_FRAME_ETHERNET
		status = callout_addDataLinkMacFilter(&g_calloutGuid_outbound_mac_etherent, &FWPM_LAYER_OUTBOUND_MAC_FRAME_ETHERNET, &subLayer);
		if (!NT_SUCCESS(status))
			break;

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

BOOLEAN callout_init(PDEVICE_OBJECT deviceObject)
{
	NTSTATUS status = STATUS_SUCCESS;
	DWORD dwStatus = 0;
	FWPM_SESSION0 session;
	RtlSecureZeroMemory(&session, sizeof(FWPM_SESSION0));

	// 创建GUID
	ExUuidCreate(&g_calloutGuid_flow_established_v4);
	ExUuidCreate(&g_calloutGuid_flow_established_v6);
	ExUuidCreate(&g_calloutGuid_inbound_mac_etherent);
	ExUuidCreate(&g_calloutGuid_outbound_mac_etherent);
	ExUuidCreate(&g_providerGuid);
	ExUuidCreate(&g_sublayerGuid);

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

}