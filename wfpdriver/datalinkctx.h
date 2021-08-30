#ifndef _DATALINKCTX_H
#define _DATALINKCTX_H


NTSTATUS datalinkctx_init();
VOID datalinkctx_free();
NTSTATUS datalinkctx_popdata();
NTSTATUS datalinkctx_pushdata();

#endif // !_DATALINKCTX_H
