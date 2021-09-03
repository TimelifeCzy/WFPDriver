// Devctrl.cpp
//		负责和驱动交互
//		处理驱动传递过来的established_layer & mac_frame_layer 数据

#include "devctrl.h"
#include <Windows.h>

#define CTL_DEVCTRL_ENABLE_MONITOR \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_NEITHER, FILE_ANY_ACCESS)
#define CTL_DEVCTRL_STOP_MONITOR \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)
#define CTL_DEVCTRL_OPEN_SHAREMEM \
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef UNALIGNED struct _NF_BUFFERS
{
	unsigned __int64 inBuf;
	unsigned __int64 inBufLen;
	unsigned __int64 outBuf;
	unsigned __int64 outBufLen;
} NF_BUFFERS, * PNF_BUFFERS;

typedef UNALIGNED struct _NF_READ_RESULT
{
	unsigned __int64 length;
} NF_READ_RESULT, * PNF_READ_RESULT;

static NF_BUFFERS	g_nfBuffers;

static HANDLE		g_hDevice;

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
	g_hDevice = NULL;
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

	g_hDevice = hDevice;
	m_devhandler = hDevice;

	return 1;
}

int DevctrlIoct::devctrl_InitshareMem()
{
	DWORD dwBytesReturned = 0;
	memset(&g_nfBuffers, 0, sizeof(g_nfBuffers));
	
	if (!m_devhandler)
		return -1;

	if (!DeviceIoControl(
		m_devhandler,
		CTL_DEVCTRL_OPEN_SHAREMEM,
		NULL, 
		0,
		(LPVOID)&g_nfBuffers, 
		sizeof(g_nfBuffers),
		NULL, 
		NULL))
	{
		if (GetLastError() != ERROR_IO_PENDING)
		{
			return -1;
		}
	}

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

	BOOL status = DeviceIoControl(
		m_devhandler,
		ioctcode,
		NULL,
		0,
		NULL,
		0,
		&dSize,
		NULL
	);
	if (!status)
		return -2;

	return 1;
}

int DevctrlIoct::devctrl_writeio()
{
	return 0;
}

// ReadFile Driver Buffer
static DWORD WINAPI nf_workThread(LPVOID lpThreadParameter)
{
	NF_READ_RESULT rr;

	do 
	{
		if (!g_hDevice)
			break;
		ReadFile(g_hDevice, &rr, sizeof(rr), NULL, &ol);
	
	
	} while (true);

	return 0;
}