// Devctrl.cpp
//		负责和驱动交互
//		处理驱动传递过来的established_layer & mac_frame_layer 数据

#include "devctrl.h"
#include <Windows.h>

#define CTL_DEVCTRL_ENABLE_MONITOR \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_NEITHER, FILE_ANY_ACCESS)
#define CTL_DEVCTRL_STOP_MONITOR \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)
#define CTL_DEVCTRL_UNINSTALL_MONITOR \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_NEITHER, FILE_ANY_ACCESS)

static DWORD WINAPI nf_workThread(LPVOID lpThreadParameter);

enum IoctCode
{
	NF_TEST_CODE = 1,
};

DevctrlIoct::DevctrlIoct()
{
}

DevctrlIoct::~DevctrlIoct()
{
}

int DevctrlIoct::devctrl_init()
{
	m_devhandler = NULL;
	m_threadobjhandler = NULL;
	m_dwthreadid = 0;
	return 1;
}

int DevctrlIoct::devctrl_workthread()
{
	// start thread
	m_threadobjhandler = CreateThread(
		NULL, 
		0, 
		nf_workThread,
		0, 
		0, 
		&m_dwthreadid
	);
	if (!m_threadobjhandler)
		return 0;
	return 1;
}

int DevctrlIoct::devctrl_opendeviceSylink(const char* devSylinkName)
{
	if (!devSylinkName && (0 >= strlen(devSylinkName)))
		return -1;
	
	// Open Driver
	HANDLE hDevice = CreateFileA(
		devSylinkName,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	if (!hDevice)
		return -1;

	m_devhandler = hDevice;

	return 1;
}

int DevctrlIoct::devctrl_waitSingeObject()
{
	WaitForSingleObject(m_threadobjhandler, INFINITE);
	return 1;
}

void DevctrlIoct::devctrl_clean()
{
	if (m_devhandler)
	{
		this->devctrl_sendioct(CTL_DEVCTRL_UNINSTALL_MONITOR);
		CloseHandle(m_devhandler);
		m_devhandler = NULL;
	}

	if (m_threadobjhandler)
	{
		TerminateThread(m_threadobjhandler, 0);
		CloseHandle(m_threadobjhandler);
		m_threadobjhandler = NULL;
	}
}

int DevctrlIoct::devctrl_sendioct(const int ioctcode)
{
	DWORD dSize = 0;

	if (!m_devhandler)
		return -1;

	BOOL status = DeviceIoControl(m_devhandler, ioctcode, NULL, 0, NULL, 0, &dSize, NULL);
	if (!status)
		return -2;

	return 1;
}

int DevctrlIoct::devctrl_writeio()
{
	return 0;
}

// ReadFile Driver Buffer
DWORD WINAPI nf_workThread(LPVOID lpThreadParameter)
{
	for (;;)
	{
		// ReadFile

		// Dispathch Handler


	}

	return 0;
}