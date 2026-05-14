

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Tue Jan 19 06:14:07 2038
 */
/* Compiler settings for ..\Shared\RpcContract.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=AMD64 8.01.0628 
    protocol : all , ms_ext, app_config, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */



/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 500
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */


#ifndef __RpcContract_h__
#define __RpcContract_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#ifndef DECLSPEC_XFGVIRT
#if defined(_CONTROL_FLOW_GUARD_XFG)
#define DECLSPEC_XFGVIRT(base, func) __declspec(xfg_virtual(base, func))
#else
#define DECLSPEC_XFGVIRT(base, func)
#endif
#endif

/* Forward Declarations */ 

#ifdef __cplusplus
extern "C"{
#endif 


#ifndef __TrayRpcControl_INTERFACE_DEFINED__
#define __TrayRpcControl_INTERFACE_DEFINED__

/* interface TrayRpcControl */
/* [endpoint][unique][version][uuid] */ 

typedef 
enum TrayRpcStopResult
    {
        TRAY_RPC_STOP_APPROVED	= 0,
        TRAY_RPC_STOP_REJECTED	= 1,
        TRAY_RPC_STOP_FAILED	= 2,
        TRAY_RPC_STOP_CONFIRMATION_REQUIRED	= 3
    } 	TrayRpcStopResult;

typedef 
enum TrayRpcStatusCode
    {
        TRAY_RPC_OK	= 0,
        TRAY_RPC_INVALID_ARGUMENT	= 1,
        TRAY_RPC_NOT_AUTHENTICATED	= 2,
        TRAY_RPC_AUTH_FAILED	= 3,
        TRAY_RPC_NETWORK_ERROR	= 4,
        TRAY_RPC_SERVER_ERROR	= 5,
        TRAY_RPC_NO_LICENSE	= 6,
        TRAY_RPC_LICENSE_EXPIRED	= 7,
        TRAY_RPC_LICENSE_BLOCKED	= 8
    } 	TrayRpcStatusCode;

typedef 
enum TrayRpcAvScanVerdict
    {
        TRAY_RPC_AV_SCAN_CLEAN	= 0,
        TRAY_RPC_AV_SCAN_INFECTED	= 1,
        TRAY_RPC_AV_SCAN_ERROR	= 2
    } 	TrayRpcAvScanVerdict;

typedef 
enum TrayRpcAvObjectType
    {
        TRAY_RPC_AV_OBJECT_UNKNOWN	= 0,
        TRAY_RPC_AV_OBJECT_PE	= 1,
        TRAY_RPC_AV_OBJECT_SCRIPT_TEXT	= 2
    } 	TrayRpcAvObjectType;

typedef 
enum TrayRpcAvDatabaseLoadStatus
    {
        TRAY_RPC_AV_DATABASE_NOT_LOADED	= 0,
        TRAY_RPC_AV_DATABASE_LOADED	= 1
    } 	TrayRpcAvDatabaseLoadStatus;

typedef struct TrayRpcAuthInfo
    {
    int authenticated;
    int hasUserId;
    hyper userId;
    wchar_t username[ 128 ];
    } 	TrayRpcAuthInfo;

typedef struct TrayRpcLicenseInfo
    {
    int hasLicense;
    int blocked;
    int expired;
    int hasExpirationEpochSeconds;
    hyper expirationEpochSeconds;
    TrayRpcStatusCode errorCode;
    } 	TrayRpcLicenseInfo;

typedef struct TrayRpcAvDatabaseInfo
    {
    int hasReleaseDate;
    hyper releaseEpochSeconds;
    int hasLastSuccessfulLoad;
    hyper lastSuccessfulLoadEpochSeconds;
    hyper recordCount;
    TrayRpcAvDatabaseLoadStatus loadStatus;
    wchar_t source[ 32 ];
    wchar_t lastUpdateStatus[ 128 ];
    } 	TrayRpcAvDatabaseInfo;

typedef struct TrayRpcAvFileScanResult
    {
    TrayRpcAvScanVerdict verdict;
    wchar_t path[ 260 ];
    TrayRpcAvObjectType objectType;
    hyper detectionOffset;
    wchar_t recordId[ 64 ];
    wchar_t objectSignatureHex[ 64 ];
    wchar_t message[ 256 ];
    } 	TrayRpcAvFileScanResult;

typedef struct TrayRpcAvDirectoryScanResult
    {
    wchar_t path[ 260 ];
    hyper totalScanned;
    hyper infectedCount;
    hyper errorCount;
    int resultCount;
    int truncated;
    TrayRpcAvFileScanResult results[ 32 ];
    wchar_t message[ 256 ];
    } 	TrayRpcAvDirectoryScanResult;

TrayRpcStopResult StopService( 
    /* [in] */ handle_t hBinding);

TrayRpcStopResult ConfirmStopService( 
    /* [in] */ handle_t hBinding);

TrayRpcStatusCode GetAuthInfo( 
    /* [in] */ handle_t hBinding,
    /* [out] */ TrayRpcAuthInfo *authInfo);

TrayRpcStatusCode Login( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ wchar_t *username,
    /* [string][in] */ wchar_t *password,
    /* [out] */ TrayRpcAuthInfo *authInfo);

TrayRpcStatusCode Logout( 
    /* [in] */ handle_t hBinding);

TrayRpcStatusCode GetLicenseState( 
    /* [in] */ handle_t hBinding,
    /* [in] */ hyper productId,
    /* [string][in] */ wchar_t *deviceMac,
    /* [out] */ TrayRpcLicenseInfo *licenseInfo);

TrayRpcStatusCode ActivateProduct( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ wchar_t *activationKey,
    /* [in] */ hyper productId,
    /* [string][in] */ wchar_t *deviceName,
    /* [string][in] */ wchar_t *deviceMac,
    /* [out] */ TrayRpcLicenseInfo *licenseInfo);

TrayRpcStatusCode ScanFile( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ wchar_t *path,
    /* [out] */ TrayRpcAvFileScanResult *scanResult);

TrayRpcStatusCode ScanDirectory( 
    /* [in] */ handle_t hBinding,
    /* [string][in] */ wchar_t *path,
    /* [out] */ TrayRpcAvDirectoryScanResult *scanResult);

TrayRpcStatusCode GetAvDatabaseInfo( 
    /* [in] */ handle_t hBinding,
    /* [out] */ TrayRpcAvDatabaseInfo *databaseInfo);



extern RPC_IF_HANDLE TrayRpcControl_v1_0_c_ifspec;
extern RPC_IF_HANDLE TrayRpcControl_v1_0_s_ifspec;
#endif /* __TrayRpcControl_INTERFACE_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


