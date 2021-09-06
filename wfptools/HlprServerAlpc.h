#pragma once
#pragma comment(lib,"alpc.lib")
#pragma comment(lib,"ntdll.lib")

typedef USHORT ADDRESS_FAMILY;

#define FWP_BYTE_ARRAY6_SIZE 6

extern int waitDriverConnectAlpcHandle;

typedef struct FWP_BYTE_ARRAY16_
{
	UINT8 byteArray16[16];
} 	FWP_BYTE_ARRAY16;

// Exec struct
typedef struct _UNIVERMSG
{
	ULONG ControlId;		// Command function Id
	ULONG Event;			// Event
}UNIVERMSG, *PUNIVERMSG;

// 	DIRVER_INJECT_DLL
typedef struct _DIRVER_INJECT_DLL
{
	UNIVERMSG univermsg;	// ALL Port Analys MSG
	PVOID ImageBase;
	ULONG Pids;
	wchar_t MsgData[10];
}DIRVER_INJECT_DLL, *PDIRVER_INJECT_DLL;

// 	DIRVER_Data_Test
typedef struct _DIRVER_MSG_TEST
{
	UNIVERMSG univermsg;	// ALL Port Analys MSG
	wchar_t MsgData[10];
}DIRVER_MSG_TEST, *PDIRVER_MSG_TEST;

typedef struct _IPPACKHADNER
{
	UNIVERMSG univermsg;	// ALL Port Analys MSG
	ULONG pid;
	ULONG protocol;
	ULONG localaddr;
	ULONG localport;
	ULONG remoteaddr;
	ULONG remoteport;
	ULONG heartbeat;				// 心跳探测 
}IPPACKHANDER, *PIPPACKHANDER;

typedef struct _MONITORCVEINFO
{
	UNIVERMSG univermsg;
	wchar_t cvename[30];	// CVE Name
	int Pid;				// Process Pid
}MONITORCVEINFO, *PMONITORCVEINFO;

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
	UINT16 toLocalPort;

	UINT8 protocol;
	UINT64 flowId;
	UINT16 layerId;
	UINT32 calloutId;

#pragma warning(push)
#pragma warning(disable: 4201) //NAMELESS_STRUCT_UNION
	union
	{
		FWP_BYTE_ARRAY16 RemoteAddr;
		UINT32 ipv4toRemoteAddr;
	};
#pragma warning(pop)
	UINT16 toRemotePort;

	// WCHAR  processPath[260];
	UINT64 processId;

	LONG refCount;

	WCHAR proceepath[1];
}NF_CALLOUT_FLOWESTABLISHED_INFO, * PNF_CALLOUT_FLOWESTABLISHED_INFO;

/*
* Callouts Buffer - DataLink Layer
*/
typedef struct _NF_CALLOUT_MAC_INFO
{
	int code;
}NF_CALLOUT_MAC_INFO, * PNF_CALLOUT_MAC_INFO;

// extern vector<NF_CALLOUT_FLOWESTABLISHED_INFO> flowestablished_list;

void AlpcPortStart(wchar_t* PortName);

void list_thread(wchar_t* PortName);

void AlpcSendtoClientMsg(HANDLE sendPort, UNIVERMSG* univermsg, const int msgid);