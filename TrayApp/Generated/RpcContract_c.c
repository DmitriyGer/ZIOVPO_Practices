

/* this ALWAYS GENERATED file contains the RPC client stubs */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Tue Jan 19 06:14:07 2038
 */
/* Compiler settings for Shared\RpcContract.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=AMD64 8.01.0628
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data
    VC __declspec() decoration level:
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#if defined(_M_AMD64)


#pragma warning( disable: 4049 )  /* more than 64k source lines */
#if _MSC_VER >= 1200
#pragma warning(push)
#endif

#pragma warning( disable: 4211 )  /* redefine extern to static */
#pragma warning( disable: 4232 )  /* dllimport identity*/
#pragma warning( disable: 4024 )  /* array to pointer mapping*/

#include <string.h>

#include "RpcContract.h"

#define TYPE_FORMAT_STRING_SIZE   197
#define PROC_FORMAT_STRING_SIZE   505
#define EXPR_FORMAT_STRING_SIZE   1
#define TRANSMIT_AS_TABLE_SIZE    0
#define WIRE_MARSHAL_TABLE_SIZE   0

typedef struct _RpcContract_MIDL_TYPE_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ TYPE_FORMAT_STRING_SIZE ];
    } RpcContract_MIDL_TYPE_FORMAT_STRING;

typedef struct _RpcContract_MIDL_PROC_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ PROC_FORMAT_STRING_SIZE ];
    } RpcContract_MIDL_PROC_FORMAT_STRING;

typedef struct _RpcContract_MIDL_EXPR_FORMAT_STRING
    {
    long          Pad;
    unsigned char  Format[ EXPR_FORMAT_STRING_SIZE ];
    } RpcContract_MIDL_EXPR_FORMAT_STRING;


static const RPC_SYNTAX_IDENTIFIER  _RpcTransferSyntax_2_0 =
{{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}};

#if defined(_CONTROL_FLOW_GUARD_XFG)
#define XFG_TRAMPOLINES(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree(pFlags, (ObjectType *)pObject);\
}
#define XFG_TRAMPOLINES64(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize64_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize64(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree64_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree64(pFlags, (ObjectType *)pObject);\
}
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)\
static void* ObjectType ## _bind_XFG(HandleType pObject)\
{\
return ObjectType ## _bind((ObjectType) pObject);\
}\
static void ObjectType ## _unbind_XFG(HandleType pObject, handle_t ServerHandle)\
{\
ObjectType ## _unbind((ObjectType) pObject, ServerHandle);\
}
#define XFG_TRAMPOLINE_FPTR(Function) Function ## _XFG
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol ## _XFG
#else
#define XFG_TRAMPOLINES(ObjectType)
#define XFG_TRAMPOLINES64(ObjectType)
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)
#define XFG_TRAMPOLINE_FPTR(Function) Function
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol
#endif


extern const RpcContract_MIDL_TYPE_FORMAT_STRING RpcContract__MIDL_TypeFormatString;
extern const RpcContract_MIDL_PROC_FORMAT_STRING RpcContract__MIDL_ProcFormatString;
extern const RpcContract_MIDL_EXPR_FORMAT_STRING RpcContract__MIDL_ExprFormatString;

#define GENERIC_BINDING_TABLE_SIZE   0


/* Standard interface: TrayRpcControl, ver. 1.0,
   GUID={0x89BD6495,0x20FA,0x4C90,{0x9A,0x1D,0x37,0xA9,0xFD,0x96,0x03,0xB2}} */


static const RPC_PROTSEQ_ENDPOINT __RpcProtseqEndpoint[] =
    {
    {(unsigned char *) "ncalrpc", (unsigned char *) "TrayServiceControlEndpoint"}
    };


static const RPC_CLIENT_INTERFACE TrayRpcControl___RpcClientInterface =
    {
    sizeof(RPC_CLIENT_INTERFACE),
    {{0x89BD6495,0x20FA,0x4C90,{0x9A,0x1D,0x37,0xA9,0xFD,0x96,0x03,0xB2}},{1,0}},
    {{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}},
    0,
    1,
    (RPC_PROTSEQ_ENDPOINT *)__RpcProtseqEndpoint,
    0,
    0,
    0x00000000
    };
RPC_IF_HANDLE TrayRpcControl_v1_0_c_ifspec = (RPC_IF_HANDLE)& TrayRpcControl___RpcClientInterface;
#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC TrayRpcControl_StubDesc;
#ifdef __cplusplus
}
#endif

static RPC_BINDING_HANDLE TrayRpcControl__MIDL_AutoBindHandle;


TrayRpcStopResult StopService(
    /* [in] */ handle_t hBinding)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&TrayRpcControl_StubDesc,
                  (PFORMAT_STRING) &RpcContract__MIDL_ProcFormatString.Format[0],
                  hBinding);
    return ( TrayRpcStopResult  )_RetVal.Simple;

}


TrayRpcStopResult ConfirmStopService(
    /* [in] */ handle_t hBinding)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&TrayRpcControl_StubDesc,
                  (PFORMAT_STRING) &RpcContract__MIDL_ProcFormatString.Format[36],
                  hBinding);
    return ( TrayRpcStopResult  )_RetVal.Simple;

}


TrayRpcStatusCode GetAuthInfo(
    /* [in] */ handle_t hBinding,
    /* [out] */ TrayRpcAuthInfo *authInfo)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&TrayRpcControl_StubDesc,
                  (PFORMAT_STRING) &RpcContract__MIDL_ProcFormatString.Format[72],
                  hBinding,
                  authInfo);
    return ( TrayRpcStatusCode  )_RetVal.Simple;

}


TrayRpcStatusCode Login(
    /* [in] */ handle_t hBinding,
    /* [string][in] */ wchar_t *username,
    /* [string][in] */ wchar_t *password,
    /* [out] */ TrayRpcAuthInfo *authInfo)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&TrayRpcControl_StubDesc,
                  (PFORMAT_STRING) &RpcContract__MIDL_ProcFormatString.Format[114],
                  hBinding,
                  username,
                  password,
                  authInfo);
    return ( TrayRpcStatusCode  )_RetVal.Simple;

}


TrayRpcStatusCode Logout(
    /* [in] */ handle_t hBinding)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&TrayRpcControl_StubDesc,
                  (PFORMAT_STRING) &RpcContract__MIDL_ProcFormatString.Format[168],
                  hBinding);
    return ( TrayRpcStatusCode  )_RetVal.Simple;

}


TrayRpcStatusCode GetLicenseState(
    /* [in] */ handle_t hBinding,
    /* [in] */ hyper productId,
    /* [string][in] */ wchar_t *deviceMac,
    /* [out] */ TrayRpcLicenseInfo *licenseInfo)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&TrayRpcControl_StubDesc,
                  (PFORMAT_STRING) &RpcContract__MIDL_ProcFormatString.Format[204],
                  hBinding,
                  productId,
                  deviceMac,
                  licenseInfo);
    return ( TrayRpcStatusCode  )_RetVal.Simple;

}


TrayRpcStatusCode ActivateProduct(
    /* [in] */ handle_t hBinding,
    /* [string][in] */ wchar_t *activationKey,
    /* [in] */ hyper productId,
    /* [string][in] */ wchar_t *deviceName,
    /* [string][in] */ wchar_t *deviceMac,
    /* [out] */ TrayRpcLicenseInfo *licenseInfo)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&TrayRpcControl_StubDesc,
                  (PFORMAT_STRING) &RpcContract__MIDL_ProcFormatString.Format[258],
                  hBinding,
                  activationKey,
                  productId,
                  deviceName,
                  deviceMac,
                  licenseInfo);
    return ( TrayRpcStatusCode  )_RetVal.Simple;

}


TrayRpcStatusCode ScanFile(
    /* [in] */ handle_t hBinding,
    /* [string][in] */ wchar_t *path,
    /* [out] */ TrayRpcAvFileScanResult *scanResult)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&TrayRpcControl_StubDesc,
                  (PFORMAT_STRING) &RpcContract__MIDL_ProcFormatString.Format[324],
                  hBinding,
                  path,
                  scanResult);
    return ( TrayRpcStatusCode  )_RetVal.Simple;

}


TrayRpcStatusCode ScanDirectory(
    /* [in] */ handle_t hBinding,
    /* [string][in] */ wchar_t *path,
    /* [out] */ TrayRpcAvDirectoryScanResult *scanResult)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&TrayRpcControl_StubDesc,
                  (PFORMAT_STRING) &RpcContract__MIDL_ProcFormatString.Format[372],
                  hBinding,
                  path,
                  scanResult);
    return ( TrayRpcStatusCode  )_RetVal.Simple;

}


TrayRpcStatusCode ScanFixedDrives(
    /* [in] */ handle_t hBinding,
    /* [out] */ TrayRpcAvDirectoryScanResult *scanResult)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&TrayRpcControl_StubDesc,
                  (PFORMAT_STRING) &RpcContract__MIDL_ProcFormatString.Format[420],
                  hBinding,
                  scanResult);
    return ( TrayRpcStatusCode  )_RetVal.Simple;

}


TrayRpcStatusCode GetAvDatabaseInfo(
    /* [in] */ handle_t hBinding,
    /* [out] */ TrayRpcAvDatabaseInfo *databaseInfo)
{

    CLIENT_CALL_RETURN _RetVal;

    _RetVal = NdrClientCall2(
                  ( PMIDL_STUB_DESC  )&TrayRpcControl_StubDesc,
                  (PFORMAT_STRING) &RpcContract__MIDL_ProcFormatString.Format[462],
                  hBinding,
                  databaseInfo);
    return ( TrayRpcStatusCode  )_RetVal.Simple;

}


#if !defined(__RPC_WIN64__)
#error  Invalid build platform for this stub.
#endif

static const RpcContract_MIDL_PROC_FORMAT_STRING RpcContract__MIDL_ProcFormatString =
    {
        0,
        {

	/* Procedure StopService */

			0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/*  2 */	NdrFcLong( 0x0 ),	/* 0 */
/*  6 */	NdrFcShort( 0x0 ),	/* 0 */
/*  8 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 10 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 12 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 14 */	NdrFcShort( 0x0 ),	/* 0 */
/* 16 */	NdrFcShort( 0x6 ),	/* 6 */
/* 18 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 20 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 22 */	NdrFcShort( 0x0 ),	/* 0 */
/* 24 */	NdrFcShort( 0x0 ),	/* 0 */
/* 26 */	NdrFcShort( 0x0 ),	/* 0 */
/* 28 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */

/* 30 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 32 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 34 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Procedure ConfirmStopService */

/* 36 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 38 */	NdrFcLong( 0x0 ),	/* 0 */
/* 42 */	NdrFcShort( 0x1 ),	/* 1 */
/* 44 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 46 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 48 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 50 */	NdrFcShort( 0x0 ),	/* 0 */
/* 52 */	NdrFcShort( 0x6 ),	/* 6 */
/* 54 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 56 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 58 */	NdrFcShort( 0x0 ),	/* 0 */
/* 60 */	NdrFcShort( 0x0 ),	/* 0 */
/* 62 */	NdrFcShort( 0x0 ),	/* 0 */
/* 64 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */

/* 66 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 68 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 70 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Procedure GetAuthInfo */

/* 72 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 74 */	NdrFcLong( 0x0 ),	/* 0 */
/* 78 */	NdrFcShort( 0x2 ),	/* 2 */
/* 80 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 82 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 84 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 86 */	NdrFcShort( 0x0 ),	/* 0 */
/* 88 */	NdrFcShort( 0x14a ),	/* 330 */
/* 90 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x2,		/* 2 */
/* 92 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 94 */	NdrFcShort( 0x0 ),	/* 0 */
/* 96 */	NdrFcShort( 0x0 ),	/* 0 */
/* 98 */	NdrFcShort( 0x0 ),	/* 0 */
/* 100 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter authInfo */

/* 102 */	NdrFcShort( 0x112 ),	/* Flags:  must free, out, simple ref, */
/* 104 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 106 */	NdrFcShort( 0xc ),	/* Type Offset=12 */

	/* Return value */

/* 108 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 110 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 112 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Procedure Login */

/* 114 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 116 */	NdrFcLong( 0x0 ),	/* 0 */
/* 120 */	NdrFcShort( 0x3 ),	/* 3 */
/* 122 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 124 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 126 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 128 */	NdrFcShort( 0x0 ),	/* 0 */
/* 130 */	NdrFcShort( 0x14a ),	/* 330 */
/* 132 */	0x46,		/* Oi2 Flags:  clt must size, has return, has ext, */
			0x4,		/* 4 */
/* 134 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 136 */	NdrFcShort( 0x0 ),	/* 0 */
/* 138 */	NdrFcShort( 0x0 ),	/* 0 */
/* 140 */	NdrFcShort( 0x0 ),	/* 0 */
/* 142 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter username */

/* 144 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 146 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 148 */	NdrFcShort( 0x1a ),	/* Type Offset=26 */

	/* Parameter password */

/* 150 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 152 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 154 */	NdrFcShort( 0x1a ),	/* Type Offset=26 */

	/* Parameter authInfo */

/* 156 */	NdrFcShort( 0x112 ),	/* Flags:  must free, out, simple ref, */
/* 158 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 160 */	NdrFcShort( 0xc ),	/* Type Offset=12 */

	/* Return value */

/* 162 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 164 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 166 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Procedure Logout */

/* 168 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 170 */	NdrFcLong( 0x0 ),	/* 0 */
/* 174 */	NdrFcShort( 0x4 ),	/* 4 */
/* 176 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 178 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 180 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 182 */	NdrFcShort( 0x0 ),	/* 0 */
/* 184 */	NdrFcShort( 0x6 ),	/* 6 */
/* 186 */	0x44,		/* Oi2 Flags:  has return, has ext, */
			0x1,		/* 1 */
/* 188 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 190 */	NdrFcShort( 0x0 ),	/* 0 */
/* 192 */	NdrFcShort( 0x0 ),	/* 0 */
/* 194 */	NdrFcShort( 0x0 ),	/* 0 */
/* 196 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Return value */

/* 198 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 200 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 202 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Procedure GetLicenseState */

/* 204 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 206 */	NdrFcLong( 0x0 ),	/* 0 */
/* 210 */	NdrFcShort( 0x5 ),	/* 5 */
/* 212 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 214 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 216 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 218 */	NdrFcShort( 0x10 ),	/* 16 */
/* 220 */	NdrFcShort( 0x6 ),	/* 6 */
/* 222 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x4,		/* 4 */
/* 224 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 226 */	NdrFcShort( 0x0 ),	/* 0 */
/* 228 */	NdrFcShort( 0x0 ),	/* 0 */
/* 230 */	NdrFcShort( 0x0 ),	/* 0 */
/* 232 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter productId */

/* 234 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 236 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 238 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Parameter deviceMac */

/* 240 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 242 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 244 */	NdrFcShort( 0x1a ),	/* Type Offset=26 */

	/* Parameter licenseInfo */

/* 246 */	NdrFcShort( 0x8113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=32 */
/* 248 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 250 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 252 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 254 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 256 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Procedure ActivateProduct */

/* 258 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 260 */	NdrFcLong( 0x0 ),	/* 0 */
/* 264 */	NdrFcShort( 0x6 ),	/* 6 */
/* 266 */	NdrFcShort( 0x38 ),	/* X64 Stack size/offset = 56 */
/* 268 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 270 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 272 */	NdrFcShort( 0x10 ),	/* 16 */
/* 274 */	NdrFcShort( 0x6 ),	/* 6 */
/* 276 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x6,		/* 6 */
/* 278 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 280 */	NdrFcShort( 0x0 ),	/* 0 */
/* 282 */	NdrFcShort( 0x0 ),	/* 0 */
/* 284 */	NdrFcShort( 0x0 ),	/* 0 */
/* 286 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter activationKey */

/* 288 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 290 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 292 */	NdrFcShort( 0x1a ),	/* Type Offset=26 */

	/* Parameter productId */

/* 294 */	NdrFcShort( 0x48 ),	/* Flags:  in, base type, */
/* 296 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 298 */	0xb,		/* FC_HYPER */
			0x0,		/* 0 */

	/* Parameter deviceName */

/* 300 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 302 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 304 */	NdrFcShort( 0x1a ),	/* Type Offset=26 */

	/* Parameter deviceMac */

/* 306 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 308 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 310 */	NdrFcShort( 0x1a ),	/* Type Offset=26 */

	/* Parameter licenseInfo */

/* 312 */	NdrFcShort( 0x8113 ),	/* Flags:  must size, must free, out, simple ref, srv alloc size=32 */
/* 314 */	NdrFcShort( 0x28 ),	/* X64 Stack size/offset = 40 */
/* 316 */	NdrFcShort( 0x20 ),	/* Type Offset=32 */

	/* Return value */

/* 318 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 320 */	NdrFcShort( 0x30 ),	/* X64 Stack size/offset = 48 */
/* 322 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Procedure ScanFile */

/* 324 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 326 */	NdrFcLong( 0x0 ),	/* 0 */
/* 330 */	NdrFcShort( 0x7 ),	/* 7 */
/* 332 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 334 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 336 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 338 */	NdrFcShort( 0x0 ),	/* 0 */
/* 340 */	NdrFcShort( 0x6 ),	/* 6 */
/* 342 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 344 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 346 */	NdrFcShort( 0x0 ),	/* 0 */
/* 348 */	NdrFcShort( 0x0 ),	/* 0 */
/* 350 */	NdrFcShort( 0x0 ),	/* 0 */
/* 352 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter path */

/* 354 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 356 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 358 */	NdrFcShort( 0x1a ),	/* Type Offset=26 */

	/* Parameter scanResult */

/* 360 */	NdrFcShort( 0x113 ),	/* Flags:  must size, must free, out, simple ref, */
/* 362 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 364 */	NdrFcShort( 0x46 ),	/* Type Offset=70 */

	/* Return value */

/* 366 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 368 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 370 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Procedure ScanDirectory */

/* 372 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 374 */	NdrFcLong( 0x0 ),	/* 0 */
/* 378 */	NdrFcShort( 0x8 ),	/* 8 */
/* 380 */	NdrFcShort( 0x20 ),	/* X64 Stack size/offset = 32 */
/* 382 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 384 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 386 */	NdrFcShort( 0x0 ),	/* 0 */
/* 388 */	NdrFcShort( 0x6 ),	/* 6 */
/* 390 */	0x47,		/* Oi2 Flags:  srv must size, clt must size, has return, has ext, */
			0x3,		/* 3 */
/* 392 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 394 */	NdrFcShort( 0x0 ),	/* 0 */
/* 396 */	NdrFcShort( 0x0 ),	/* 0 */
/* 398 */	NdrFcShort( 0x0 ),	/* 0 */
/* 400 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter path */

/* 402 */	NdrFcShort( 0x10b ),	/* Flags:  must size, must free, in, simple ref, */
/* 404 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 406 */	NdrFcShort( 0x1a ),	/* Type Offset=26 */

	/* Parameter scanResult */

/* 408 */	NdrFcShort( 0x113 ),	/* Flags:  must size, must free, out, simple ref, */
/* 410 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 412 */	NdrFcShort( 0x7c ),	/* Type Offset=124 */

	/* Return value */

/* 414 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 416 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 418 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Procedure ScanFixedDrives */

/* 420 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 422 */	NdrFcLong( 0x0 ),	/* 0 */
/* 426 */	NdrFcShort( 0x9 ),	/* 9 */
/* 428 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 430 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 432 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 434 */	NdrFcShort( 0x0 ),	/* 0 */
/* 436 */	NdrFcShort( 0x6 ),	/* 6 */
/* 438 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 440 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 442 */	NdrFcShort( 0x0 ),	/* 0 */
/* 444 */	NdrFcShort( 0x0 ),	/* 0 */
/* 446 */	NdrFcShort( 0x0 ),	/* 0 */
/* 448 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter scanResult */

/* 450 */	NdrFcShort( 0x113 ),	/* Flags:  must size, must free, out, simple ref, */
/* 452 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 454 */	NdrFcShort( 0x7c ),	/* Type Offset=124 */

	/* Return value */

/* 456 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 458 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 460 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

	/* Procedure GetAvDatabaseInfo */

/* 462 */	0x0,		/* 0 */
			0x48,		/* Old Flags:  */
/* 464 */	NdrFcLong( 0x0 ),	/* 0 */
/* 468 */	NdrFcShort( 0xa ),	/* 10 */
/* 470 */	NdrFcShort( 0x18 ),	/* X64 Stack size/offset = 24 */
/* 472 */	0x32,		/* FC_BIND_PRIMITIVE */
			0x0,		/* 0 */
/* 474 */	NdrFcShort( 0x0 ),	/* X64 Stack size/offset = 0 */
/* 476 */	NdrFcShort( 0x0 ),	/* 0 */
/* 478 */	NdrFcShort( 0x6 ),	/* 6 */
/* 480 */	0x45,		/* Oi2 Flags:  srv must size, has return, has ext, */
			0x2,		/* 2 */
/* 482 */	0xa,		/* 10 */
			0x1,		/* Ext Flags:  new corr desc, */
/* 484 */	NdrFcShort( 0x0 ),	/* 0 */
/* 486 */	NdrFcShort( 0x0 ),	/* 0 */
/* 488 */	NdrFcShort( 0x0 ),	/* 0 */
/* 490 */	NdrFcShort( 0x0 ),	/* 0 */

	/* Parameter databaseInfo */

/* 492 */	NdrFcShort( 0x113 ),	/* Flags:  must size, must free, out, simple ref, */
/* 494 */	NdrFcShort( 0x8 ),	/* X64 Stack size/offset = 8 */
/* 496 */	NdrFcShort( 0xa0 ),	/* Type Offset=160 */

	/* Return value */

/* 498 */	NdrFcShort( 0x70 ),	/* Flags:  out, return, base type, */
/* 500 */	NdrFcShort( 0x10 ),	/* X64 Stack size/offset = 16 */
/* 502 */	0xd,		/* FC_ENUM16 */
			0x0,		/* 0 */

			0x0
        }
    };

static const RpcContract_MIDL_TYPE_FORMAT_STRING RpcContract__MIDL_TypeFormatString =
    {
        0,
        {
			NdrFcShort( 0x0 ),	/* 0 */
/*  2 */
			0x11, 0x0,	/* FC_RP */
/*  4 */	NdrFcShort( 0x8 ),	/* Offset= 8 (12) */
/*  6 */
			0x1d,		/* FC_SMFARRAY */
			0x1,		/* 1 */
/*  8 */	NdrFcShort( 0x100 ),	/* 256 */
/* 10 */	0x5,		/* FC_WCHAR */
			0x5b,		/* FC_END */
/* 12 */
			0x15,		/* FC_STRUCT */
			0x7,		/* 7 */
/* 14 */	NdrFcShort( 0x110 ),	/* 272 */
/* 16 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 18 */	0xb,		/* FC_HYPER */
			0x4c,		/* FC_EMBEDDED_COMPLEX */
/* 20 */	0x0,		/* 0 */
			NdrFcShort( 0xfff1 ),	/* Offset= -15 (6) */
			0x5b,		/* FC_END */
/* 24 */
			0x11, 0x8,	/* FC_RP [simple_pointer] */
/* 26 */
			0x25,		/* FC_C_WSTRING */
			0x5c,		/* FC_PAD */
/* 28 */
			0x11, 0x4,	/* FC_RP [alloced_on_stack] */
/* 30 */	NdrFcShort( 0x2 ),	/* Offset= 2 (32) */
/* 32 */
			0x1a,		/* FC_BOGUS_STRUCT */
			0x7,		/* 7 */
/* 34 */	NdrFcShort( 0x20 ),	/* 32 */
/* 36 */	NdrFcShort( 0x0 ),	/* 0 */
/* 38 */	NdrFcShort( 0x0 ),	/* Offset= 0 (38) */
/* 40 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 42 */	0x8,		/* FC_LONG */
			0x8,		/* FC_LONG */
/* 44 */	0xb,		/* FC_HYPER */
			0xd,		/* FC_ENUM16 */
/* 46 */	0x40,		/* FC_STRUCTPAD4 */
			0x5b,		/* FC_END */
/* 48 */
			0x11, 0x0,	/* FC_RP */
/* 50 */	NdrFcShort( 0x14 ),	/* Offset= 20 (70) */
/* 52 */
			0x1d,		/* FC_SMFARRAY */
			0x1,		/* 1 */
/* 54 */	NdrFcShort( 0x208 ),	/* 520 */
/* 56 */	0x5,		/* FC_WCHAR */
			0x5b,		/* FC_END */
/* 58 */
			0x1d,		/* FC_SMFARRAY */
			0x1,		/* 1 */
/* 60 */	NdrFcShort( 0x80 ),	/* 128 */
/* 62 */	0x5,		/* FC_WCHAR */
			0x5b,		/* FC_END */
/* 64 */
			0x1d,		/* FC_SMFARRAY */
			0x1,		/* 1 */
/* 66 */	NdrFcShort( 0x200 ),	/* 512 */
/* 68 */	0x5,		/* FC_WCHAR */
			0x5b,		/* FC_END */
/* 70 */
			0x1a,		/* FC_BOGUS_STRUCT */
			0x7,		/* 7 */
/* 72 */	NdrFcShort( 0x518 ),	/* 1304 */
/* 74 */	NdrFcShort( 0x0 ),	/* 0 */
/* 76 */	NdrFcShort( 0x0 ),	/* Offset= 0 (76) */
/* 78 */	0xd,		/* FC_ENUM16 */
			0x4c,		/* FC_EMBEDDED_COMPLEX */
/* 80 */	0x0,		/* 0 */
			NdrFcShort( 0xffe3 ),	/* Offset= -29 (52) */
			0xd,		/* FC_ENUM16 */
/* 84 */	0xb,		/* FC_HYPER */
			0x4c,		/* FC_EMBEDDED_COMPLEX */
/* 86 */	0x0,		/* 0 */
			NdrFcShort( 0xffe3 ),	/* Offset= -29 (58) */
			0x4c,		/* FC_EMBEDDED_COMPLEX */
/* 90 */	0x0,		/* 0 */
			NdrFcShort( 0xffdf ),	/* Offset= -33 (58) */
			0x4c,		/* FC_EMBEDDED_COMPLEX */
/* 94 */	0x0,		/* 0 */
			NdrFcShort( 0xffe1 ),	/* Offset= -31 (64) */
			0x5b,		/* FC_END */
/* 98 */
			0x11, 0x0,	/* FC_RP */
/* 100 */	NdrFcShort( 0x18 ),	/* Offset= 24 (124) */
/* 102 */
			0x21,		/* FC_BOGUS_ARRAY */
			0x7,		/* 7 */
/* 104 */	NdrFcShort( 0x20 ),	/* 32 */
/* 106 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 110 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 112 */	NdrFcLong( 0xffffffff ),	/* -1 */
/* 116 */	NdrFcShort( 0x0 ),	/* Corr flags:  */
/* 118 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 120 */	NdrFcShort( 0xffce ),	/* Offset= -50 (70) */
/* 122 */	0x5c,		/* FC_PAD */
			0x5b,		/* FC_END */
/* 124 */
			0x1a,		/* FC_BOGUS_STRUCT */
			0x7,		/* 7 */
/* 126 */	NdrFcShort( 0xa728 ),	/* -22744 */
/* 128 */	NdrFcShort( 0x0 ),	/* 0 */
/* 130 */	NdrFcShort( 0x0 ),	/* Offset= 0 (130) */
/* 132 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 134 */	NdrFcShort( 0xffae ),	/* Offset= -82 (52) */
/* 136 */	0xb,		/* FC_HYPER */
			0xb,		/* FC_HYPER */
/* 138 */	0xb,		/* FC_HYPER */
			0x8,		/* FC_LONG */
/* 140 */	0x8,		/* FC_LONG */
			0x4c,		/* FC_EMBEDDED_COMPLEX */
/* 142 */	0x0,		/* 0 */
			NdrFcShort( 0xffd7 ),	/* Offset= -41 (102) */
			0x4c,		/* FC_EMBEDDED_COMPLEX */
/* 146 */	0x0,		/* 0 */
			NdrFcShort( 0xffad ),	/* Offset= -83 (64) */
			0x5b,		/* FC_END */
/* 150 */
			0x11, 0x0,	/* FC_RP */
/* 152 */	NdrFcShort( 0x8 ),	/* Offset= 8 (160) */
/* 154 */
			0x1d,		/* FC_SMFARRAY */
			0x1,		/* 1 */
/* 156 */	NdrFcShort( 0x40 ),	/* 64 */
/* 158 */	0x5,		/* FC_WCHAR */
			0x5b,		/* FC_END */
/* 160 */
			0x1a,		/* FC_BOGUS_STRUCT */
			0x7,		/* 7 */
/* 162 */	NdrFcShort( 0x210 ),	/* 528 */
/* 164 */	NdrFcShort( 0x0 ),	/* 0 */
/* 166 */	NdrFcShort( 0x0 ),	/* Offset= 0 (166) */
/* 168 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 170 */	0xb,		/* FC_HYPER */
			0x8,		/* FC_LONG */
/* 172 */	0x40,		/* FC_STRUCTPAD4 */
			0xb,		/* FC_HYPER */
/* 174 */	0xb,		/* FC_HYPER */
			0xd,		/* FC_ENUM16 */
/* 176 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 178 */	NdrFcShort( 0xffe8 ),	/* Offset= -24 (154) */
/* 180 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 182 */	NdrFcShort( 0xff50 ),	/* Offset= -176 (6) */
/* 184 */	0x4c,		/* FC_EMBEDDED_COMPLEX */
			0x0,		/* 0 */
/* 186 */	NdrFcShort( 0xff80 ),	/* Offset= -128 (58) */
/* 188 */	0x40,		/* FC_STRUCTPAD4 */
			0xb,		/* FC_HYPER */
/* 190 */	0x8,		/* FC_LONG */
			0x40,		/* FC_STRUCTPAD4 */
/* 192 */	0xb,		/* FC_HYPER */
			0x8,		/* FC_LONG */
/* 194 */	0x8,		/* FC_LONG */
			0x5b,		/* FC_END */

			0x0
        }
    };

static const unsigned short TrayRpcControl_FormatStringOffsetTable[] =
    {
    0,
    36,
    72,
    114,
    168,
    204,
    258,
    324,
    372,
    420,
    462
    };


#ifdef __cplusplus
namespace {
#endif
static const MIDL_STUB_DESC TrayRpcControl_StubDesc =
    {
    (void *)& TrayRpcControl___RpcClientInterface,
    MIDL_user_allocate,
    MIDL_user_free,
    &TrayRpcControl__MIDL_AutoBindHandle,
    0,
    0,
    0,
    0,
    RpcContract__MIDL_TypeFormatString.Format,
    1, /* -error bounds_check flag */
    0x50002, /* Ndr library version */
    0,
    0x8010274, /* MIDL Version 8.1.628 */
    0,
    0,
    0,  /* notify & notify_flag routine table */
    0x1, /* MIDL flag */
    0, /* cs routines */
    0,   /* proxy/server info */
    0
    };
#ifdef __cplusplus
}
#endif
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif


#endif /* defined(_M_AMD64)*/

