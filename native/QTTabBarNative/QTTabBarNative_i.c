

/* this ALWAYS GENERATED file contains the IIDs and CLSIDs */

/* link this file in with the server and any clients */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Tue Jan 19 04:14:07 2038
 */
/* Compiler settings for QTTabBarNative.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=AMD64 8.01.0628 
    protocol : all , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */



#ifdef __cplusplus
extern "C"{
#endif 


#include <rpc.h>
#include <rpcndr.h>

#ifdef _MIDL_USE_GUIDDEF_

#ifndef INITGUID
#define INITGUID
#include <guiddef.h>
#undef INITGUID
#else
#include <guiddef.h>
#endif

#define MIDL_DEFINE_GUID(type,name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) \
        DEFINE_GUID(name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8)

#else // !_MIDL_USE_GUIDDEF_

#ifndef __IID_DEFINED__
#define __IID_DEFINED__

typedef struct _IID
{
    unsigned long x;
    unsigned short s1;
    unsigned short s2;
    unsigned char  c[8];
} IID;

#endif // __IID_DEFINED__

#ifndef CLSID_DEFINED
#define CLSID_DEFINED
typedef IID CLSID;
#endif // CLSID_DEFINED

#define MIDL_DEFINE_GUID(type,name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) \
        EXTERN_C __declspec(selectany) const type name = {l,w1,w2,{b1,b2,b3,b4,b5,b6,b7,b8}}

#endif // !_MIDL_USE_GUIDDEF_

MIDL_DEFINE_GUID(IID, LIBID_QTTabBarNativeLib,0x7AB1C387,0x4B4C,0x457D,0x91,0xC7,0x54,0xB1,0x8A,0x97,0x64,0xB2);


MIDL_DEFINE_GUID(IID, IID_IQTTabBarClass,0xE4F0E46A,0x1EAC,0x4F67,0x8C,0x47,0x58,0xC6,0x72,0xA6,0x43,0x3B);


MIDL_DEFINE_GUID(IID, IID_IQTButtonBar,0xE4F0E46A,0x1EAC,0x4F67,0x8C,0x47,0x58,0xC6,0x72,0xA6,0x43,0x3C);


MIDL_DEFINE_GUID(CLSID, CLSID_QTTabBarClass,0xD2BF470E,0xED1C,0x487F,0xA3,0x33,0x2B,0xD8,0x83,0x5E,0xB6,0xCE);


MIDL_DEFINE_GUID(IID, IID_IQTSecondViewBar,0x724B8F87,0x69C4,0x4B78,0x94,0x4F,0xF1,0x3D,0x2A,0xD3,0x3B,0x71);


MIDL_DEFINE_GUID(CLSID, CLSID_QTSecondViewBar,0xD2BF470E,0xED1C,0x487F,0xA8,0x8A,0x2B,0xD8,0x83,0x5E,0xB6,0xCE);


MIDL_DEFINE_GUID(CLSID, CLSID_QTButtonBar,0xD2BF470E,0xED1C,0x487F,0xA6,0x66,0x2B,0xD8,0x83,0x5E,0xB6,0xCE);


MIDL_DEFINE_GUID(IID, IID_IQTDesktopTool,0xFA1C427D,0xACAB,0x4CFB,0xAC,0x87,0x01,0x80,0x25,0x8F,0xF0,0xC4);


MIDL_DEFINE_GUID(CLSID, CLSID_QTDesktopTool,0xD2BF470E,0xED1C,0x487F,0xA5,0x55,0x2B,0xD8,0x83,0x5E,0xB6,0xCE);


MIDL_DEFINE_GUID(CLSID, CLSID_AutoLoaderNative,0xD2BF470E,0xED1C,0x487F,0xA7,0x77,0x2B,0xD8,0x83,0x5E,0xB6,0xCE);

#undef MIDL_DEFINE_GUID

#ifdef __cplusplus
}
#endif



