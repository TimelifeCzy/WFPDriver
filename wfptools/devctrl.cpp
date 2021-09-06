// Devctrl.cpp
//		�������������
//		�����������ݹ�����established_layer & mac_frame_layer ����
#include <Windows.h>

#include "nfdriver.h"
#include "sync.h"
#include "eventQueue.h"
#include "devctrl.h"
#include "nfevents.h"
#include "HlprServerAlpc.h"

#define TCP_TIMEOUT_CHECK_PERIOD	5 * 1000

static NF_BUFFERS			g_nfBuffers;
static DWORD				g_nThreads = 1;
static HANDLE				g_hDevice_old;
static AutoHandle			g_hDevice;
static AutoEventHandle		g_ioPostEvent;
static AutoEventHandle		g_ioEvent;
static AutoEventHandle		g_stopEvent;
static DWORD WINAPI	nf_workThread(LPVOID lpThreadParameter);
// static DWORD WINAPI nf_AlpcworkThread(LPVOID lpThreadParameter);
static NF_EventHandler* g_pEventHandler = NULL;
static char	g_driverName[MAX_PATH] = { 0 };

static AutoCriticalSection	g_cs;

#include "EventQueue.h"
//static EventQueue<NFEvent> g_eventQueue;
//static EventQueue<NFEventOut> g_eventQueueOut;

static AutoEventHandle		g_workThreadStartedEvent;
static AutoEventHandle		g_workThreadStoppedEvent;

enum IoctCode
{
	NF_DATALINKMAC_LAYER_PACKET = 1,
	NF_ESTABLISHED_LAYER_PACKET
};

PVOID DevctrlIoct::get_eventhandler()
{
	return g_pEventHandler;
}

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
	m_alpcthreadobjhandler = NULL;
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

int DevctrlIoct::devctrl_Alpcworkthread()
{
	WCHAR AlpcDriverPortName[] = L"\\RPC Control\\AlpcDriverPort";
	// start thread
	m_alpcthreadobjhandler = CreateThread(
		NULL,
		0,
		(LPTHREAD_START_ROUTINE)AlpcPortStart,
		(LPVOID)AlpcDriverPortName,
		0,
		&m_dwthreadid
	);
	if (!m_threadobjhandler)
		return 0;

	m_listthreadobjhandler = CreateThread(
		NULL,
		0,
		(LPTHREAD_START_ROUTINE)list_thread,
		NULL,
		0,
		&m_dwthreadid1
	);
	if (!m_listthreadobjhandler)
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

int DevctrlIoct::devctrl_InitshareMem()
{
	AutoLock lock(g_cs);

	if (m_devhandler == INVALID_HANDLE_VALUE)
	{
		return NF_STATUS_FAIL;
	}
	else
	{
		OutputDebugString(L"Attach m_devhandler Success");
		g_hDevice.Attach(m_devhandler);
		strncpy(g_driverName, "wfpdriver", sizeof(g_driverName));
	}

	DWORD dwBytesReturned = 0;
	memset(&g_nfBuffers, 0, sizeof(g_nfBuffers));

	OVERLAPPED ol;
	AutoEventHandle hEvent;

	memset(&ol, 0, sizeof(ol));
	ol.hEvent = hEvent;

	if (!DeviceIoControl(g_hDevice,
		CTL_DEVCTRL_OPEN_SHAREMEM,
		NULL, 0,
		(LPVOID)&g_nfBuffers, sizeof(g_nfBuffers),
		NULL, &ol))
	{
		if (GetLastError() != ERROR_IO_PENDING)
		{
			g_hDevice.Close();
			return NF_STATUS_FAIL;
		}
	}

	if (!GetOverlappedResult(g_hDevice, &ol, &dwBytesReturned, TRUE))
	{
		g_hDevice.Close();
		return NF_STATUS_FAIL;
	}

	if (dwBytesReturned != sizeof(g_nfBuffers))
	{
		g_hDevice.Close();
		return NF_STATUS_FAIL;
	}

	return 1;
}

int DevctrlIoct::devctrl_waitSingeObject()
{
	if(m_alpcthreadobjhandler)
		WaitForSingleObject(m_alpcthreadobjhandler, INFINITE);
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

int DevctrlIoct::devctrl_OnMonitor()
{
	return devctrl_sendioct(CTL_DEVCTRL_ENABLE_MONITOR);
}

int DevctrlIoct::devctrl_sendioct(const int ioctcode)
{
	DWORD dSize;

	if (!m_devhandler)
		return -1;

	OutputDebugString(L"devctrl_sendioct entablMonitor");
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
	{
		OutputDebugString(L"devctrl_sendioct Error End");
		return -2;
	}	
	return 1;
}

int DevctrlIoct::devctrl_writeio()
{
	return 0;
}

static void handleEventDispath(PNF_DATA pData)
{
	switch (pData->code)
	{
	case NF_ESTABLISHED_LAYER_PACKET:
	{
		// push established - event
		// printf("Code: %d, id: %d, pData_buffer: 0x%p , size = %d\n\r", pData->code, pData->id, pData->buffer, pData->bufferSize);
		g_pEventHandler->establishedPacket(pData->buffer, pData->bufferSize);
	}
	break;
	case NF_DATALINKMAC_LAYER_PACKET:
	{
		g_pEventHandler->datalinkPacket(pData->buffer, pData->bufferSize);
		// push datalink - event
	}
	break;
	}
}

// ReadFile Driver Buffer
static DWORD WINAPI nf_workThread(LPVOID lpThreadParameter)
{
	DWORD readBytes;
	PNF_DATA pData;
	OVERLAPPED ol;
	DWORD dwRes;
	NF_READ_RESULT rr;
	HANDLE events[] = { g_ioEvent, g_stopEvent };
	DWORD waitTimeout;
	bool abortBatch;
	int i;

	OutputDebugString(L"Entry WorkThread");

	SetEvent(g_workThreadStartedEvent);
	// g_eventQueue.init(g_nThreads);
	// g_eventQueueOut.init(1);

	for (;;)
	{
		waitTimeout = 10;
		abortBatch = false;
		// g_eventQueue.suspend(true);
		
		// �첽ȥ��
		for (i = 0; i < 8; i++)
		{
			readBytes = 0;

			memset(&ol, 0, sizeof(ol));

			ol.hEvent = g_ioEvent;

			if (!ReadFile(g_hDevice, &rr, sizeof(rr), NULL, &ol))
			{
				if (GetLastError() != ERROR_IO_PENDING)
				{
					OutputDebugString(L"ReadFile Error!");
					goto finish;
				}
			}

			for (;;)
			{
				dwRes = WaitForMultipleObjects(
					sizeof(events) / sizeof(events[0]),
					events,
					FALSE,
					waitTimeout);

				if (dwRes == WAIT_TIMEOUT)
				{
					waitTimeout = TCP_TIMEOUT_CHECK_PERIOD;

					//g_eventQueue.suspend(false);
					//g_eventQueueOut.processEvents();
					//g_eventQueue.processEvents();
					abortBatch = true;

					continue;
				}
				else if (dwRes != WAIT_OBJECT_0)
				{
					goto finish;
				}

				dwRes = WaitForSingleObject(g_stopEvent, 0);
				if (dwRes == WAIT_OBJECT_0)
				{
					goto finish;
				}

				if (!GetOverlappedResult(g_hDevice, &ol, &readBytes, FALSE))
				{
					goto finish;
				}

				break;
			}

			readBytes = (DWORD)rr.length;

			if (readBytes > g_nfBuffers.inBufLen)
			{
				readBytes = (DWORD)g_nfBuffers.inBufLen;
			}

			pData = (PNF_DATA)g_nfBuffers.inBuf;

			while (readBytes >= (sizeof(NF_DATA) - 1))
			{
				handleEventDispath(pData);

				if ((pData->code == NF_DATALINKMAC_LAYER_PACKET ||
					pData->code == NF_ESTABLISHED_LAYER_PACKET) &&
					pData->bufferSize < 1400)
				{
					abortBatch = true;
				}

				if (readBytes < (sizeof(NF_DATA) - 1 + pData->bufferSize))
				{
					break;
				}

				readBytes -= sizeof(NF_DATA) - 1 + pData->bufferSize;
				pData = (PNF_DATA)(pData->buffer + pData->bufferSize);
			}

			if (abortBatch)
				break;
		}

		//g_eventQueue.suspend(false);
		//g_eventQueueOut.processEvents();
		//g_eventQueue.processEvents();
		//g_eventQueue.wait(8000);
		//g_eventQueueOut.wait(64000);
	}

finish:

	CancelIo(g_hDevice);

	//g_eventQueue.free();
	//g_eventQueueOut.free();

	SetEvent(g_workThreadStoppedEvent);

	OutputDebugString(L"ReadFile Thread Exit");

	return 0;
}

void DevctrlIoct::nf_setWfpCheckEventHandler(PVOID64 pHandler)
{
	g_pEventHandler = (NF_EventHandler*)pHandler;
}