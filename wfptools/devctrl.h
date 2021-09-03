#ifndef _DEVCTRL_H
#define _DEVCTRL_H

class DevctrlIoct
{
public:
	DevctrlIoct();
	~DevctrlIoct();

	int devctrl_init();
	int devctrl_opendeviceSylink(const char* devSylinkName);
	int devctrl_workthread();
	int devctrl_waitSingeObject();
	void devctrl_clean();

private:
	HANDLE m_devhandler;
	HANDLE m_threadobjhandler;
	DWORD  m_dwthreadid;

	// ·¢ËÍ¿ØÖÆÂë
	int devctrl_sendioct(const int ioctcode);
	int devctrl_writeio();
};

#endif // !_DEVCTRL_H
