// wfptools.dll��������
// 2021.9.6
#include <Windows.h>

typedef int (*Pnf_init)(void);
Pnf_init nf_init;

typedef int (*Pnf_getprocessinfo)(UINT32* Locaaddripv4, unsigned long localport, int protocol, PVOID64 getbuffer);
Pnf_getprocessinfo nf_getprocessinfo;

typedef int (*Pnf_monitor)(int code);
Pnf_monitor nf_monitor;

typedef struct _PROCESS_INFO
{
	WCHAR  processPath[260];
	UINT64 processId;
}PROCESS_INFO, * PPROCESS_INFO;

int main(void)
{
	DWORD status = 0;
	do
	{
		HMODULE wfpdll = LoadLibrary(L"wfptools.dll");
		if (!wfpdll)
			break;

		nf_init = (Pnf_init)GetProcAddress(wfpdll,"nf_init");
		if (!nf_init)
			break;

		nf_getprocessinfo = (Pnf_getprocessinfo)GetProcAddress(wfpdll, "nf_getprocessinfo");
		if (!nf_getprocessinfo)
			break;

		nf_monitor = (Pnf_monitor)GetProcAddress(wfpdll, "nf_monitor");
		if (!nf_monitor)
			break;

		// 1) ��ʼ���� ������װ�������� - processinfo���ݹ���
		nf_init();

		system("pause");

		/*
			1.  ipv4 address - ipv6(������)
			2.  ���ض˿�
			3.  Э�� tcp - udp
			4.  ָ��
		*/
		PROCESS_INFO processinfo;
		RtlSecureZeroMemory(&processinfo, sizeof(PROCESS_INFO));
		UINT32 ipv4addr = 0x2199562432;
		unsigned long localport = 53;
		// 2) ��ȡ������Ϣ
		status = nf_getprocessinfo(&ipv4addr, localport, IPPROTO_TCP, &processinfo);
		if (status == 1)
		{
			// Success
			processinfo.processId;
			processinfo.processPath;
		}

		// ��Ҫ֧��ipv6��ַ��ѯ

		// 3����ͣ��� - �����ǹر�������DLL - ֻ�ǲ���������ץ��
		status = nf_monitor(0);

		// 4) ���ü�� - ����ץ��
		status = nf_monitor(1);

		// 5) DLL�ͷ� - ����������й¶

	} while (false);

	return 0;
}