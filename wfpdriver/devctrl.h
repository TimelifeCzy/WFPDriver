#ifndef  _DEVCTRL_H
#define  _DEVCTRL_H

DRIVER_DISPATCH devctrl_dispatch;
NTSTATUS devctrl_dispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP irp);

NTSTATUS devctrl_init();
NTSTATUS devctrl_free();
void devctrl_setShutdown();
UINT64 devctrl_fillBuffer();
NTSTATUS devtrl_popDataLinkData(UINT64* pOffset);
NTSTATUS devctrl_pushDataLinkCtxBuffer(int code);
NTSTATUS devctrl_pushFlowCtxBuffer(int code);

#endif // ! _DEVCTRL_H
