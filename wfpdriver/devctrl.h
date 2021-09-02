#ifndef  _DEVCTRL_H
#define  _DEVCTRL_H

DRIVER_DISPATCH devctrl_dispatch;
NTSTATUS devctrl_dispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP irp);

NTSTATUS devctrl_init();
NTSTATUS devctrl_free();
void devctrl_setShutdown();
NTSTATUS devctrl_pushDataLinkCtxBuffer(int code);
NTSTATUS devtrl_popDataLinkData(UINT64* pOffset);
UINT64 devctrl_fillBuffer();

#endif // ! _DEVCTRL_H
