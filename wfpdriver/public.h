//
// 	WFPDRIVER 
// 	Copyright (C) 2021 Vitaly Sidorov
//	All rights reserved.
//
//  ¹«¹²Í·
//

#ifndef _PUBLIC_H
#define _PUBLIC_H

#ifdef _NXPOOLS
#ifdef USE_NTDDI
#if (NTDDI_VERSION >= NTDDI_WIN8)
#define POOL_NX_OPTIN 1
#endif
#endif
#endif

#include <ntifs.h>
#include <ntstrsafe.h>

#undef ASSERT
#define ASSERT(x)

#define MEM_TAG		'3TLF'
#define MEM_TAG_TCP	'TTLF'
#define MEM_TAG_TCP_PACKET	'PTLF'
#define MEM_TAG_TCP_DATA	'DTLF'
#define MEM_TAG_TCP_DATA_COPY	'CTLF'
#define MEM_TAG_TCP_INJECT	'ITLF'
#define MEM_TAG_UDP	'UULF'
#define MEM_TAG_UDP_PACKET	'PULF'
#define MEM_TAG_UDP_DATA	'DULF'
#define MEM_TAG_UDP_DATA_COPY	'CULF'
#define MEM_TAG_UDP_INJECT	'IULF'
#define MEM_TAG_QUEUE	'QTLF'
#define MEM_TAG_IP_PACKET	'PILF'
#define MEM_TAG_IP_DATA_COPY 'DILF'
#define MEM_TAG_IP_INJECT	'IILF'
#define MEM_TAG_NETWORK	'SWSW'

#define MEM_TAG_DK	'UDDK'

#define malloc_np(size)	ExAllocatePoolWithTag(NonPagedPool, (size), MEM_TAG)
#define free_np(p) ExFreePool(p);

#define sl_init(x) KeInitializeSpinLock(x)
#define sl_lock(x, lh) KeAcquireInStackQueuedSpinLock(x, lh)
#define sl_unlock(lh) KeReleaseInStackQueuedSpinLock(lh)

#define htonl(x) (((((ULONG)(x))&0xffL)<<24)           | \
	((((ULONG)(x))&0xff00L)<<8)        | \
	((((ULONG)(x))&0xff0000L)>>8)        | \
	((((ULONG)(x))&0xff000000L)>>24))

#define htons(_x_) ((((unsigned char*)&_x_)[0] << 8) & 0xFF00) | ((unsigned char*)&_x_)[1] 

#define DPREFIX "[DK]-"

#define DEFAULT_HASH_SIZE 3019

#define MAX_PROCESS_PATH_LEN 300
#define MAX_PROCESS_NAME_LEN 64

extern DWORD g_dwLogLevel;

#define LogOutput(t,...) if (g_dwLogLevel & t) DbgPrint(## __VA_ARGS__)
#define LogOutputEx(b,t,...) if ((!(g_dwLogLevel & 0x1000000) || (b)) && (g_dwLogLevel & t)) DbgPrint(## __VA_ARGS__)
#define IS_TCP_GAME_FLT() (g_dwLogLevel & 0x10000000)
#define IS_UDP_GAME_FLT() (g_dwLogLevel & 0x20000000)
#define IS_GAME_LOG() (g_dwLogLevel & 0x1000000)
#define IS_TCP_GAME_CHK() (g_dwLogLevel & 0x11000000)
#define IS_UDP_GAME_CHK() (g_dwLogLevel & 0x21000000)

#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(CtlGuid,(a7f09d73, 5ac6, 4b8b, 8a33, e7b8c87e4609),  \
        WPP_DEFINE_BIT(FLAG_INFO))

#define _NF_INTERNALS

BOOLEAN regPathExists(wchar_t* registryPath);

enum _NF_DATA_CODE
{
	NF_DATALINK_SEND = 1,
	NF_DATALINK_INJECT
}NF_DATA_CODE;

typedef UNALIGNED struct _NF_DATA
{
	int				code;
	INT64			id;
	unsigned long	bufferSize;
	char 			buffer[1];
} NF_DATA, * PNF_DATA;

typedef UNALIGNED struct _NF_READ_RESULT
{
	unsigned __int64 length;
} NF_READ_RESULT, * PNF_READ_RESULT;

#endif