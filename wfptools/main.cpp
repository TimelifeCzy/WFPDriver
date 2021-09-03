//
// @ 2021/9/3
//  
//

#include <Windows.h>
#include <iostream>
#include "devctrl.h"
using namespace std;

const char devSyLinkName[] = "\\??\\WFPDark";

int main(void)
{
	int status = 0;
	DevctrlIoct devobj;

	// Init devctrl
	status = devobj.devctrl_init();
	if (!status)
	{
		cout << "devctrl_init error: main.c --> lines: 19" << endl;
		return -1;
	}

	// Open driver
	status = devobj.devctrl_opendeviceSylink(devSyLinkName);
	if (!status)
	{
		cout << "devctrl_opendeviceSylink error: main.c --> lines: 30" << endl;
		return -1;
	}

	// Start devctrl workThread
	status = devobj.devctrl_workthread();
	if (!status)
	{
		cout << "devctrl_workthread error: main.c --> lines: 38" << endl;
		return -1;
	}
	
	// Wait Thread Exit
	devobj.devctrl_waitSingeObject();

	// clean
	devobj.devctrl_clean();

	return 0;
}