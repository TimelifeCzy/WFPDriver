//
// @ 2021/9/3
//  
//

#include <Windows.h>
#include <iostream>
#include "devctrl.h"
#include "nfevents.h"

using namespace std;

const char devSyLinkName[] = "\\??\\WFPDark";

class EventHandler : public NF_EventHandler
{
	// ���� TCP UDP �ѽ�����������
	void establishedPacket(const char* buf, int len) override
	{
		printf("%s\r\n", buf);
	}

	// ���� MAC ��·������
	void datalinkPacket(const char* buf, int len) override
	{
		printf("%s\r\n", buf);
	}

};

int main(void)
{
	int status = 0;
	DevctrlIoct devobj;
	EventHandler packtebuff;

	OutputDebugString(L"Entry Main");

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
		status = devobj.devctrl_InitshareMem();
		if (!status)
		{
			cout << "devctrl_InitshareMem error: main.c --> lines: 38" << endl;
			break;
		}
		system("pause");


		// Enable try Network packte Monitor
		status = devobj.devctrl_OnMonitor();
		if (!status)
		{
			cout << "devctrl_InitshareMem error: main.c --> lines: 38" << endl;
			break;
		}
		system("pause");

		// Start devctrl workThread
		status = devobj.devctrl_workthread();
		if (!status)
		{
			cout << "devctrl_workthread error: main.c --> lines: 46" << endl;
			break;
		}
		system("pause");



		// Enable Event
		devobj.nf_setWfpCheckEventHandler((PVOID)&packtebuff);

		system("pause");
		// Wait Thread Exit
		devobj.devctrl_waitSingeObject();
		system("pause");
	} while (false);
	
	// clean
	devobj.devctrl_clean();

	return 0;
}