#include <Windows.h>
#include <iostream>
#include "devctrl.h"
#include "nfevents.h"
#include "nf_api.h"
#include <map>
#include <mutex>
BOOL DeviceDosPathToNtPath(wchar_t* pszDosPath, wchar_t* pszNtPath);

using namespace std;

const char devSyLinkName[] = "\\??\\WFPDark";

typedef struct _PROCESS_INFO
{
	WCHAR  processPath[MAX_PATH * 2];
	UINT64 processId;
}PROCESS_INFO, *PPROCESS_INFO;

static mutex g_mutx;
map<int, NF_CALLOUT_FLOWESTABLISHED_INFO> flowestablished_map;

class EventHandler : public NF_EventHandler
{
	// ���� TCP UDP �ѽ�����������
	void establishedPacket(const char* buf, int len) override
	{
		NF_CALLOUT_FLOWESTABLISHED_INFO flowestablished_processinfo;
		RtlSecureZeroMemory(&flowestablished_processinfo, sizeof(NF_CALLOUT_FLOWESTABLISHED_INFO));
		RtlCopyMemory(&flowestablished_processinfo, buf, len);
		
		/*
			TCP - UDP ��ͬЭ����ͬ�˿ڽ����ǣ���Ϊ������Ҫ�������еİ�
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
		default:
		{
			OutputDebugString(L"Other Protocol Erro");
		}
		}
		g_mutx.lock();
		flowestablished_map[keyLocalPort] = flowestablished_processinfo;
		g_mutx.unlock();

		//// test api �����Ƿ���Դ�map��ȡ����
		//PROCESS_INFO processinfo = { 0, };
		//nf_getprocessinfo(&flowestablished_processinfo.ipv4LocalAddr, flowestablished_processinfo.toLocalPort, flowestablished_processinfo.protocol, &processinfo);
		//processinfo.processId;
		//processinfo.processPath;

		// test path
		wstring wsinfo;
		WCHAR info[MAX_PATH] = { 0, };
		// swprintf(str, 100, L"%ls%d is %d", L"The half of ", 80, 80 / 2);
		swprintf(info, MAX_PATH, L"Locate: 0x%d:%d -> remote: 0x%d:%d type: %d", \
			flowestablished_processinfo.ipv4LocalAddr, flowestablished_processinfo.toLocalPort, \
			flowestablished_processinfo.ipv4toRemoteAddr, flowestablished_processinfo.toRemotePort, \
			flowestablished_processinfo.protocol
		);
		wsinfo = flowestablished_processinfo.processPath;
		wsinfo += L"\r\n";
		wsinfo += info;
		OutputDebugString(wsinfo.data());
	}

	// ���� MAC ��·������
	void datalinkPacket(const char* buf, int len) override
	{
		NF_CALLOUT_MAC_INFO datalink_netinfo;
		RtlSecureZeroMemory(&datalink_netinfo, sizeof(NF_CALLOUT_MAC_INFO));
		RtlCopyMemory(&datalink_netinfo, buf, len);
		
		OutputDebugString(L"-------------------------------------");
		OutputDebugStringA((LPCSTR)datalink_netinfo.mac_info.pSourceAddress);
		OutputDebugStringA((LPCSTR)datalink_netinfo.mac_info.pDestinationAddress);
		OutputDebugString(L"-------------------------------------");
	}

};

static DevctrlIoct devobj;
static EventHandler packtebuff;

#define				SECURITY_STRING_LEN							168
#define				LG_PAGE_SIZE								4096
#define				MAX_KEY_LENGTH								1024
#define				LG_SLEEP_TIME								4000

const BYTE g_szSecurity[SECURITY_STRING_LEN] =
{
	0x01,0x00,0x14,0x80,0x90,0x00,0x00,0x00,0x9c,0x00,0x00,0x00,0x14,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0x02,
	0x00,0x1c,0x00,0x01,0x00,0x00,0x00,0x02,0x80,0x14,0x00,0xff,0x01,0x0f,0x00,0x01,0x01,0x00,0x00,0x00,0x00,
	0x00,0x01,0x00,0x00,0x00,0x00,0x02,0x00,0x60,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x14,0x00,0xfd,0x01,0x02,
	0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x05,0x12,0x00,0x00,0x00,0x00,0x00,0x18,0x00,0xff,0x01,0x0f,0x00,
	0x01,0x02,0x00,0x00,0x00,0x00,0x00,0x05,0x20,0x00,0x00,0x00,0x20,0x02,0x00,0x00,0x00,0x00,0x14,0x00,0x8d,
	0x01,0x02,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x05,0x0b,0x00,0x00,0x00,0x00,0x00,0x18,0x00,0xfd,0x01,
	0x02,0x00,0x01,0x02,0x00,0x00,0x00,0x00,0x00,0x05,0x20,0x00,0x00,0x00,0x23,0x02,0x00,0x00,0x01,0x01,0x00,
	0x00,0x00,0x00,0x00,0x05,0x12,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x05,0x12,0x00,0x00,0x00
};

bool SplitFilePath(const char* szFullPath, char* szPath, char* szFileName, char* szFileExt)
{
	char* p, * q, * r;
	size_t	len;

	if (NULL == szFullPath)
	{
		return false;
	}
	p = (char*)szFullPath;
	len = strlen(szFullPath);
	if (szPath)
	{
		szPath[0] = 0;
	}
	if (szFileName)
	{
		szFileName[0] = 0;
	}
	if (szFileExt)
	{
		szFileExt[0] = 0;
	}
	q = p + len;
	while (q > p)
	{
		if (*q == '\\' || *q == '/')
		{
			break;
		}
		q--;
	}
	if (q <= p)
	{
		return false;
	}
	if (szPath)
	{
		memcpy(szPath, p, q - p + 1);
		szPath[q - p + 1] = 0;
	}
	q++;
	p = q;
	r = NULL;
	while (*q)
	{
		if (*q == '.')
		{
			r = q;
		}
		q++;
	}
	if (NULL == r)
	{
		if (szFileName)
		{
			memcpy(szFileName, p, q - p + 1);
		}
	}
	else
	{
		if (szFileName)
		{
			memcpy(szFileName, p, r - p);
			szFileName[r - p] = 0;
		}
		if (szFileExt)
		{
			memcpy(szFileExt, r + 1, q - r + 1);
		}
	}

	return true;
}

int FindInMultiSz(LPTSTR szMultiSz, int nMultiSzLen, LPTSTR szMatch)
{
	size_t	i, j;
	size_t	len = lstrlenW(szMatch);
	TCHAR	FirstChar = *szMatch;
	bool	bFound;
	LPTSTR	pTry;

	if (NULL == szMultiSz || NULL == szMatch || nMultiSzLen <= 0)
	{
		return -1;
	}
	for (i = 0; i < nMultiSzLen - len; i++)
	{
		if (*szMultiSz++ == FirstChar)
		{
			bFound = true;
			pTry = szMultiSz;
			for (j = 1; j <= len; j++)
			{
				if (*pTry++ != szMatch[j])
				{
					bFound = false;
					break;
				}
			}
			if (bFound)
			{
				return (int)i;
			}
		}
	}

	return -1;
}

int	InstallDriver(const wchar_t* cszDriverName, const wchar_t* cszDriverFullPath)
{
	WCHAR	szBuf[LG_PAGE_SIZE];
	HKEY	hKey;
	DWORD	dwData;

	if (NULL == cszDriverName || NULL == cszDriverFullPath)
	{
		return -1;
	}
	memset(szBuf, 0, LG_PAGE_SIZE);
	lstrcpyW(szBuf,  L"SYSTEM\\CurrentControlSet\\Services\\");
	lstrcatW(szBuf, cszDriverName);
	if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, szBuf, 0, REG_NONE, 0, KEY_ALL_ACCESS, NULL, &hKey, (LPDWORD)&dwData) != ERROR_SUCCESS)
	{
		return -1;
	}
	lstrcpyW(szBuf, cszDriverName);
	if (RegSetValueEx(hKey, L"DisplayName", 0, REG_SZ, (CONST BYTE*)szBuf, (DWORD)lstrlenW(szBuf)) != ERROR_SUCCESS)
	{
		return -1;
	}
	dwData = 1;
	if (RegSetValueEx(hKey, L"ErrorControl", 0, REG_DWORD, (CONST BYTE*) & dwData, sizeof(DWORD)) != ERROR_SUCCESS)
	{
		return -1;
	}
	lstrcpyW(szBuf, L"\\??\\");
	lstrcatW(szBuf, cszDriverFullPath);
	if (RegSetValueEx(hKey, L"ImagePath", 0, REG_SZ, (CONST BYTE*)szBuf, (DWORD)lstrlenW(szBuf)) != ERROR_SUCCESS)
	{
		return -1;
	}
	dwData = 3;
	if (RegSetValueEx(hKey, L"Start", 0, REG_DWORD, (CONST BYTE*) & dwData, sizeof(DWORD)) != ERROR_SUCCESS)
	{
		return -1;
	}
	dwData = 1;
	if (RegSetValueEx(hKey, L"Type", 0, REG_DWORD, (CONST BYTE*) & dwData, sizeof(DWORD)) != ERROR_SUCCESS)
	{
		return -1;
	}
	RegFlushKey(hKey);
	RegCloseKey(hKey);
	lstrcpyW(szBuf, L"SYSTEM\\CurrentControlSet\\Services\\");
	lstrcpyW(szBuf, cszDriverName);
	lstrcpyW(szBuf, L"\\Security");
	if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, szBuf, 0, REG_NONE, 0, KEY_ALL_ACCESS, NULL, &hKey, (LPDWORD)&dwData) != ERROR_SUCCESS)
	{
		return -1;
	}
	dwData = SECURITY_STRING_LEN;
	if (RegSetValueEx(hKey, L"Security", 0, REG_BINARY, g_szSecurity, dwData) != ERROR_SUCCESS)
	{
		return -1;
	}
	RegFlushKey(hKey);
	RegCloseKey(hKey);

	return 0;
}

int CreateDriver(const wchar_t* cszDriverName, const wchar_t* cszDriverFullPath)
{
	SC_HANDLE		schManager;
	SC_HANDLE		schService;
	SERVICE_STATUS	svcStatus;
	bool			bStopped = false;
	int				i;

	if (NULL == cszDriverName || NULL == cszDriverFullPath)
	{
		return -1;
	}
	schManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (NULL == schManager)
	{
		return -1;
	}
	schService = OpenService(schManager, cszDriverName, SERVICE_ALL_ACCESS);
	if (NULL != schService)
	{
		if (ControlService(schService, SERVICE_CONTROL_INTERROGATE, &svcStatus))
		{
			if (svcStatus.dwCurrentState != SERVICE_STOPPED)
			{
				if (0 == ControlService(schService, SERVICE_CONTROL_STOP, &svcStatus))
				{
					CloseServiceHandle(schService);
					CloseServiceHandle(schManager);
					return -1;
				}
				for (i = 0; i < 10; i++)
				{
					if (ControlService(schService, SERVICE_CONTROL_INTERROGATE, &svcStatus) == 0 || svcStatus.dwCurrentState == SERVICE_STOPPED)
					{
						bStopped = true;
						break;
					}
					Sleep(LG_SLEEP_TIME);
				}
				if (!bStopped)
				{
					CloseServiceHandle(schService);
					CloseServiceHandle(schManager);
					return -1;
				}
			}
		}
		CloseServiceHandle(schService);
		CloseServiceHandle(schManager);
		return 0;
	}
	schService = CreateService(schManager, cszDriverName, cszDriverName, SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, cszDriverFullPath, NULL, NULL, NULL, NULL, NULL);
	if (NULL == schService)
	{
		CloseServiceHandle(schManager);
		return -1;
	}
	CloseServiceHandle(schService);
	CloseServiceHandle(schManager);

	return 0;
}

int StartDriver(const wchar_t* cszDriverName, const wchar_t* cszDriverFullPath)
{
	SC_HANDLE		schManager;
	SC_HANDLE		schService;
	SERVICE_STATUS	svcStatus;
	bool			bStarted = false;
	int				i;

	if (NULL == cszDriverName)
	{
		return -1;
	}
	if (CreateDriver(cszDriverName, cszDriverFullPath) < 0)
	{
		return -1;
	}
	schManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (NULL == schManager)
	{
		return -1;
	}
	schService = OpenService(schManager, cszDriverName, SERVICE_ALL_ACCESS);
	if (NULL == schService)
	{
		CloseServiceHandle(schManager);
		return -1;
	}
	if (ControlService(schService, SERVICE_CONTROL_INTERROGATE, &svcStatus))
	{
		if (svcStatus.dwCurrentState == SERVICE_RUNNING)
		{
			CloseServiceHandle(schService);
			CloseServiceHandle(schManager);
			return 0;
		}
	}
	else if (GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
	{
		CloseServiceHandle(schService);
		CloseServiceHandle(schManager);
		return -1;
	}
	if (0 == StartService(schService, 0, NULL))
	{
		CloseServiceHandle(schService);
		CloseServiceHandle(schManager);
		return -1;
	}
	for (i = 0; i < 10; i++)
	{
		if (ControlService(schService, SERVICE_CONTROL_INTERROGATE, &svcStatus) && svcStatus.dwCurrentState == SERVICE_RUNNING)
		{
			bStarted = true;
			break;
		}
		Sleep(LG_SLEEP_TIME);
	}
	CloseServiceHandle(schService);
	CloseServiceHandle(schManager);

	return bStarted ? 1 : -1;
}

int StopDriver(const wchar_t* cszDriverName, const wchar_t* cszDriverFullPath)
{
	SC_HANDLE		schManager;
	SC_HANDLE		schService;
	SERVICE_STATUS	svcStatus;
	bool			bStopped = false;
	int				i;

	schManager = OpenSCManager(NULL, 0, 0);
	if (NULL == schManager)
	{
		return -1;
	}
	schService = OpenService(schManager, cszDriverName, SERVICE_ALL_ACCESS);
	if (NULL == schService)
	{
		CloseServiceHandle(schManager);
		return -1;
	}
	if (ControlService(schService, SERVICE_CONTROL_INTERROGATE, &svcStatus))
	{
		if (svcStatus.dwCurrentState != SERVICE_STOPPED)
		{
			if (0 == ControlService(schService, SERVICE_CONTROL_STOP, &svcStatus))
			{
				CloseServiceHandle(schService);
				CloseServiceHandle(schManager);
				return -1;
			}
			for (i = 0; i < 10; i++)
			{
				if (ControlService(schService, SERVICE_CONTROL_INTERROGATE, &svcStatus) == 0 || svcStatus.dwCurrentState == SERVICE_STOPPED)
				{
					bStopped = true;
					break;
				}
				Sleep(LG_SLEEP_TIME);
			}
			if (!bStopped)
			{
				CloseServiceHandle(schService);
				CloseServiceHandle(schManager);
				return -1;
			}
		}
	}
	CloseServiceHandle(schService);
	CloseServiceHandle(schManager);

	return 0;
}

DWORD GetServicesStatus(void)
{
	TCHAR szSvcName[] = L"wfpdriver";
	SC_HANDLE schSCManager = NULL;
	SC_HANDLE schService = NULL;

	SERVICE_STATUS_PROCESS ssStatus;
	DWORD dwOldCheckPoint = 0;
	DWORD dwStartTickCount = 0;
	DWORD dwWaitTime = 0;
	DWORD dwBytesNeeded = 0;

	schSCManager = OpenSCManager(
		NULL,                                // local computer
		NULL,                                // ServicesActive database
		SC_MANAGER_ALL_ACCESS);              // full access rights

	if (NULL == schSCManager)
	{
		return -1;

	}

	schService = OpenService(
		schSCManager,                      // SCM database
		szSvcName,                         // name of service
		SERVICE_QUERY_STATUS |
		SERVICE_ENUMERATE_DEPENDENTS);     // full access

	if (schService == NULL)
	{
		CloseServiceHandle(schSCManager);
		return -1;
	}

	if (!QueryServiceStatusEx(
		schService,                         // handle to service
		SC_STATUS_PROCESS_INFO,             // information level
		(LPBYTE)&ssStatus,                 // address of structure
		sizeof(SERVICE_STATUS_PROCESS),     // size of structure
		&dwBytesNeeded))                  // size needed if buffer is too small
	{
		CloseServiceHandle(schService);
		CloseServiceHandle(schSCManager);
		return -1;
	}
	return ssStatus.dwCurrentState;
}

int nf_driverInstall()
{
	wstring DriverPath;
	wstring PathAll;
	wstring pszCmd = L"sc start wfpdriver";
	STARTUPINFO si = { sizeof(STARTUPINFO) };
	DWORD nSeriverstatus = -1;
	TCHAR szFilePath[MAX_PATH + 1] = { 0 };
	GetModuleFileName(NULL, szFilePath, MAX_PATH);
	OutputDebugString(szFilePath);
	DriverPath = szFilePath;
	int num = DriverPath.find_last_of(L"\\");
	PathAll = DriverPath.substr(0, num);
	PathAll += L"\\wfpdriver.sys";

	// �ȿ�����C��
	CopyFile(PathAll.data(), L"C:\\Windows\\System32\\drivers\\wfpdriver.sys", FALSE);

	//if (InstallDriver(L"wfpdriver", L"C:\\Windows\\System32\\drivers\\wfpdriver.sys") == TRUE) {
	//	OutputDebugString(L"installDvr success.");
	//}
	//else
	//{
	//	return -1;
	//}
	if (StartDriver(L"wfpdriver", L"C:\\Windows\\System32\\drivers\\wfpdriver.sys") == TRUE)
	{
		OutputDebugString(L"Start Driver success.");
		return 1;
	}
	else
	{
		OutputDebugString(L"Start Driver failuer.");
		return -1;
	}
}

//int main(void)
int nf_init(void)
{
	int status = 0;
	DWORD nSeriverstatus = -1;
	wstring pszCmd = L"sc start wfpdriver";
	STARTUPINFO si = { sizeof(STARTUPINFO) };
	OutputDebugString(L"Install Driver");

	nSeriverstatus = GetServicesStatus();
	switch (nSeriverstatus)
	{
	// ��������
	case SERVICE_CONTINUE_PENDING:
	case SERVICE_RUNNING:
	case SERVICE_START_PENDING:
	{
		OutputDebugString(L"Driver Running");
		break;
	}
	break;
	// �Ѱ�װ - δ����
	case SERVICE_STOPPED:
	case SERVICE_STOP_PENDING:
	{
		GetStartupInfo(&si);
		si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
		si.wShowWindow = SW_HIDE;
		// ����������
		PROCESS_INFORMATION pi;
		CreateProcess(NULL, (LPWSTR)pszCmd.c_str(), NULL, NULL, TRUE, NULL, NULL, NULL, &si, &pi);
		Sleep(3000);
		nSeriverstatus = GetServicesStatus();
		if (SERVICE_RUNNING == nSeriverstatus)
		{
			OutputDebugString(L"sc Driver Running");
			break;
		}
		else
		{
			OutputDebugString(L"sc Driver Install Failuer");
			return -1;
		}
	}
	break;
	default:
	{
		OutputDebugStringW(L"nf_driverInstall");
		if (!nf_driverInstall())
		{
			return -1;
		}
	}
	break;
	}

	// Init devctrl
	status = devobj.devctrl_init();
	if (0 > status)
	{
		cout << "devctrl_init error: main.c --> lines: 342" << endl;
		return -1;
	}

	do 
	{
		// Open driver
		status = devobj.devctrl_opendeviceSylink(devSyLinkName);
		if (0 > status)
		{
			cout << "devctrl_opendeviceSylink error: main.c --> lines: 352" << endl;
			break;
		}

		// Init share Mem
		status = devobj.devctrl_InitshareMem();
		if (0 > status)
		{
			cout << "devctrl_InitshareMem error: main.c --> lines: 360" << endl;
			break;
		}

		status = devobj.devctrl_workthread();
		if (0 > status)
		{
			cout << "devctrl_workthread error: main.c --> lines: 367" << endl;
			break;
		}

		// Enable try Network packte Monitor
		status = devobj.devctrl_OnMonitor();
		if (0 > status)
		{
			cout << "devctrl_InitshareMem error: main.c --> lines: 375" << endl;
			break;
		}

		// Enable Event
		devobj.nf_setWfpCheckEventHandler((PVOID)&packtebuff);
		
		status = 1;
	} while (false);

	return status;
}

/*
	@ ����1 ipv4 address
	@ ����2 ���ض˿�
	@ ����3 Э��
	@ ����4 ����ָ��
*/
int nf_getprocessinfo(
	UINT32* Locaaddripv4, 
	unsigned long localport,
	int protocol,
	PVOID64 getbuffer
)
{
	// -1 ��������
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
		//RtlCopyMemory(processinf->processPath, mapiter->second.processPath, mapiter->second.processPathSize);
		
		WCHAR ntPath[MAX_PATH] = { 0 };
		DeviceDosPathToNtPath(mapiter->second.processPath, ntPath);
		RtlCopyMemory(processinf->processPath, ntPath, sizeof(ntPath));
		return 1;
	}
	catch (const std::exception&)
	{
		// �쳣
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


BOOL DeviceDosPathToNtPath(wchar_t* pszDosPath, wchar_t* pszNtPath)
{
    WCHAR			szDriveStr[MAX_PATH] = { 0 };
    WCHAR			szDevName[MAX_PATH] = { 0 };
    TCHAR			szDrive[3];
    INT             cchDevName;
    INT             i;

    //������  
    if (IsBadReadPtr(pszDosPath, 1) != 0)return FALSE;
    if (IsBadWritePtr(pszNtPath, 1) != 0)return FALSE;

    //��ȡ���ش����ַ���  
    ZeroMemory(szDriveStr, ARRAYSIZE(szDriveStr));
    ZeroMemory(szDevName, ARRAYSIZE(szDevName));
    if (GetLogicalDriveStringsW(sizeof(szDriveStr), szDriveStr))
    {
        for (i = 0; szDriveStr[i]; i += 4)
        {
            if (!lstrcmpiW(&(szDriveStr[i]), L"A:\\") /*|| !lstrcmpi(&(szDriveStr[i]), L"B:\\")*/)
                continue;

            szDrive[0] = szDriveStr[i];
            szDrive[1] = szDriveStr[i + 1];
            szDrive[2] = '\0';
            if (!QueryDosDeviceW(szDrive, szDevName, MAX_PATH))//��ѯ Dos �豸��  
                return FALSE;

            cchDevName = lstrlenW(szDevName);

            if (wcsnicmp(pszDosPath, szDevName, cchDevName) == 0)//����  
            {
                lstrcpyW(pszNtPath, szDrive);//����������  
                lstrcatW(pszNtPath, pszDosPath + cchDevName);//����·��  

                return TRUE;
            }
        }
    }

    lstrcpyW(pszNtPath, pszDosPath);

    return FALSE;
}