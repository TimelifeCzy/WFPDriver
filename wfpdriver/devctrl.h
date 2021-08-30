#ifndef  _DEVCTRL_H
#define  _DEVCTRL_H

DRIVER_DISPATCH devctrl_dispatch;
NTSTATUS devctrl_dispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP irp);

#endif // ! _DEVCTRL_H
