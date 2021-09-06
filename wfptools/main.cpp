//
// @ 2021/9/4
//  
//
#include <Windows.h>
#include <iostream>
#include "devctrl.h"
#include "nfevents.h"
#include "HlprServerAlpc.h"
#include "nf_api.h"

#include <map>
#include <mutex>
using namespace std;

const char devSyLinkName[] = "\\??\\WFPDark";

typedef struct _PROCESS_INFO
{
	WCHAR  processPath[260];
	UINT64 processId;
}PROCESS_INFO, *PPROCESS_INFO;

static mutex g_mutx;
map<int, NF_CALLOUT_FLOWESTABLISHED_INFO> flowestablished_map;

class EventHandler : public NF_EventHandler
{
	// 捕获 TCP UDP 已建立连接数据
	void establishedPacket(const char* buf, int len) override
	{
		NF_CALLOUT_FLOWESTABLISHED_INFO flowestablished_processinfo;
		RtlSecureZeroMemory(&flowestablished_processinfo, sizeof(NF_CALLOUT_FLOWESTABLISHED_INFO));
		RtlCopyMemory(&flowestablished_processinfo, buf, len);
		
		/*
			TCP - UDP 不同协议相同端口将覆盖，因为需求不需要保存所有的包
		*/
		DWORD keyLocalPort = flowestablished_processinfo.toLocalPort;
		switch (flowestablished_processinfo.protocol)
		{
		case IPPROTO_TCP:
			keyLocalPort += 1000000;
			break;
		case IPPROTO_UDP:
			keyLocalPort += 2000000;
			break;
		}
		g_mutx.lock();
		flowestablished_map[keyLocalPort] = flowestablished_processinfo;
		g_mutx.unlock();

		//// test api 测试是否可以从map获取数据
		//PROCESS_INFO processinfo = { 0, };
		//nf_getprocessinfo(&flowestablished_processinfo.ipv4LocalAddr, flowestablished_processinfo.toLocalPort, flowestablished_processinfo.protocol, &processinfo);
		//processinfo.processId;
		//processinfo.processPath;

		// test path
		wstring wsinfo;
		wsinfo = flowestablished_processinfo.processPath;
		OutputDebugString(wsinfo.data());
	}

	// 捕获 MAC 链路层数据
	void datalinkPacket(const char* buf, int len) override
	{
		printf("%s\r\n", buf);
	}

};

int main(void)
// int nf_init(void)
{
	int status = 0;
	DevctrlIoct devobj;
	EventHandler packtebuff;

	OutputDebugString(L"Entry Main");
	//// Start devctrl workThread
	//status = devobj.devctrl_Alpcworkthread();
	//if (!status)
	//{
	//	cout << "devctrl_workthread error: main.c --> lines: 46" << endl;
	//}

	//// 给线程执行机会 - 创建port
	//Sleep(100);

	//DWORD whiles = 0;
	//// wait driver Connect
	//while (true)
	//{
	//	if (waitDriverConnectAlpcHandle == 100)
	//	{
	//		break;
	//	}
	//	if (whiles == 10000)
	//	{
	//		OutputDebugString(L"Driver Load Timeout");
	//	}
	//	Sleep(1000);
	//	whiles++;
	//}

	// Init devctrl
	status = devobj.devctrl_init();
	if (0 > status)
	{
		cout << "devctrl_init error: main.c --> lines: 19" << endl;
		return -1;
	}

	do 
	{
		// Open driver
		status = devobj.devctrl_opendeviceSylink(devSyLinkName);
		if (0 > status)
		{
			cout << "devctrl_opendeviceSylink error: main.c --> lines: 30" << endl;
			break;
		}

		// Init share Mem
		status = devobj.devctrl_InitshareMem();
		if (0 > status)
		{
			cout << "devctrl_InitshareMem error: main.c --> lines: 38" << endl;
			break;
		}

		status = devobj.devctrl_workthread();
		if (0 > status)
		{
			cout << "devctrl_workthread error: main.c --> lines: 38" << endl;
			break;
		}

		// Enable try Network packte Monitor
		status = devobj.devctrl_OnMonitor();
		if (0 > status)
		{
			cout << "devctrl_InitshareMem error: main.c --> lines: 38" << endl;
			break;
		}

		// Enable Event
		devobj.nf_setWfpCheckEventHandler((PVOID)&packtebuff);
		
		// Wait Thread Exit
		// devobj.devctrl_waitSingeObject();
		status = 1;
	} while (false);

	// 当DLL时候可以注释掉
	//MSG msg;
	//if (0 > status)
	//{
	//	devobj.devctrl_clean();
	//	return 0;
	//}
	//while (GetMessage(&msg, NULL, 0, 0))
	//{
	//	TranslateMessage(&msg);
	//	DispatchMessage(&msg);
	//}
	// devobj.devctrl_clean();

	return status;
}

/*
	@ 参数1 ipv4 address
	@ 参数2 本地端口
	@ 参数3 协议
	@ 参数4 数据指针
*/
int nf_getprocessinfo(
	UINT32* Locaaddripv4, 
	unsigned long localport,
	int protocol,
	PVOID64 getbuffer
)
{
	// -1 参数错误
	if (!Locaaddripv4 && (localport <= 0) && !getbuffer && !protocol)
		return  -1;

	switch (protocol)
	{
	case IPPROTO_TCP:
		localport += 1000000;
		break;
	case IPPROTO_UDP:
		localport += 2000000;
		break;
	}

	try
	{
		PPROCESS_INFO processinf = NULL;
		processinf = (PPROCESS_INFO)getbuffer;
		auto mapiter = flowestablished_map.find(localport);
		// -3 find failuer not`t processinfo
		if (mapiter == flowestablished_map.end())
			return -3;
		processinf->processId = mapiter->second.processId;
		RtlCopyMemory(processinf->processPath, mapiter->second.processPath, mapiter->second.processPathSize);
		return 1;
	}
	catch (const std::exception&)
	{
		// 异常
		return -4;
	}
}

int nf_monitor(
	int code
)
{
	DWORD dSize = 0;
	DWORD ioctcode = 0;

	if (!g_deviceHandle)
		return -1;

	switch (code)
	{
	case 0:
		ioctcode = CTL_DEVCTRL_DISENTABLE_MONITOR;
		break;
	case 1:
		ioctcode = CTL_DEVCTRL_ENABLE_MONITOR;
		break;
	}

	OutputDebugString(L"devctrl_sendioct entablMonitor");
	BOOL status = DeviceIoControl(
		g_deviceHandle,
		ioctcode,
		NULL,
		0,
		NULL,
		0,
		&dSize,
		NULL
	);
	if (!status)
	{
		OutputDebugString(L"devctrl_sendioct Error End");
		return -2;
	}
	return status;
}
