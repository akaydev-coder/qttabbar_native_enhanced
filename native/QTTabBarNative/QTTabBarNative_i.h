

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Tue Jan 19 04:14:07 2038
 */
/* Compiler settings for QTTabBarNative.idl:
    Oicf, W1, Zp8, env=Win32 (32b run), target_arch=X86 8.01.0628
    protocol : dce , ms_ext, c_ext, robust
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


#ifndef __QTTabBarNative_i_h__
#define __QTTabBarNative_i_h__

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

#ifndef __IQTTabBarClass_FWD_DEFINED__
#define __IQTTabBarClass_FWD_DEFINED__
typedef interface IQTTabBarClass IQTTabBarClass;

#endif 	/* __IQTTabBarClass_FWD_DEFINED__ */


#ifndef __IQTButtonBar_FWD_DEFINED__
#define __IQTButtonBar_FWD_DEFINED__
typedef interface IQTButtonBar IQTButtonBar;

#endif 	/* __IQTButtonBar_FWD_DEFINED__ */


#ifndef __QTTabBarClass_FWD_DEFINED__
#define __QTTabBarClass_FWD_DEFINED__

#ifdef __cplusplus
typedef class QTTabBarClass QTTabBarClass;
#else
typedef struct QTTabBarClass QTTabBarClass;
#endif /* __cplusplus */

#endif 	/* __QTTabBarClass_FWD_DEFINED__ */


#ifndef __IQTSecondViewBar_FWD_DEFINED__
#define __IQTSecondViewBar_FWD_DEFINED__
typedef interface IQTSecondViewBar IQTSecondViewBar;

#endif 	/* __IQTSecondViewBar_FWD_DEFINED__ */


#ifndef __QTSecondViewBar_FWD_DEFINED__
#define __QTSecondViewBar_FWD_DEFINED__

#ifdef __cplusplus
typedef class QTSecondViewBar QTSecondViewBar;
#else
typedef struct QTSecondViewBar QTSecondViewBar;
#endif /* __cplusplus */

#endif 	/* __QTSecondViewBar_FWD_DEFINED__ */


#ifndef __QTButtonBar_FWD_DEFINED__
#define __QTButtonBar_FWD_DEFINED__

#ifdef __cplusplus
typedef class QTButtonBar QTButtonBar;
#else
typedef struct QTButtonBar QTButtonBar;
#endif /* __cplusplus */

#endif 	/* __QTButtonBar_FWD_DEFINED__ */


#ifndef __IQTDesktopTool_FWD_DEFINED__
#define __IQTDesktopTool_FWD_DEFINED__
typedef interface IQTDesktopTool IQTDesktopTool;

#endif 	/* __IQTDesktopTool_FWD_DEFINED__ */


#ifndef __QTDesktopTool_FWD_DEFINED__
#define __QTDesktopTool_FWD_DEFINED__

#ifdef __cplusplus
typedef class QTDesktopTool QTDesktopTool;
#else
typedef struct QTDesktopTool QTDesktopTool;
#endif /* __cplusplus */

#endif 	/* __QTDesktopTool_FWD_DEFINED__ */


#ifndef __AutoLoaderNative_FWD_DEFINED__
#define __AutoLoaderNative_FWD_DEFINED__

#ifdef __cplusplus
typedef class AutoLoaderNative AutoLoaderNative;
#else
typedef struct AutoLoaderNative AutoLoaderNative;
#endif /* __cplusplus */

#endif 	/* __AutoLoaderNative_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

#ifdef __cplusplus
extern "C"{
#endif 



#ifndef __QTTabBarNativeLib_LIBRARY_DEFINED__
#define __QTTabBarNativeLib_LIBRARY_DEFINED__

/* library QTTabBarNativeLib */
/* [version][uuid] */ 


EXTERN_C const IID LIBID_QTTabBarNativeLib;

#ifndef __IQTTabBarClass_INTERFACE_DEFINED__
#define __IQTTabBarClass_INTERFACE_DEFINED__

/* interface IQTTabBarClass */
/* [object][uuid] */ 


EXTERN_C const IID IID_IQTTabBarClass;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E4F0E46A-1EAC-4F67-8C47-58C672A6433B")
    IQTTabBarClass : public IUnknown
    {
    public:
    };
    
    
#else 	/* C style interface */

    typedef struct IQTTabBarClassVtbl
    {
        BEGIN_INTERFACE
        
        DECLSPEC_XFGVIRT(IUnknown, QueryInterface)
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IQTTabBarClass * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            _COM_Outptr_  void **ppvObject);
        
        DECLSPEC_XFGVIRT(IUnknown, AddRef)
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IQTTabBarClass * This);
        
        DECLSPEC_XFGVIRT(IUnknown, Release)
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IQTTabBarClass * This);
        
        END_INTERFACE
    } IQTTabBarClassVtbl;

    interface IQTTabBarClass
    {
        CONST_VTBL struct IQTTabBarClassVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IQTTabBarClass_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IQTTabBarClass_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IQTTabBarClass_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IQTTabBarClass_INTERFACE_DEFINED__ */


#ifndef __IQTButtonBar_INTERFACE_DEFINED__
#define __IQTButtonBar_INTERFACE_DEFINED__

/* interface IQTButtonBar */
/* [object][uuid] */ 


EXTERN_C const IID IID_IQTButtonBar;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E4F0E46A-1EAC-4F67-8C47-58C672A6433C")
    IQTButtonBar : public IUnknown
    {
    public:
    };
    
    
#else 	/* C style interface */

    typedef struct IQTButtonBarVtbl
    {
        BEGIN_INTERFACE
        
        DECLSPEC_XFGVIRT(IUnknown, QueryInterface)
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IQTButtonBar * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            _COM_Outptr_  void **ppvObject);
        
        DECLSPEC_XFGVIRT(IUnknown, AddRef)
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IQTButtonBar * This);
        
        DECLSPEC_XFGVIRT(IUnknown, Release)
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IQTButtonBar * This);
        
        END_INTERFACE
    } IQTButtonBarVtbl;

    interface IQTButtonBar
    {
        CONST_VTBL struct IQTButtonBarVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IQTButtonBar_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IQTButtonBar_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IQTButtonBar_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IQTButtonBar_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_QTTabBarClass;

#ifdef __cplusplus

class DECLSPEC_UUID("D2BF470E-ED1C-487F-A333-2BD8835EB6CE")
QTTabBarClass;
#endif

#ifndef __IQTSecondViewBar_INTERFACE_DEFINED__
#define __IQTSecondViewBar_INTERFACE_DEFINED__

/* interface IQTSecondViewBar */
/* [object][uuid] */ 


EXTERN_C const IID IID_IQTSecondViewBar;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("724B8F87-69C4-4B78-944F-F13D2AD33B71")
    IQTSecondViewBar : public IUnknown
    {
    public:
    };
    
    
#else 	/* C style interface */

    typedef struct IQTSecondViewBarVtbl
    {
        BEGIN_INTERFACE
        
        DECLSPEC_XFGVIRT(IUnknown, QueryInterface)
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IQTSecondViewBar * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            _COM_Outptr_  void **ppvObject);
        
        DECLSPEC_XFGVIRT(IUnknown, AddRef)
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IQTSecondViewBar * This);
        
        DECLSPEC_XFGVIRT(IUnknown, Release)
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IQTSecondViewBar * This);
        
        END_INTERFACE
    } IQTSecondViewBarVtbl;

    interface IQTSecondViewBar
    {
        CONST_VTBL struct IQTSecondViewBarVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IQTSecondViewBar_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IQTSecondViewBar_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IQTSecondViewBar_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IQTSecondViewBar_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_QTSecondViewBar;

#ifdef __cplusplus

class DECLSPEC_UUID("D2BF470E-ED1C-487F-A88A-2BD8835EB6CE")
QTSecondViewBar;
#endif

EXTERN_C const CLSID CLSID_QTButtonBar;

#ifdef __cplusplus

class DECLSPEC_UUID("D2BF470E-ED1C-487F-A666-2BD8835EB6CE")
QTButtonBar;
#endif

#ifndef __IQTDesktopTool_INTERFACE_DEFINED__
#define __IQTDesktopTool_INTERFACE_DEFINED__

/* interface IQTDesktopTool */
/* [object][uuid] */ 


EXTERN_C const IID IID_IQTDesktopTool;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("FA1C427D-ACAB-4CFB-AC87-0180258FF0C4")
    IQTDesktopTool : public IUnknown
    {
    public:
    };
    
    
#else 	/* C style interface */

    typedef struct IQTDesktopToolVtbl
    {
        BEGIN_INTERFACE
        
        DECLSPEC_XFGVIRT(IUnknown, QueryInterface)
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IQTDesktopTool * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            _COM_Outptr_  void **ppvObject);
        
        DECLSPEC_XFGVIRT(IUnknown, AddRef)
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IQTDesktopTool * This);
        
        DECLSPEC_XFGVIRT(IUnknown, Release)
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IQTDesktopTool * This);
        
        END_INTERFACE
    } IQTDesktopToolVtbl;

    interface IQTDesktopTool
    {
        CONST_VTBL struct IQTDesktopToolVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IQTDesktopTool_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IQTDesktopTool_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IQTDesktopTool_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IQTDesktopTool_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_QTDesktopTool;

#ifdef __cplusplus

class DECLSPEC_UUID("D2BF470E-ED1C-487F-A555-2BD8835EB6CE")
QTDesktopTool;
#endif

EXTERN_C const CLSID CLSID_AutoLoaderNative;

#ifdef __cplusplus

class DECLSPEC_UUID("D2BF470E-ED1C-487F-A777-2BD8835EB6CE")
AutoLoaderNative;
#endif
#endif /* __QTTabBarNativeLib_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


