//
// @ 2021/9/3
//  
//
#include <Windows.h>
#include <iostream>
#include "devctrl.h"
#include "nfevents.h"
#include "HlprServerAlpc.h"

using namespace std;

const char devSyLinkName[] = "\\??\\WFPDark";

class EventHandler : public NF_EventHandler
{
	// 捕获 TCP UDP 已建立连接数据
	void establishedPacket(const char* buf, int len) override
	{
		printf("%s\r\n", buf);
	}

	// 捕获 MAC 链路层数据
	void datalinkPacket(const char* buf, int len) override
	{
		printf("%s\r\n", buf);
	}

};

int main(void)
{
	int status = 0;
	DevctrlIoct devobj;
	// EventHandler packtebuff;

	OutputDebugString(L"Entry Main");

	// Start devctrl workThread
	status = devobj.devctrl_Alpcworkthread();
	if (!status)
	{
		cout << "devctrl_workthread error: main.c --> lines: 46" << endl;
	}

	// 给线程执行机会 - 创建port
	Sleep(100);

	DWORD whiles = 0;
	// wait driver Connect
	while (true)
	{
		if (waitDriverConnectAlpcHandle == 100)
		{
			break;
		}
		if (whiles == 10000)
		{
			OutputDebugString(L"Driver Load Timeout");
		}
		Sleep(1000);
		whiles++;
	}

	OutputDebugString(L"Init Connect Success");

	// Init devctrl
	status = devobj.devctrl_init();
	if (!status)
	{
		cout << "devctrl_init error: main.c --> lines: 19" << endl;
		return -1;
	}

	do 
	{
		// Open driver
		status = devobj.devctrl_opendeviceSylink(devSyLinkName);
		if (!status)
		{
			cout << "devctrl_opendeviceSylink error: main.c --> lines: 30" << endl;
			break;
		}

		// Init share Mem
		//status = devobj.devctrl_InitshareMem();
		//if (!status)
		//{
		//	cout << "devctrl_InitshareMem error: main.c --> lines: 38" << endl;
		//	break;
		//}
		//system("pause");

		// Enable try Network packte Monitor
		status = devobj.devctrl_OnMonitor();
		if (!status)
		{
			cout << "devctrl_InitshareMem error: main.c --> lines: 38" << endl;
			break;
		}
		// system("pause");

		// Enable Event
		// devobj.nf_setWfpCheckEventHandler((PVOID)&packtebuff);
		
		// Wait Thread Exit
		// devobj.devctrl_waitSingeObject();
	} while (false);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// clean
	devobj.devctrl_clean();

	return 0;
}