#ifndef TWAPI_H
#define TWAPI_H

/*
 * Copyright (c) 2010, Ashok P. Nadkarni
 * All rights reserved.
 *
 * See the file LICENSE for license
 */

/* Enable prototype-less extern functions warnings even at warning level 1 */
#pragma warning (1 : 13)

/*
 * The following is along the lines of the Tcl model for exporting
 * functions.
 *
 * TWAPI_STATIC_BUILD - the TWAPI build should define this to build
 * a static library version of TWAPI. Clients should define this when
 * linking to the static library version of TWAPI.
 *
 * twapi_base_BUILD - the TWAPI core build and ONLY the TWAPI core build
 * should define this, both for static as well dll builds. Clients should never 
 * define it for their builds, nor should other twapi components. (lower
 * case "twapi_base" because it is derived from the build system)
 *
 * USE_TWAPI_STUBS - for future use should not be currently defined.
 */

#ifdef TWAPI_STATIC_BUILD
# define TWAPI_EXPORT
# define TWAPI_IMPORT
#else
# define TWAPI_EXPORT __declspec(dllexport)
# define TWAPI_IMPORT __declspec(dllimport)
#endif

#ifdef twapi_base_BUILD
#   define TWAPI_STORAGE_CLASS TWAPI_EXPORT
#else
#   ifdef USE_TWAPI_STUBS
#      error USE_TWAPI_STUBS not implemented yet.
#      define TWAPI_STORAGE_CLASS
#   else
#      define TWAPI_STORAGE_CLASS TWAPI_IMPORT
#   endif
#endif

#ifdef __cplusplus
#   define TWAPI_EXTERN extern "C" TWAPI_STORAGE_CLASS
#else
#   define TWAPI_EXTERN extern TWAPI_STORAGE_CLASS
#endif



#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <winsock2.h>
#include <windows.h>

//#define _WSPIAPI_COUNTOF // Needed to use VC++ 6.0 with Platform SDK 2003 SP1
#ifndef ARRAYSIZE
/* Older SDK's do not define this */
#define ARRAYSIZE(A) RTL_NUMBER_OF(A)
#endif
#include <ws2tcpip.h>
#include <winsvc.h>
#include <psapi.h>
#include <pdhmsg.h>
#include <sddl.h>
#include <lmerr.h>
#include <lm.h>
#include <limits.h>
#include <errno.h>
#include <lmat.h>
#include <lm.h>
#include <pdh.h>         /* Include AFTER lm.h due to HLOG def conflict */
#include <sddl.h>        /* For SECURITY_DESCRIPTOR <-> string conversions */
#include <aclapi.h>
#include <winnetwk.h>
#include <iphlpapi.h>
#include <objidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <locale.h>
#include <ntsecapi.h>
#include <wtsapi32.h>
#include <uxtheme.h>
#include <tmschema.h>
#include <intshcut.h>
#include <dispex.h>
#include <ocidl.h>
#include <mstask.h>
#include <dsgetdc.h>
#include <powrprof.h>
#if _MSC_VER <= 1400
/* Not present in newer compiler/sdks as it is subsumed by winuser.h */ 
# include <winable.h>
#endif
#define SECURITY_WIN32 1
#include <security.h>
#include <userenv.h>
#include <setupapi.h>
#include <wmistr.h>             /* Needed for WNODE_HEADER for evntrace */
#include <evntrace.h>

#include "tcl.h"

#include "twapi_sdkdefs.h"
#include "twapi_ddkdefs.h"
#include "zlist.h"
#include "memlifo.h"

#if 0
// Do not use for now as it pulls in C RTL _vsnprintf AND docs claim
// needs XP *SP2*.
// Note has to be included after all headers
//#define STRSAFE_LIB             /* Use lib, not inline strsafe functions */
//#include <strsafe.h>
#endif


typedef DWORD WIN32_ERROR;
typedef int TCL_RESULT;

#define TWAPI_TCL_NAMESPACE "twapi"
#define TWAPI_SCRIPT_RESOURCE_TYPE "tclscript"
#define TWAPI_SCRIPT_RESOURCE_TYPE_LZMA "tclscriptlzma"
#define TWAPI_SETTINGS_VAR  TWAPI_TCL_NAMESPACE "::settings"
#define TWAPI_LOG_VAR TWAPI_TCL_NAMESPACE "::log_messages"

#define MAKESTRINGLITERAL(s_) # s_
 /*
  * Stringifying special CPP symbols (__LINE__) needs another level of macro
  */
#define MAKESTRINGLITERAL2(s_) MAKESTRINGLITERAL(s_)

#if TWAPI_ENABLE_ASSERT
#  if TWAPI_ENABLE_ASSERT == 1
#    define TWAPI_ASSERT(bool_) (void)( (bool_) || (Tcl_Panic("Assertion (%s) failed at line %d in file %s.", #bool_, __LINE__, __FILE__), 0) )
#  elif TWAPI_ENABLE_ASSERT == 2
#    define TWAPI_ASSERT(bool_) (void)( (bool_) || (DebugOutput("Assertion (" #bool_ ") failed at line " MAKESTRINGLITERAL2(__LINE__) " in file " __FILE__ "\n"), 0) )
#  elif TWAPI_ENABLE_ASSERT == 3
#    define TWAPI_ASSERT(bool_) do { if (! (bool_)) { __asm int 3 } } while (0)
#  else
#    error Invalid value for TWAPI_ENABLE_ASSERT
#  endif
#else
#define TWAPI_ASSERT(bool_) ((void) 0)
#endif

/*
 * Macro to create a stub to load and return a pointer to a function
 * fnname in dll dllname
 * Example:
 *     MAKE_DYNLOAD_FUNC(ConvertSidToStringSidA, advapi32, FARPROC)
 * will define a function Twapi_GetProc_ConvertSidToStringSidA
 * which can be called to return a pointer to ConvertSidToStringSidA
 * in DLL advapi32.dll or NULL if the function does not exist in the dll
 *
 * Then it can be called as:
 *      FARPROC func = Twapi_GetProc_ConvertSidToStringSidA();
 *      if (func) { (*func)(parameters...);}
 *      else { ... function not present, do whatever... }
 *
 * Note you can pass any function prototype typedef instead of FARPROC
 * for better type safety.
 */

#define MAKE_DYNLOAD_FUNC(fnname, dllname, fntype)       \
    fntype Twapi_GetProc_ ## fnname (void)               \
    { \
        static HINSTANCE dllname ## _H; \
        static fntype  fnname ## dllname ## _F;       \
        if ((fnname ## dllname ## _F) == NULL) { \
            if ((dllname ## _H) == NULL) { \
                dllname ## _H = LoadLibraryA(#dllname ".dll"); \
            } \
            if (dllname ## _H) { \
                fnname ## dllname ## _F = \
                    (fntype) GetProcAddress( dllname ## _H, #fnname); \
            } \
        } \
 \
        return fnname ## dllname ## _F; \
    }

/* Like MAKE_DYNLOAD_FUNC but function name includes dll. Useful in
 * cases where function may be in one of several different DLL's
 */
#define MAKE_DYNLOAD_FUNC2(fnname, dllname) \
    FARPROC Twapi_GetProc_ ## fnname ##_ ## dllname (void) \
    { \
        static HINSTANCE dllname ## _H; \
        static FARPROC   fnname ## dllname ## _F; \
        if ((fnname ## dllname ## _F) == NULL) { \
            if ((dllname ## _H) == NULL) { \
                dllname ## _H = LoadLibraryA(#dllname ".dll"); \
            } \
            if (dllname ## _H) { \
                fnname ## dllname ## _F = \
                    (FARPROC) GetProcAddress( dllname ## _H, #fnname); \
            } \
        } \
 \
        return fnname ## dllname ## _F; \
    }

#define MAKE_DYNLOAD_FUNC_ORDINAL(ord, dllname) \
    FARPROC Twapi_GetProc_ ## dllname ## _ ## ord (void) \
    { \
        static HINSTANCE dllname ## _H; \
        static FARPROC   ord_ ## ord ## dllname ## _F; \
        if ((ord_ ## ord ## dllname ## _F) == NULL) { \
            if ((dllname ## _H) == NULL) { \
                dllname ## _H = LoadLibraryA(#dllname ".dll"); \
            } \
            if (dllname ## _H) { \
                ord_ ## ord ## dllname ## _F = \
                    (FARPROC) GetProcAddress( dllname ## _H, (LPCSTR) ord); \
            } \
        } \
 \
        return ord_ ## ord ## dllname ## _F; \
    }

/* 64 bittedness needs a BOOL version of the FARPROC def */
typedef BOOL (WINAPI *FARPROC_BOOL)();

/*
 * Macros for alignment
 */
#define ALIGNMENT sizeof(__int64)
#define ALIGNMASK (~(INT_PTR)(ALIGNMENT-1))
/* Round up to alignment size */
#define ROUNDUP(x_) (( ALIGNMENT - 1 + (x_)) & ALIGNMASK)
#define ROUNDED(x_) (ROUNDUP(x_) == (x_))
#define ROUNDDOWN(x_) (ALIGNMASK & (x_))

/* Note diff between ALIGNPTR and ADDPTR is that former aligns the pointer */
#define ALIGNPTR(base_, offset_, type_) \
    (type_) ROUNDUP((offset_) + (DWORD_PTR)(base_))
#define ADDPTR(p_, incr_, type_) \
    ((type_)((incr_) + (char *)(p_)))
#define SUBPTR(p_, decr_, type_) \
    ((type_)(((char *)(p_)) - (decr_)))
#define ALIGNED(p_) (ROUNDED((DWORD_PTR)(p_)))

/*
 * Pointer diff assuming difference fits in 32 bits. That should be always
 * true even on 64-bit systems because of our limits on alloc size
 */
#define PTRDIFF32(p_, q_) ((int)((char*)(p_) - (char *)(q_)))

/*
 * Support for one-time initialization 
 */
typedef volatile LONG TwapiOneTimeInitState;
#define TWAPI_INITSTATE_NOT_DONE    0
#define TWAPI_INITSTATE_IN_PROGRESS 1
#define TWAPI_INITSTATE_DONE        2
#define TWAPI_INITSTATE_ERROR       3

/*************************
 * Error code definitions
 *************************

/*
 * String to use as first element of a errorCode
 */
#define TWAPI_WIN32_ERRORCODE_TOKEN "TWAPI_WIN32"  /* For Win32 errors */
#define TWAPI_ERRORCODE_TOKEN       "TWAPI"        /* For TWAPI errors */

/* TWAPI error codes - used with the Tcl error facility */
#define TWAPI_NO_ERROR       0
#define TWAPI_INVALID_ARGS   1
#define TWAPI_BUFFER_OVERRUN 2
#define TWAPI_EXTRA_ARGS     3
#define TWAPI_BAD_ARG_COUNT  4
#define TWAPI_INTERNAL_LIMIT 5
#define TWAPI_INVALID_OPTION 6
#define TWAPI_INVALID_FUNCTION_CODE 7
#define TWAPI_BUG            8
#define TWAPI_UNKNOWN_OBJECT 9
#define TWAPI_SYSTEM_ERROR  10
#define TWAPI_REGISTER_WAIT_FAILED 11
#define TWAPI_BUG_INVALID_STATE_FOR_OP 12
#define TWAPI_OUT_OF_RANGE 13

/*
 * Map TWAPI error codes into Win32 error code format.
 */
#define TWAPI_WIN32_ERROR_FAC 0xABC /* 12-bit facility to distinguish from app */
#define TWAPI_ERROR_TO_WIN32(code) (0xE0000000 | (TWAPI_WIN32_ERROR_FAC < 16) | (code))
#define IS_TWAPI_WIN32_ERROR(code) (((code) >> 16) == (0xe000 | TWAPI_WIN32_ERROR_FAC))
#define TWAPI_WIN32_ERROR_TO_CODE(winerr) ((winerr) & 0x0000ffff)

/**********************
 * Misc utility macros
 **********************/

/* Verify a Tcl_Obj is an integer/long and return error if not */
#define CHECK_INTEGER_OBJ(interp_, intvar_, objp_)       \
    do {                                                                \
        if (Tcl_GetIntFromObj((interp_), (objp_), &(intvar_)) != TCL_OK) \
            return TCL_ERROR;                                           \
    } while (0)

/* String equality test - check first char before calling strcmp */
#define STREQ(x, y) ( (((x)[0]) == ((y)[0])) && ! lstrcmpA((x), (y)) )
#define STREQUN(x, y, n) \
    (((x)[0] == (y)[0]) && (strncmp(x, y, n) == 0))
#define WSTREQ(x, y) ( (((x)[0]) == ((y)[0])) && ! wcscmp((x), (y)) )

/* Make a pointer null if it points to empty element (generally used
   when we want to treat empty strings as null pointers */
#define NULLIFY_EMPTY(s_) if ((s_) && ((s_)[0] == 0)) (s_) = NULL
/* And the converse */
#define EMPTIFY_NULL(s_) if ((s_) == NULL) (s_) = L"";


/**********************************************
 * Macros and definitions dealing with Tcl_Obj 
 **********************************************/
enum {
    TWAPI_TCLTYPE_NONE  = 0,
    TWAPI_TCLTYPE_STRING,
    TWAPI_TCLTYPE_BOOLEAN,
    TWAPI_TCLTYPE_INT,
    TWAPI_TCLTYPE_DOUBLE,
    TWAPI_TCLTYPE_BYTEARRAY,
    TWAPI_TCLTYPE_LIST,
    TWAPI_TCLTYPE_DICT,
    TWAPI_TCLTYPE_WIDEINT,
    TWAPI_TCLTYPE_BOOLEANSTRING,
    TWAPI_TCLTYPE_BOUND
} TwapiTclType;
    


/* Create a string obj from a string literal. */
#define STRING_LITERAL_OBJ(x) Tcl_NewStringObj(x, sizeof(x)-1)



/* Macros to append field name and values to a list */

/* Appends a struct DWORD field name and value pair to a given Tcl list object */
#define Twapi_APPEND_DWORD_FIELD_TO_LIST(interp_, listp_, structp_, field_) \
  do { \
    Tcl_ListObjAppendElement((interp_), (listp_), STRING_LITERAL_OBJ( # field_)); \
    Tcl_ListObjAppendElement((interp_), (listp_), Tcl_NewLongObj((DWORD)((structp_)->field_))); \
  } while (0)

/* Appends a struct ULONGLONG field name and value pair to a given Tcl list object */
#define Twapi_APPEND_ULONGLONG_FIELD_TO_LIST(interp_, listp_, structp_, field_) \
  do { \
    Tcl_ListObjAppendElement((interp_), (listp_), STRING_LITERAL_OBJ( # field_)); \
    Tcl_ListObjAppendElement((interp_), (listp_), Tcl_NewWideIntObj((ULONGLONG)(structp_)->field_)); \
  } while (0)

/* Append a struct SIZE_T field name and value pair to a Tcl list object */
#ifdef _WIN64
#define Twapi_APPEND_SIZE_T_FIELD_TO_LIST Twapi_APPEND_ULONGLONG_FIELD_TO_LIST
#else
#define Twapi_APPEND_SIZE_T_FIELD_TO_LIST Twapi_APPEND_DWORD_FIELD_TO_LIST
#endif
#define Twapi_APPEND_ULONG_PTR_FIELD_TO_LIST Twapi_APPEND_SIZE_T_FIELD_TO_LIST

/* Appends a struct LARGE_INTEGER field name and value pair to a given Tcl list object */
#define Twapi_APPEND_LARGE_INTEGER_FIELD_TO_LIST(interp_, listp_, structp_, field_) \
  do { \
    Tcl_ListObjAppendElement((interp_), (listp_), STRING_LITERAL_OBJ( # field_)); \
    Tcl_ListObjAppendElement((interp_), (listp_), Tcl_NewWideIntObj((structp_)->field_.QuadPart)); \
  } while (0)

/* Appends a struct Unicode field name and string pair to a Tcl list object */
#define Twapi_APPEND_LPCWSTR_FIELD_TO_LIST(interp_, listp_, structp_, field_) \
  do { \
    Tcl_ListObjAppendElement((interp_), (listp_), STRING_LITERAL_OBJ( # field_)); \
    Tcl_ListObjAppendElement((interp_),(listp_), ObjFromUnicodeN(((structp_)->field_ ? (structp_)->field_ : L""), -1)); \
  } while (0)

/* Appends a struct char string field name and string pair to a Tcl list object */
#define Twapi_APPEND_LPCSTR_FIELD_TO_LIST(interp_, listp_, structp_, field_) \
  do { \
    Tcl_ListObjAppendElement((interp_), (listp_), STRING_LITERAL_OBJ( # field_)); \
    Tcl_ListObjAppendElement((interp_),(listp_), Tcl_NewStringObj(((structp_)->field_ ? (structp_)->field_ : ""), -1)); \
  } while (0)


/* Appends a struct Unicode field name and string pair to a Tcl list object */
#define Twapi_APPEND_LSA_UNICODE_FIELD_TO_LIST(interp_, listp_, structp_, field_) \
  do { \
    Tcl_ListObjAppendElement((interp_), (listp_), STRING_LITERAL_OBJ( # field_)); \
    Tcl_ListObjAppendElement((interp_),(listp_), ObjFromLSA_UNICODE_STRING(&((structp_)->field_))); \
  } while (0)

#define Twapi_APPEND_UUID_FIELD_TO_LIST(interp_, listp_, structp_, field_) \
  do { \
    Tcl_ListObjAppendElement((interp_), (listp_), STRING_LITERAL_OBJ( # field_)); \
    Tcl_ListObjAppendElement((interp_),(listp_), ObjFromUUID(&((structp_)->field_))); \
  } while (0)

#define Twapi_APPEND_LUID_FIELD_TO_LIST(interp_, listp_, structp_, field_) \
  do { \
    Tcl_ListObjAppendElement((interp_), (listp_), STRING_LITERAL_OBJ( # field_)); \
    Tcl_ListObjAppendElement((interp_),(listp_), ObjFromLUID(&((structp_)->field_))); \
  } while (0)

#define Twapi_APPEND_PSID_FIELD_TO_LIST(interp_, listp_, structp_, field_) \
  do { \
    Tcl_Obj *obj = ObjFromSIDNoFail((structp_)->field_); \
    Tcl_ListObjAppendElement((interp_), (listp_), STRING_LITERAL_OBJ( # field_)); \
    Tcl_ListObjAppendElement((interp_),(listp_), obj ? obj : Tcl_NewStringObj("", 0)); \
  } while (0)

#define Twapi_APPEND_GUID_FIELD_TO_LIST(interp_, listp_, structp_, field_) \
  do { \
    Tcl_ListObjAppendElement((interp_), (listp_), STRING_LITERAL_OBJ( # field_)); \
    Tcl_ListObjAppendElement((interp_),(listp_), ObjFromGUID(&((structp_)->field_))); \
  } while (0)

/*
 * Macros to build ordered list of names and values of the fields
 * in a struct while maintaining consistency in the order. 
 * See services.i for examples of usage
 */
#define FIELD_NAME_OBJ(x, unused, unused2) STRING_LITERAL_OBJ(# x)
#define FIELD_VALUE_OBJ(field, func, structp) func(structp->field)

/******************************
 * Tcl version dependent stuff
 ******************************/
struct TwapiTclVersion {
    int major;
    int minor;
    int patchlevel;
    int reltype;
};


/*
 * We need to access platform-dependent internal stubs. For
 * example, the Tcl channel system relies on specific values to be used
 * for EAGAIN, EWOULDBLOCK etc. These are actually compiler-dependent.
 * so the only way to make sure we are using a consistent Win32->Posix
 * mapping is to use the internal Tcl mapping function.
 */
struct TwapiTcl85IntPlatStubs {
    int   magic;
    void *hooks;
    void (*tclWinConvertError) (DWORD errCode); /* 0 */
    int (*fn2[29])(); /* Totally 30 fns, (index 0-29) */
};
extern struct TwapiTcl85IntPlatStubs *tclIntPlatStubsPtr;
#define TWAPI_TCL85_INT_PLAT_STUB(fn_) (((struct TwapiTcl85IntPlatStubs *)tclIntPlatStubsPtr)->fn_)


/*******************
 * Misc definitions
 *******************/

/*
 * Type for generating ids. Because they are passed around in windows
 * messages, make them the same size as DWORD_PTR though we would have
 * liked them to be 64 bit even on 32-bit platforms.
 */
typedef DWORD_PTR TwapiId;
#define ObjFromTwapiId ObjFromDWORD_PTR
#define ObjToTwapiId ObjToDWORD_PTR
#define INVALID_TwapiId    0
#define TWAPI_NEWID Twapi_NewId

/* Used to maintain context for common NetEnum* interfaces */
typedef struct _TwapiEnumCtx {
    Tcl_Interp *interp;
    Tcl_Obj    *objP;
} TwapiEnumCtx;

typedef struct {
    int    tag;  /* Type of entries in netbufP[] */
#define TWAPI_NETENUM_USERINFO              0
#define TWAPI_NETENUM_GROUPINFO             1
#define TWAPI_NETENUM_LOCALGROUPINFO        2
#define TWAPI_NETENUM_GROUPUSERSINFO        3
#define TWAPI_NETENUM_LOCALGROUPUSERSINFO   4
#define TWAPI_NETENUM_LOCALGROUPMEMBERSINFO 5
#define TWAPI_NETENUM_SESSIONINFO           6
#define TWAPI_NETENUM_FILEINFO              7
#define TWAPI_NETENUM_CONNECTIONINFO        8
#define TWAPI_NETENUM_SHAREINFO             9
#define TWAPI_NETENUM_USEINFO              10
    LPBYTE netbufP;     /* If non-NULL, points to buffer to be freed
                           with NetApiBufferFree */
    NET_API_STATUS status;
    DWORD level;
    DWORD entriesread;
    DWORD totalentries;
    DWORD_PTR  hresume;
} TwapiNetEnumContext;

typedef void TWAPI_FILEVERINFO;

/****************************************************************
 * Defintitions used for conversion from Tcl_Obj to C types
 ****************************************************************/

/*
 * Used to pass a typed result to TwapiSetResult
 * Do NOT CHANGE VALUES AS THEY MAY ALSO BE REFLECTED IN TCL CODE
 */
typedef enum {
    TRT_BADFUNCTIONCODE = 0,
    TRT_BOOL = 1,
    TRT_EXCEPTION_ON_FALSE = 2,
    TRT_HWND = 3,
    TRT_UNICODE = 4,
    TRT_OBJV = 5,            /* Array of Tcl_Obj * */
    TRT_RECT = 6,
    TRT_HANDLE = 7,
    TRT_CHARS = 8,           /* char string */
    TRT_BINARY = 9,
    TRT_CHARS_DYNAMIC = 10,  /* Char string to be freed through TwapiFree */
    TRT_DWORD = 11,
    TRT_HGLOBAL = 12,
    TRT_NONZERO_RESULT = 13,
    TRT_EXCEPTION_ON_ERROR = 14,
    TRT_HDC = 15,
    TRT_HMONITOR = 16,
    TRT_FILETIME = 17,
    TRT_EMPTY = 18,
    TRT_EXCEPTION_ON_MINUSONE = 19,
    TRT_UUID = 20,
    TRT_LUID = 21,
    TRT_SC_HANDLE = 22,
    TRT_HDESK = 23,
    TRT_HWINSTA = 24,
    TRT_POINT = 25,
    TRT_VALID_HANDLE = 26, // Unlike TRT_HANDLE, NULL is not an error
    TRT_GETLASTERROR = 27,   /* Set result as error code from GetLastError() */
    TRT_EXCEPTION_ON_WNET_ERROR = 28,
    TRT_DWORD_PTR = 29,
    TRT_LPVOID = 30,
    TRT_NONNULL_LPVOID = 31,
    TRT_INTERFACE = 32,         /* COM interface */
    TRT_OBJ = 33,
    TRT_UNINITIALIZED = 34,     /* Error - result not initialized */
    TRT_VARIANT = 35,           /* Must VarientInit before use ! */
    TRT_LPOLESTR = 36,    /* WCHAR string to be freed through CoTaskMemFree
                             Note these are NOT BSTR's
                           */
    TRT_SYSTEMTIME = 37,
    TRT_DOUBLE = 38,
    TRT_GUID = 39,  /* Also use for IID, CLSID; string rep differs from TRT_UUID
 */
    TRT_OPAQUE = 40,
    TRT_TCL_RESULT = 41,             /* Interp result already set. Return ival
                                        field as status */
    TRT_NTSTATUS = 42,
    TRT_LSA_HANDLE = 43,
    TRT_SEC_WINNT_AUTH_IDENTITY = 44,
    TRT_HDEVINFO = 45,
    TRT_PIDL = 46,              /* Freed using CoTaskMemFree */
    TRT_WIDE = 47,              /* Tcl_WideInt */
    TRT_UNICODE_DYNAMIC = 48,     /* Unicode to be freed through TwapiFree */
    TRT_TWAPI_ERROR = 49,          /* TWAPI error code in ival*/
    TRT_HRGN,
    TRT_HMODULE,
} TwapiResultType;

typedef struct {
    TwapiResultType type;
    union {
        int ival;
        double dval;
        BOOL bval;
        DWORD_PTR dwp;
        Tcl_WideInt wide;
        LPVOID pv;
        HANDLE hval;
        HMODULE hmodule;
        HWND hwin;
        struct {
            WCHAR *str;
            int    len;         /* len == -1 if str is null terminated */
        } unicode;
        struct {
            char *str;
            int    len;         /* len == -1 if str is null terminated */
        } chars;
        struct {
            char  *p;
            int    len;
        } binary;
        Tcl_Obj *obj;
        struct {
            int nobj;
            Tcl_Obj **objPP;
        } objv;
        RECT rect;
        POINT point;
        FILETIME filetime;
        UUID uuid;
        GUID guid;              /* Formatted differently from uuid */
        LUID luid;
        struct {
            void *p;
            char *name;
        } ifc;
        struct {
            void *p;
            char *name;
        } opaque;
        VARIANT var;            /* Must VariantInit before use!! */
        LPOLESTR lpolestr; /* WCHAR string to be freed through CoTaskMemFree */
        SYSTEMTIME systime;
        LPITEMIDLIST pidl;
    } value;
} TwapiResult;

#define InitTwapiResult(tr_) do {(tr_)->type = TRT_EMPTY;} while (0)

/*
 * Macros for passing arguments to TwapiGetArgs.
 * v is a variable of the appropriate type.
 * I is an int variable or const
 * n is a variable of type int
 * typestr - is any type string such as "HSERVICE" that indicates the type
 * fn - is a function to call to convert the value. The function
 *   should have the prototype TwapiGetArgFn
 */
#define ARGEND      0
#define ARGTERM     1
#define ARGBOOL    'b'
#define ARGBIN     'B'
#define ARGDOUBLE  'd'
#define ARGNULLIFEMPTY 'E'
#define ARGINT     'i'
#define ARGWIDE    'I'
#define ARGNULLTOKEN 'N'
#define ARGOBJ     'o'
#define ARGPTR      'p'
#define ARGDWORD_PTR 'P'
#define ARGAARGV   'r'
#define ARGWARGV   'R'
#define ARGASTR      's'
#define ARGASTRN     'S'
#define ARGWSTR     'u'
#define ARGWSTRN    'U'
#define ARGVAR     'v'
#define ARGVARWITHDEFAULT 'V'
#define ARGWORD     'w'
#define ARGSKIP     'x'
#define ARGUSEDEFAULT '?'

#define GETBOOL(v)    ARGBOOL, &(v)
#define GETBIN(v, n)  ARGBIN, &(v), &(n)
#define GETINT(v)     ARGINT, &(v)
#define GETWIDE(v)    ARGWIDE, &(v)
#define GETDOUBLE(v)  ARGDOUBLE, &(v)
#define GETOBJ(v)     ARGOBJ, &(v)
#define GETDWORD_PTR(v) ARGDWORD_PTR, &(v)
#define GETASTR(v)      ARGASTR, &(v)
#define GETASTRN(v, n)  ARGASTRN, &(v), &(n)
#define GETWSTR(v)     ARGWSTR, &(v)
#define GETWSTRN(v, n) ARGWSTRN, &(v), &(n)
#define GETNULLIFEMPTY(v) ARGNULLIFEMPTY, &(v)
#define GETNULLTOKEN(v) ARGNULLTOKEN, &(v)
#define GETWORD(v)     ARGWORD, &(v)
#define GETPTR(v, typesym) ARGPTR, &(v), #typesym
#define GETVOIDP(v)    ARGPTR, &(v), NULL
#define GETHANDLE(v)   GETVOIDP(v)
#define GETHANDLET(v, typesym) GETPTR(v, typesym)
#define GETHWND(v) GETHANDLET(v, HWND)
#define GETVAR(v, fn)  ARGVAR, &(v), fn
#define GETVARWITHDEFAULT(v, fn)  ARGVARWITHDEFAULT, &(v), fn
#define GETGUID(v)     GETVAR(v, ObjToGUID)
#define GETUUID(v)     GETVAR(v, ObjToUUID)
/* For GETAARGV/GETWARGV, v is of type char *v[n], or WCHAR *v[n] */
#define GETAARGV(v, I, n) ARGAARGV, (v), (I), &(n)
#define GETWARGV(v, I, n) ARGWARGV, (v), (I), &(n)

typedef int (*TwapiGetArgsFn)(Tcl_Interp *, Tcl_Obj *, void *);

/*
 * Forward decls
 */
typedef struct _TwapiInterpContext TwapiInterpContext;
ZLINK_CREATE_TYPEDEFS(TwapiInterpContext); 
ZLIST_CREATE_TYPEDEFS(TwapiInterpContext);

typedef struct _TwapiCallback TwapiCallback;
ZLINK_CREATE_TYPEDEFS(TwapiCallback); 
ZLIST_CREATE_TYPEDEFS(TwapiCallback);

typedef struct _TwapiDirectoryMonitorContext TwapiDirectoryMonitorContext;
ZLINK_CREATE_TYPEDEFS(TwapiDirectoryMonitorContext); 
ZLIST_CREATE_TYPEDEFS(TwapiDirectoryMonitorContext);


/*
 * We need to keep track of handles that are being tracked by the 
 * thread pool so they can be released on interp deletion even if
 * the application code does not explicitly release them.
 * NOTE: currently not all modules make use of this but probably should - TBD.
 */
typedef struct _TwapiThreadPoolRegistration TwapiThreadPoolRegistration;
ZLINK_CREATE_TYPEDEFS(TwapiThreadPoolRegistration); 
ZLIST_CREATE_TYPEDEFS(TwapiThreadPoolRegistration); 
typedef struct _TwapiThreadPoolRegistration {
    HANDLE handle;              /* Handle being waited on by thread pool */
    HANDLE tp_handle;           /* Corresponding handle returned by pool */
    TwapiInterpContext *ticP;
    ZLINK_DECL(TwapiThreadPoolRegistration); /* Link for tracking list */

    /* To be called when a HANDLE is signalled */
    void (*signal_handler) (TwapiInterpContext *ticP, TwapiId id, HANDLE h, DWORD);

    /*
     * To be called when handle wait is being unregistered. Routine should
     * take care to handle the case where ticP, ticP->interp and/or h is NULL.
     */
    void (*unregistration_handler)(TwapiInterpContext *ticP, TwapiId id, HANDLE h);

    TwapiId id;                 /* We need an id because OS handles can be
                                   reused and therefore cannot be used
                                   to filter stale events that have been
                                   queued for older handles with same value
                                   that have since been closed.
                                 */

    /* Only accessed from Interp thread so no locking */
    ULONG nrefs;
} TwapiThreadPoolRegistration;



/*
 * For asynchronous notifications of events, there is a common framework
 * that passes events from the asynchronous handlers into the Tcl event
 * dispatch loop. From there, the framework calls a function of type
 * TwapiCallbackFn. On entry to this function,
 *  - a non-NULL pointer to the callback structure (cbP) is passed in
 *  - the cbP->ticP, which contains the interp context is also guaranteed
 *    to be non-NULL.
 *  - the cbP->ticP->interp is the Tcl interp if non-NULL. This may be NULL
 *    if the original associated interp has been logically or physically 
 *    deleted.
 *  - the cbP->clientdata* fields may contain any callback-specific data
 *    set by the enqueueing module.
 *  - the cbP->winerr field is set by the enqueuing module to ERROR_SUCCESS
 *    or a Win32 error code. It is up to the callback and enqueuing module
 *    to figure out what to do with it.
 *  - the cbP pointer may actually point to a "derived" structure where
 *    the callback structure is just the header. The enqueuing module
 *    should use the TwapiCallbackNew function to allocate
 *    and initialize. This function allows the size of allocated storage
 *    to be specified.
 *
 * If the Tcl interp is valid (non-NULL), the callback function is expected
 * to invoke an appropriate script in the interp and store an appropriate
 * result in the cbP->response field. Generally, callbacks build a script
 * and make use of the TwapiEvalAndUpdateCallback utility function to
 * invoke the script and store the result in cbP->response.
 *
 * If the callback function returns TCL_OK, the cbP->status and cbP->response
 * fields are returned to the enqueuing code if it is waiting for a response.
 * Note that the cbP->status may itself contain a Win32 error code to
 * indicate an error. It is entirely up to the enqueuing module to interpret
 * the response.
 *
 * If the callback fails with TCL_ERROR, the framework will set cbP->status
 * to an appropriate Win32 error code and set cbP->response to TRT_EMPTY.
 *
 * In all cases (success or fail), any additional resources attached
 * to cbP, for example buffers, should be freed, or arranged to be freed,
 * by the callback. Obviously, the framework cannot arrange for this.
 */
typedef int TwapiCallbackFn(struct _TwapiCallback *cbP);

/*
 * Definitions relating to queue of pending callbacks. All pending callbacks
 * structure definitions must start with this structure as the header.
 */

/* Creates list link definitions */
typedef struct _TwapiCallback {
    struct _TwapiInterpContext *ticP; /* Interpreter context */
    TwapiCallbackFn  *callback;  /* Function to call back - see notes
                                       in the TwapiCallbackFn typedef */
    LONG volatile     nrefs;       /* Ref count - use InterlockedIncrement */
    ZLINK_DECL(TwapiCallback); /* Link for list */
    HANDLE            completion_event;
    DWORD             winerr;         /* Win32 error code. Used in both
                                         callback request and response */
    /*
     * Associates with a particular notification handle. Not used by all
     * notifications.
     */
    TwapiId           receiver_id;
    DWORD_PTR         clientdata;     /* For use by client code */
    DWORD_PTR         clientdata2;    /* == ditto == */
    union {
        TwapiResult response;
        struct {
            POINTS message_pos;
            DWORD  ticks;
        } wm_state;             /* Used for Window message notifications
                                   (where there is no response) */
    } ;
} TwapiCallback;


/*
 * Thread local storage area
 */
typedef struct _TwapiTls {
    Tcl_ThreadId thread;
#define TWAPI_TLS_SLOTS 8
    DWORD_PTR slots[TWAPI_TLS_SLOTS];
#define TWAPI_TLS_SLOT(slot_) (Twapi_GetTls()->slots[slot_])
} TwapiTls;

/*
 * TwapiInterpContext keeps track of a per-interpreter context.
 * This is allocated when twapi is loaded into an interpreter and
 * passed around as ClientData to most commands. It is reference counted
 * for deletion purposes and also placed on a global list for cleanup
 * purposes when a thread exits.
 */
typedef struct _TwapiInterpContext {
    ZLINK_DECL(TwapiInterpContext); /* Links all the contexts, primarily
                                       to track cleanup requirements */

    LONG volatile         nrefs;   /* Reference count for alloc/free */

    /* Back pointer to the associated interp. This must only be modified or
     * accessed in the Tcl thread. THIS IS IMPORTANT AS THERE IS NO
     * SYNCHRONIZATION PROTECTION AND Tcl interp's ARE NOT MEANT TO BE
     * ACCESSED FROM OTHER THREADS
     */
    Tcl_Interp *interp;

    Tcl_ThreadId thread;     /* Id of interp thread */

    MemLifo memlifo;            /* Must ONLY be used in interp thread */

    /*
     * A single lock that is shared among multiple lists attached to this
     * structure as contention is expected to be low.
     */
    CRITICAL_SECTION lock;

    /* List of pending callbacks. Accessed controlled by the lock field */
    ZLIST_DECL(TwapiCallback) pending;
    int              pending_suspended;       /* If true, do not pend events */
    
    /*
     * List of handles registered with the Windows thread pool. To be accessed
     * only from the interp thread.
     */
    ZLIST_DECL(TwapiThreadPoolRegistration) threadpool_registrations; 

    /*
     * List of directory change monitor contexts. ONLY ACCESSED
     * FROM Tcl THREAD SO ACCESSED WITHOUT A LOCK.
     */
    ZLIST_DECL(TwapiDirectoryMonitorContext) directory_monitors;

    /* Tcl Async callback token. This is created on initialization
     * Note this CANNOT be left to be done when the event actually occurs.
     */
    Tcl_AsyncHandler async_handler;
    HWND          notification_win; /* Window used for various notifications */
    HWND          clipboard_win;    /* Window used for clipboard notifications */
    int           power_events_on; /* True-> send through power notifications */
    int           console_ctrl_hooked; /* True -> This interp is handling
                                          console ctrol signals */
    DWORD         device_notification_tid; /* device notification thread id */
    

} TwapiInterpContext;

/*
 * Structure used for passing events into the Tcl core. In some instances
 * you can directly inherit from Tcl_Event and do not have to go through
 * the expense of an additional allocation. However, Tcl_Event based
 * structures have to be allocated using Tcl_Alloc and we prefer not
 * to do that from outside Tcl threads. In such cases, pending_callback
 * is allocated using TwapiAlloc, passed between threads, and attached
 * to a Tcl_Alloc'ed TwapiTclEvent in a Tcl thread. See async.c
 */
typedef struct _TwapiTclEvent {
    Tcl_Event event;            /* Must be first field */
    TwapiCallback *pending_callback;
} TwapiTclEvent;



/****************************************
 * Definitions related to hidden windows
 * TBD - should this be in twapi_wm.h ?
 ****************************************/
/* Name of hidden window class */
#define TWAPI_HIDDEN_WINDOW_CLASS_L L"TwapiHiddenWindow"

/* Define offsets in window data */
#define TWAPI_HIDDEN_WINDOW_CONTEXT_OFFSET     0
#define TWAPI_HIDDEN_WINDOW_CALLBACK_OFFSET   (TWAPI_HIDDEN_WINDOW_CONTEXT_OFFSET + sizeof(LONG_PTR))
#define TWAPI_HIDDEN_WINDOW_CLIENTDATA_OFFSET (TWAPI_HIDDEN_WINDOW_CALLBACK_OFFSET + sizeof(LONG_PTR))
#define TWAPI_HIDDEN_WINDOW_DATA_SIZE       (TWAPI_HIDDEN_WINDOW_CLIENTDATA_OFFSET + sizeof(LONG_PTR))



/*****************************************************************
 * Prototypes and globals
 *****************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/* GLOBALS */
extern OSVERSIONINFO gTwapiOSVersionInfo;
extern HMODULE gTwapiModuleHandle;     /* DLL handle to ourselves */
extern GUID gTwapiNullGuid;
extern struct TwapiTclVersion gTclVersion;
extern int gTclIsThreaded;
extern TwapiId volatile gIdGenerator;
extern CRITICAL_SECTION gETWCS;
extern ULONG  gETWProviderTraceEnableFlags;
extern ULONG  gETWProviderTraceEnableLevel;
extern TRACEHANDLE gETWProviderSessionHandle;

#define ERROR_IF_UNTHREADED(interp_)        \
    do {                                        \
        if (! gTclIsThreaded) {                                          \
            if (interp_) Tcl_SetResult((interp_), "This command requires a threaded build of Tcl.", TCL_STATIC); \
            return TCL_ERROR;                                           \
        }                                                               \
    } while (0)


/* Thread pool handle registration */
TCL_RESULT TwapiThreadPoolRegister(
    TwapiInterpContext *ticP,
    HANDLE h,
    ULONG timeout,
    DWORD flags,
    void (*signal_handler)(TwapiInterpContext *ticP, TwapiId, HANDLE, DWORD),
    void (*unregistration_handler)(TwapiInterpContext *ticP, TwapiId, HANDLE)
    );
void TwapiThreadPoolUnregister(
    TwapiInterpContext *ticP,
    TwapiId id
    );
void TwapiCallRegisteredWaitScript(TwapiInterpContext *ticP, TwapiId id, HANDLE h, DWORD timeout);
void TwapiThreadPoolRegistrationShutdown(TwapiThreadPoolRegistration *tprP);


TWAPI_EXTERN int Twapi_GenerateWin32Error(Tcl_Interp *interp, DWORD error, char *msg);

LRESULT TwapiEvalWinMessage(TwapiInterpContext *ticP, UINT msg, WPARAM wParam, LPARAM lParam);

int Twapi_TclAsyncProc(TwapiInterpContext *ticP, Tcl_Interp *interp, int code);

/* Tcl_Obj manipulation and conversion - basic Windows types */

void Twapi_FreeNewTclObj(Tcl_Obj *objPtr);
Tcl_Obj *TwapiAppendObjArray(Tcl_Obj *resultObj, int objc, Tcl_Obj **objv,
                         char *join_string);
Tcl_Obj *ObjFromCONSOLE_SCREEN_BUFFER_INFO(
    Tcl_Interp *interp,
    const CONSOLE_SCREEN_BUFFER_INFO *csbiP
    );
Tcl_Obj *ObjFromPOINTS(POINTS *ptP);
int ObjToCOORD(Tcl_Interp *interp, Tcl_Obj *coordObj, COORD *coordP);
Tcl_Obj *ObjFromCOORD(Tcl_Interp *interp, const COORD *coordP);
int ObjToSMALL_RECT(Tcl_Interp *interp, Tcl_Obj *obj, SMALL_RECT *rectP);
int ObjToCHAR_INFO(Tcl_Interp *interp, Tcl_Obj *obj, CHAR_INFO *ciP);
Tcl_Obj *ObjFromSMALL_RECT(Tcl_Interp *interp, const SMALL_RECT *rectP);
int ObjToFLASHWINFO (Tcl_Interp *interp, Tcl_Obj *obj, FLASHWINFO *fwP);
Tcl_Obj *ObjFromWINDOWINFO (WINDOWINFO *wiP);
Tcl_Obj *ObjFromWINDOWPLACEMENT(WINDOWPLACEMENT *wpP);
int ObjToWINDOWPLACEMENT(Tcl_Interp *, Tcl_Obj *objP, WINDOWPLACEMENT *wpP);
Tcl_Obj *ObjFromSecHandle(SecHandle *shP);
int ObjToSecHandle(Tcl_Interp *interp, Tcl_Obj *obj, SecHandle *shP);
int ObjToSecHandle_NULL(Tcl_Interp *interp, Tcl_Obj *obj, SecHandle **shPP);
Tcl_Obj *ObjFromSecPkgInfo(SecPkgInfoW *spiP);
void TwapiFreeSecBufferDesc(SecBufferDesc *sbdP);
int ObjToSecBufferDesc(Tcl_Interp *interp, Tcl_Obj *obj, SecBufferDesc *sbdP, int readonly);
int ObjToSecBufferDescRO(Tcl_Interp *interp, Tcl_Obj *obj, SecBufferDesc *sbdP);
int ObjToSecBufferDescRW(Tcl_Interp *interp, Tcl_Obj *obj, SecBufferDesc *sbdP);
Tcl_Obj *ObjFromSecBufferDesc(SecBufferDesc *sbdP);
int ObjToSP_DEVINFO_DATA(Tcl_Interp *, Tcl_Obj *objP, SP_DEVINFO_DATA *sddP);
int ObjToSP_DEVINFO_DATA_NULL(Tcl_Interp *interp, Tcl_Obj *objP,
                              SP_DEVINFO_DATA **sddPP);
Tcl_Obj *ObjFromSP_DEVINFO_DATA(SP_DEVINFO_DATA *sddP);
int ObjToSP_DEVICE_INTERFACE_DATA(Tcl_Interp *interp, Tcl_Obj *objP,
                                  SP_DEVICE_INTERFACE_DATA *sdidP);
Tcl_Obj *ObjFromSP_DEVICE_INTERFACE_DATA(SP_DEVICE_INTERFACE_DATA *sdidP);
Tcl_Obj *ObjFromDISPLAY_DEVICE(DISPLAY_DEVICEW *ddP);
Tcl_Obj *ObjFromMONITORINFOEX(MONITORINFO *miP);
Tcl_Obj *ObjFromSYSTEM_POWER_STATUS(SYSTEM_POWER_STATUS *spsP);

Tcl_Obj *ObjFromSOCKADDR_address(SOCKADDR *saP);
int ObjToSOCKADDR_STORAGE(Tcl_Interp *interp, Tcl_Obj *objP, SOCKADDR_STORAGE *ssP);
Tcl_Obj *ObjFromMIB_IPNETROW(Tcl_Interp *interp, const MIB_IPNETROW *netrP);
Tcl_Obj *ObjFromMIB_IPNETTABLE(Tcl_Interp *interp, MIB_IPNETTABLE *nettP);
Tcl_Obj *ObjFromMIB_IPFORWARDROW(Tcl_Interp *interp, const MIB_IPFORWARDROW *ipfrP);
Tcl_Obj *ObjFromMIB_IPFORWARDTABLE(Tcl_Interp *interp, MIB_IPFORWARDTABLE *fwdP);
Tcl_Obj *ObjFromIP_ADAPTER_INDEX_MAP(Tcl_Interp *interp, IP_ADAPTER_INDEX_MAP *iaimP);
Tcl_Obj *ObjFromIP_INTERFACE_INFO(Tcl_Interp *interp, IP_INTERFACE_INFO *iiP);
Tcl_Obj *ObjFromMIB_TCPROW(Tcl_Interp *interp, const MIB_TCPROW *row, int size);
int ObjToMIB_TCPROW(Tcl_Interp *interp, Tcl_Obj *listObj, MIB_TCPROW *row);
Tcl_Obj *ObjFromIP_ADDR_STRING (Tcl_Interp *, const IP_ADDR_STRING *ipaddrstrP);
Tcl_Obj *ObjFromMIB_IPADDRROW(Tcl_Interp *interp, const MIB_IPADDRROW *iparP);
Tcl_Obj *ObjFromMIB_IPADDRTABLE(Tcl_Interp *interp, MIB_IPADDRTABLE *ipatP);
Tcl_Obj *ObjFromMIB_IFROW(Tcl_Interp *interp, const MIB_IFROW *ifrP);
Tcl_Obj *ObjFromMIB_IFTABLE(Tcl_Interp *interp, MIB_IFTABLE *iftP);
Tcl_Obj *ObjFromIP_ADAPTER_INDEX_MAP(Tcl_Interp *, IP_ADAPTER_INDEX_MAP *iaimP);
Tcl_Obj *ObjFromMIB_UDPROW(Tcl_Interp *interp, MIB_UDPROW *row, int size);
Tcl_Obj *ObjFromMIB_TCPTABLE(Tcl_Interp *interp, MIB_TCPTABLE *tab);
Tcl_Obj *ObjFromMIB_TCPTABLE_OWNER_PID(Tcl_Interp *i, MIB_TCPTABLE_OWNER_PID *tab);
Tcl_Obj *ObjFromMIB_TCPTABLE_OWNER_MODULE(Tcl_Interp *, MIB_TCPTABLE_OWNER_MODULE *tab);
Tcl_Obj *ObjFromMIB_UDPTABLE(Tcl_Interp *, MIB_UDPTABLE *tab);
Tcl_Obj *ObjFromMIB_UDPTABLE_OWNER_PID(Tcl_Interp *, MIB_UDPTABLE_OWNER_PID *tab);
Tcl_Obj *ObjFromMIB_UDPTABLE_OWNER_MODULE(Tcl_Interp *, MIB_UDPTABLE_OWNER_MODULE *tab);
Tcl_Obj *ObjFromTcpExTable(Tcl_Interp *interp, void *buf);
Tcl_Obj *ObjFromUdpExTable(Tcl_Interp *interp, void *buf);
int ObjToTASK_TRIGGER(Tcl_Interp *interp, Tcl_Obj *obj, TASK_TRIGGER *triggerP);
Tcl_Obj *ObjFromTASK_TRIGGER(TASK_TRIGGER *triggerP);

int ObjToLUID(Tcl_Interp *interp, Tcl_Obj *objP, LUID *luidP);
int ObjToLUID_NULL(Tcl_Interp *interp, Tcl_Obj *objP, LUID **luidPP);
Tcl_Obj *ObjFromLSA_UNICODE_STRING(const LSA_UNICODE_STRING *lsauniP);
void ObjToLSA_UNICODE_STRING(Tcl_Obj *objP, LSA_UNICODE_STRING *lsauniP);
int ObjToLSASTRINGARRAY(Tcl_Interp *interp, Tcl_Obj *obj,
                        LSA_UNICODE_STRING **arrayP, ULONG *countP);
Tcl_Obj *ObjFromACE (Tcl_Interp *interp, void *aceP);
int ObjToACE (Tcl_Interp *interp, Tcl_Obj *aceobj, void **acePP);
Tcl_Obj *ObjFromACL(Tcl_Interp *interp, ACL *aclP);
Tcl_Obj *ObjFromCONNECTION_INFO(Tcl_Interp *interp, LPBYTE infoP, DWORD level);
Tcl_Obj *ObjFromUSE_INFO(Tcl_Interp *interp, LPBYTE infoP, DWORD level);
Tcl_Obj *ObjFromSHARE_INFO(Tcl_Interp *interp, LPBYTE infoP, DWORD level);
Tcl_Obj *ObjFromFILE_INFO(Tcl_Interp *interp, LPBYTE infoP, DWORD level);
Tcl_Obj *ObjFromSESSION_INFO(Tcl_Interp *interp, LPBYTE infoP, DWORD level);
Tcl_Obj *ObjFromUSER_INFO(Tcl_Interp *interp, LPBYTE infoP, DWORD level);
Tcl_Obj *ObjFromGROUP_INFO(Tcl_Interp *interp, LPBYTE infoP, DWORD level);
Tcl_Obj *ObjFromLOCALGROUP_INFO(Tcl_Interp *interp, LPBYTE infoP, DWORD level);
Tcl_Obj *ObjFromGROUP_USERS_INFO(Tcl_Interp *interp, LPBYTE infoP, DWORD level);

/* System related */
int Twapi_LoadUserProfile(Tcl_Interp *interp, HANDLE hToken, DWORD flags,
                          LPWSTR username, LPWSTR profilepath);
BOOLEAN Twapi_Wow64DisableWow64FsRedirection(LPVOID *oldvalueP);
BOOLEAN Twapi_Wow64RevertWow64FsRedirection(LPVOID addr);
BOOLEAN Twapi_Wow64EnableWow64FsRedirection(BOOLEAN enable_redirection);
BOOL Twapi_IsWow64Process(HANDLE h, BOOL *is_wow64P);
int Twapi_GetSystemWow64Directory(Tcl_Interp *interp);
int Twapi_GetSystemInfo(Tcl_Interp *interp);
TCL_RESULT Twapi_GlobalMemoryStatus(Tcl_Interp *interp);
TCL_RESULT Twapi_GetPerformanceInformation(Tcl_Interp *interp);
int Twapi_SystemProcessorTimes(TwapiInterpContext *ticP);
int Twapi_SystemPagefileInformation(TwapiInterpContext *ticP);
int Twapi_TclGetChannelHandle(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
int Twapi_GetPrivateProfileSection(TwapiInterpContext *ticP,
                                   LPCWSTR app, LPCWSTR fn);
int Twapi_GetPrivateProfileSectionNames(TwapiInterpContext *,LPCWSTR filename);
int Twapi_GetVersionEx(Tcl_Interp *interp);
void TwapiGetDllVersion(char *dll, DLLVERSIONINFO *verP);

/* Shell stuff */
HRESULT Twapi_SHGetFolderPath(HWND hwndOwner, int nFolder, HANDLE hToken,
                          DWORD flags, WCHAR *pathbuf);
BOOL Twapi_SHObjectProperties(HWND hwnd, DWORD dwType,
                              LPCWSTR szObject, LPCWSTR szPage);

int Twapi_GetThemeColor(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
int Twapi_GetThemeFont(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
int TwapiGetThemeDefine(Tcl_Interp *interp, char *name);
int Twapi_GetCurrentThemeName(Tcl_Interp *interp);
int Twapi_GetShellVersion(Tcl_Interp *interp);
int Twapi_ShellExecuteEx(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
int Twapi_ReadShortcut(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
int Twapi_WriteShortcut(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
int Twapi_ReadUrlShortcut(Tcl_Interp *interp, LPCWSTR linkPath);
int Twapi_WriteUrlShortcut(Tcl_Interp *interp, LPCWSTR linkPath, LPCWSTR url, DWORD flags);
int Twapi_InvokeUrlShortcut(Tcl_Interp *, int objc, Tcl_Obj *CONST objv[]);
int Twapi_SHFileOperation(Tcl_Interp *, int objc, Tcl_Obj *CONST objv[]);
int Twapi_VerQueryValue_FIXEDFILEINFO(Tcl_Interp *interp, TWAPI_FILEVERINFO * verP);
int Twapi_VerQueryValue_STRING(Tcl_Interp *interp, TWAPI_FILEVERINFO * verP,
                               LPCSTR lang_and_cp, LPSTR name);
int Twapi_VerQueryValue_TRANSLATIONS(Tcl_Interp *interp, TWAPI_FILEVERINFO * verP);
TWAPI_FILEVERINFO * Twapi_GetFileVersionInfo(LPWSTR path);
void Twapi_FreeFileVersionInfo(TWAPI_FILEVERINFO * verP);


/* Processes and threads */
int Twapi_GetProcessList(TwapiInterpContext *, int objc, Tcl_Obj * CONST objv[]);
int Twapi_EnumProcesses (TwapiInterpContext *ticP);
int Twapi_EnumDeviceDrivers(TwapiInterpContext *ticP);
int Twapi_EnumProcessModules(TwapiInterpContext *ticP, HANDLE phandle);
int TwapiCreateProcessHelper(Tcl_Interp *interp, int func, int objc, Tcl_Obj * CONST objv[]);
int Twapi_NtQueryInformationProcessBasicInformation(Tcl_Interp *interp,
                                                    HANDLE processH);
int Twapi_NtQueryInformationThreadBasicInformation(Tcl_Interp *interp,
                                                   HANDLE threadH);
int Twapi_CommandLineToArgv(Tcl_Interp *interp, LPCWSTR cmdlineP);

/* Shares and LANMAN */
int Twapi_WNetGetUniversalName(TwapiInterpContext *ticP, LPCWSTR localpathP);
int Twapi_WNetGetUser(Tcl_Interp *interp, LPCWSTR  lpName);
int Twapi_NetScheduleJobEnum(Tcl_Interp *interp, LPCWSTR servername);
int Twapi_NetShareEnum(Tcl_Interp *interp, LPWSTR server_name);
int Twapi_NetUseGetInfo(Tcl_Interp *interp, LPWSTR UncServer, LPWSTR UseName, DWORD level);
int Twapi_NetShareCheck(Tcl_Interp *interp, LPWSTR server, LPWSTR device);
int Twapi_NetShareGetInfo(Tcl_Interp *interp, LPWSTR server,
                          LPWSTR netname, DWORD level);
int Twapi_NetShareSetInfo(Tcl_Interp *interp, LPWSTR server_name,
                          LPWSTR net_name, LPWSTR remark, DWORD  max_uses,
                          SECURITY_DESCRIPTOR *secd);
int Twapi_NetConnectionEnum(Tcl_Interp    *interp, LPWSTR server,
                            LPWSTR qualifier, DWORD level);
int Twapi_NetFileEnum(Tcl_Interp *interp, LPWSTR server, LPWSTR basepath,
                      LPWSTR user, DWORD level);
int Twapi_NetFileGetInfo(Tcl_Interp    *interp, LPWSTR server,
                         DWORD fileid, DWORD level);
int Twapi_NetSessionEnum(Tcl_Interp    *interp, LPWSTR server, LPWSTR client,
                         LPWSTR user, DWORD level);
int Twapi_NetSessionGetInfo(Tcl_Interp *interp, LPWSTR server,
                            LPWSTR client, LPWSTR user, DWORD level);
int Twapi_NetGetDCName(Tcl_Interp *interp, LPCWSTR server, LPCWSTR domain);
int Twapi_WNetGetResourceInformation(TwapiInterpContext *ticP,
                                     LPWSTR remoteName, LPWSTR provider,
                                     DWORD  resourcetype);
int Twapi_WNetUseConnection(Tcl_Interp *, int objc, Tcl_Obj *CONST objv[]);
int Twapi_NetShareAdd(Tcl_Interp *, int objc, Tcl_Obj *CONST objv[]);


/* Security related */
int Twapi_LookupAccountName (Tcl_Interp *interp, LPCWSTR sysname, LPCWSTR name);
int Twapi_LookupAccountSid (Tcl_Interp *interp, LPCWSTR sysname, PSID sidP);
int Twapi_NetUserEnum(Tcl_Interp *interp, LPWSTR server_name, DWORD filter);
int Twapi_NetGroupEnum(Tcl_Interp *interp, LPWSTR server_name);
int Twapi_NetLocalGroupEnum(Tcl_Interp *interp, LPWSTR server_name);
int Twapi_NetUserGetGroups(Tcl_Interp *interp, LPWSTR server, LPWSTR user);
int Twapi_NetUserGetLocalGroups(Tcl_Interp *interp, LPWSTR server,
                                LPWSTR user, DWORD flags);
int Twapi_NetLocalGroupGetMembers(Tcl_Interp *interp, LPWSTR server, LPWSTR group);
int Twapi_NetGroupGetUsers(Tcl_Interp *interp, LPCWSTR server, LPCWSTR group);
int Twapi_NetUserGetInfo(Tcl_Interp *interp, LPCWSTR server,
                         LPCWSTR user, DWORD level);
int Twapi_NetGroupGetInfo(Tcl_Interp *interp, LPCWSTR server,
                          LPCWSTR group, DWORD level);
int Twapi_NetLocalGroupGetInfo(Tcl_Interp *interp, LPCWSTR server,
                               LPCWSTR group, DWORD level);
int Twapi_LsaEnumerateLogonSessions(Tcl_Interp *interp);
int Twapi_LsaQueryInformationPolicy (Tcl_Interp *, int objc, Tcl_Obj *CONST objv[]
);
int Twapi_InitializeSecurityDescriptor(Tcl_Interp *interp);
int Twapi_GetSecurityInfo(Tcl_Interp *interp, HANDLE h, int type, int wanted_fields);
int Twapi_GetNamedSecurityInfo (Tcl_Interp *, LPWSTR name,int type,int wanted);
int Twapi_LsaGetLogonSessionData(Tcl_Interp *, int objc, Tcl_Obj *CONST objv[]);

int TwapiReturnNetEnum(Tcl_Interp *interp, TwapiNetEnumContext *necP);
int Twapi_NetUseEnum(Tcl_Interp *interp);
int Twapi_NetUserSetInfoDWORD(int fun, LPCWSTR server, LPCWSTR user, DWORD dw);
int Twapi_NetUserSetInfoLPWSTR(int fun, LPCWSTR server, LPCWSTR user, LPWSTR s);
int Twapi_NetUserAdd(Tcl_Interp *interp, LPCWSTR servername, LPWSTR name,
                     LPWSTR password, DWORD priv, LPWSTR home_dir,
                     LPWSTR comment, DWORD flags, LPWSTR script_path);
int Twapi_GetTokenInformation(Tcl_Interp *interp, HANDLE tokenH, int tclass);
int Twapi_SetTokenPrimaryGroup(HANDLE tokenH, PSID sidP);
int Twapi_SetTokenVirtualizationEnabled(HANDLE tokenH, DWORD enabled);
int Twapi_AdjustTokenPrivileges(TwapiInterpContext *ticP, HANDLE tokenH,
                                BOOL disableAll, TOKEN_PRIVILEGES *tokprivP);
DWORD Twapi_PrivilegeCheck(HANDLE tokenH, const TOKEN_PRIVILEGES *tokprivP,
                           int all_required, int *resultP);

int Twapi_LsaEnumerateAccountRights(Tcl_Interp *interp,
                                    LSA_HANDLE PolicyHandle, PSID AccountSid);
int Twapi_LsaEnumerateAccountsWithUserRight(
    Tcl_Interp *, LSA_HANDLE PolicyHandle, LSA_UNICODE_STRING *UserRights);


/* Crypto API */
int Twapi_EnumerateSecurityPackages(Tcl_Interp *interp);
int Twapi_InitializeSecurityContext(
    Tcl_Interp *interp,
    SecHandle *credentialP,
    SecHandle *contextP,
    LPWSTR     targetP,
    ULONG      contextreq,
    ULONG      reserved1,
    ULONG      targetdatarep,
    SecBufferDesc *sbd_inP,
    ULONG     reserved2);
int Twapi_AcceptSecurityContext(Tcl_Interp *interp, SecHandle *credentialP,
                                SecHandle *contextP, SecBufferDesc *sbd_inP,
                                ULONG contextreq, ULONG targetdatarep);
int Twapi_QueryContextAttributes(Tcl_Interp *interp, SecHandle *INPUT,
                                 ULONG attr);
SEC_WINNT_AUTH_IDENTITY_W *Twapi_Allocate_SEC_WINNT_AUTH_IDENTITY (
    LPCWSTR user, LPCWSTR domain, LPCWSTR password);
void Twapi_Free_SEC_WINNT_AUTH_IDENTITY (SEC_WINNT_AUTH_IDENTITY_W *swaiP);
int Twapi_MakeSignature(TwapiInterpContext *ticP, SecHandle *INPUT,
                        ULONG qop, int BINLEN, void *BINDATA, ULONG seqnum);
int Twapi_EncryptMessage(TwapiInterpContext *ticP, SecHandle *INPUT,
                        ULONG qop, int BINLEN, void *BINDATA, ULONG seqnum);
int Twapi_CryptGenRandom(Tcl_Interp *interp, HCRYPTPROV hProv, DWORD dwLen);

/* Device related */
int Twapi_EnumDisplayMonitors(Tcl_Interp *interp, HDC hdc, const RECT *rectP);
int Twapi_QueryDosDevice(TwapiInterpContext *, LPCWSTR lpDeviceName);
int Twapi_SetupDiGetDeviceRegistryProperty(TwapiInterpContext *, int objc, Tcl_Obj *CONST objv[]);
int Twapi_SetupDiGetDeviceInterfaceDetail(TwapiInterpContext *, int objc,
                                          Tcl_Obj *CONST objv[]);
int Twapi_SetupDiClassGuidsFromNameEx(TwapiInterpContext *, int objc,
                                      Tcl_Obj *CONST objv[]);
int Twapi_RegisterDeviceNotification(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
int Twapi_UnregisterDeviceNotification(TwapiInterpContext *ticP, TwapiId id);

/* File and disk related */
int TwapiFirstVolume(Tcl_Interp *interp, LPCWSTR path);
int TwapiNextVolume(Tcl_Interp *interp, int treat_as_mountpoint, HANDLE hFindVolume);
int Twapi_GetVolumeInformation(Tcl_Interp *interp, LPCWSTR path);
int Twapi_GetDiskFreeSpaceEx(Tcl_Interp *interp, LPCWSTR dir);
int Twapi_GetFileType(Tcl_Interp *interp, HANDLE h);
int Twapi_RegisterDirectoryMonitor(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
int Twapi_UnregisterDirectoryMonitor(TwapiInterpContext *ticP, HANDLE dirhandle);

/* PDH */
void TwapiPdhRestoreLocale(void);
int Twapi_PdhParseCounterPath(TwapiInterpContext *, LPCWSTR buf, DWORD dwFlags);
int Twapi_PdhGetFormattedCounterValue(Tcl_Interp *, HANDLE hCtr, DWORD fmt);
int Twapi_PdhLookupPerfNameByIndex(Tcl_Interp *,  LPCWSTR machine, DWORD ctr);
int Twapi_PdhMakeCounterPath (TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
int Twapi_PdhBrowseCounters(Tcl_Interp *interp);
int Twapi_PdhEnumObjects(TwapiInterpContext *ticP,
                         LPCWSTR source, LPCWSTR machine,
                         DWORD  dwDetailLevel, BOOL bRefresh);
int Twapi_PdhEnumObjectItems(TwapiInterpContext *,
                             LPCWSTR source, LPCWSTR machine,
                              LPCWSTR objname, DWORD detail, DWORD dwFlags);


/* Printers */
int Twapi_EnumPrinters_Level4(Tcl_Interp *interp, DWORD flags);

/* Console related */
int Twapi_ReadConsole(TwapiInterpContext *, HANDLE conh, unsigned int numchars);

/* Clipboard related */
int Twapi_EnumClipboardFormats(Tcl_Interp *interp);
int Twapi_ClipboardMonitorStart(TwapiInterpContext *ticP);
int Twapi_ClipboardMonitorStop(TwapiInterpContext *ticP);
int Twapi_StartConsoleEventNotifier(TwapiInterpContext *ticP);
int Twapi_StopConsoleEventNotifier(TwapiInterpContext *ticP);


/* ADSI related */
int Twapi_DsGetDcName(Tcl_Interp *interp, LPCWSTR systemnameP,
                      LPCWSTR domainnameP, UUID *guidP,
                      LPCWSTR sitenameP, ULONG flags);

/* Network related */
Tcl_Obj *IPAddrObjFromDWORD(DWORD addr);
int IPAddrObjToDWORD(Tcl_Interp *interp, Tcl_Obj *objP, DWORD *addrP);
Tcl_Obj *ObjFromIPv6Addr(const char *addrP, DWORD scope_id);
int Twapi_GetNetworkParams(TwapiInterpContext *ticP);
    int Twapi_GetAdaptersAddresses(TwapiInterpContext *ticP, ULONG, ULONG, void *);
int Twapi_GetAdaptersInfo(TwapiInterpContext *ticP);
int Twapi_GetInterfaceInfo(TwapiInterpContext *ticP);
int Twapi_GetPerAdapterInfo(TwapiInterpContext *ticP, int adapter_index);
int Twapi_GetIfEntry(Tcl_Interp *interp, int if_index);
int Twapi_GetIfTable(TwapiInterpContext *ticP, int sort);
int Twapi_GetIpAddrTable(TwapiInterpContext *ticP, int sort);
int Twapi_GetIpNetTable(TwapiInterpContext *ticP, int sort);
int Twapi_GetIpForwardTable(TwapiInterpContext *ticP, int sort);

int Twapi_GetBestRoute(TwapiInterpContext *, int objc, Tcl_Obj *CONST objv[]);
int Twapi_GetBestInterface(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
int Twapi_AllocateAndGetTcpExTableFromStack(TwapiInterpContext *,BOOL sort,DWORD flags);
int Twapi_AllocateAndGetUdpExTableFromStack(TwapiInterpContext *,BOOL sort,DWORD flags);
int Twapi_FormatExtendedTcpTable(Tcl_Interp *, void *buf, int family, int table_class);
int Twapi_FormatExtendedUdpTable(Tcl_Interp *, void *buf, int family, int table_class);
int Twapi_GetExtendedTcpTable(Tcl_Interp *interp, void *buf, DWORD buf_sz,
                              BOOL sorted, ULONG family, int table_class);
int Twapi_GetExtendedUdpTable(Tcl_Interp *interp, void *buf, DWORD buf_sz,
                              BOOL sorted, ULONG family, int table_class);
Tcl_Obj *ObjFromIP_ADAPTER_INFO(Tcl_Interp *interp, IP_ADAPTER_INFO *ainfoP);
Tcl_Obj *ObjFromIP_ADAPTER_INFO_table(Tcl_Interp *, IP_ADAPTER_INFO *ainfoP);
int ObjToSOCKADDR_IN(Tcl_Interp *, Tcl_Obj *objP, struct sockaddr_in *sinP);
Tcl_Obj *TwapiCollectAddrInfo(struct addrinfo *addrP, int family);
int TwapiStringToSOCKADDR_STORAGE(char *s, SOCKADDR_STORAGE *ssP, int family);
int Twapi_GetNameInfo(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
int Twapi_GetAddrInfo(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
int Twapi_ResolveAddressAsync(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
int Twapi_ResolveHostnameAsync(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);

/* NLS */

int Twapi_GetNumberFormat(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
int Twapi_GetCurrencyFormat(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);



/* COM stuff */

int TwapiMakeVariantParam(
    Tcl_Interp *interp,
    Tcl_Obj *paramDescriptorP,
    VARIANT *varP,
    VARIANT *refvarP,
    USHORT  *paramflagsP,
    Tcl_Obj *valueObj
    );
void TwapiClearVariantParam(Tcl_Interp *interp, VARIANT *varP);

/* Note - because ifcp_ is typed, this has to be a macro */
#define TWAPI_STORE_COM_ERROR(interp_, hr_, ifcp_, iidp_)  \
    do { \
        ISupportErrorInfo *sei = NULL; \
        (ifcp_)->lpVtbl->QueryInterface((ifcp_), &IID_ISupportErrorInfo, (LPVOID*)&sei); \
        /* Twapi_AppendCOMError will accept NULL sei so no check for error */ \
        Twapi_AppendCOMError((interp_), (hr_), sei, (iidp_));           \
        if (sei) sei->lpVtbl->Release(sei);                              \
    } while (0)

#define TWAPI_GET_ISupportErrorInfo(sei_,ifcp_)    \
    do { \
        if (FAILED((ifcp_)->lpVtbl->QueryInterface((ifcp_), &IID_ISupportErrorInfo, (LPVOID*)&sei_))) { \
                sei_ = NULL; \
            } \
    } while (0)


int Twapi_AppendCOMError(Tcl_Interp *interp, HRESULT hr, ISupportErrorInfo *sei, REFIID iid);


/* WTS */

int Twapi_WTSEnumerateSessions(Tcl_Interp *interp, HANDLE wtsH);
int Twapi_WTSEnumerateProcesses(Tcl_Interp *interp, HANDLE wtsH);
int Twapi_WTSQuerySessionInformation(Tcl_Interp *interp,  HANDLE wtsH,
                                     DWORD  sess_id, WTS_INFO_CLASS info_class);

/* Services */
int Twapi_CreateService(Tcl_Interp *interp, int objc,
                        Tcl_Obj *CONST objv[]);
int Twapi_StartService(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
int Twapi_ChangeServiceConfig(Tcl_Interp *interp, int objc,
                              Tcl_Obj *CONST objv[]);
int Twapi_EnumServicesStatusEx(TwapiInterpContext *, SC_HANDLE hService,
                               int infolevel, DWORD dwServiceType,
                               DWORD dwServiceState,  LPCWSTR groupname);
int Twapi_EnumDependentServices(TwapiInterpContext *interp, SC_HANDLE hService, DWORD state);
int Twapi_QueryServiceStatusEx(Tcl_Interp *interp, SC_HANDLE h, SC_STATUS_TYPE level);
int Twapi_QueryServiceConfig(TwapiInterpContext *ticP, SC_HANDLE hService);
int Twapi_BecomeAService(TwapiInterpContext *, int objc, Tcl_Obj *CONST objv[]);

int Twapi_SetServiceStatus(TwapiInterpContext *, int objc, Tcl_Obj *CONST objv[]);


/* Task scheduler related */
int Twapi_IEnumWorkItems_Next(Tcl_Interp *interp,
        IEnumWorkItems *ewiP, unsigned long count);
int Twapi_IScheduledWorkItem_GetRunTimes(Tcl_Interp *interp,
        IScheduledWorkItem *swiP, SYSTEMTIME *, SYSTEMTIME *, WORD );
int Twapi_IScheduledWorkItem_GetWorkItemData(Tcl_Interp *interp,
                                             IScheduledWorkItem *swiP);

/* Event log */
BOOL Twapi_IsEventLogFull(HANDLE hEventLog, int *fullP);
int Twapi_ReportEvent(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
int Twapi_ReadEventLog(TwapiInterpContext *, HANDLE evlH, DWORD  flags, DWORD offset);

/* ETW */
TCL_RESULT Twapi_RegisterTraceGuids(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
TCL_RESULT Twapi_UnregisterTraceGuids(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
TCL_RESULT Twapi_TraceEvent(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
TCL_RESULT Twapi_OpenTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
TCL_RESULT Twapi_CloseTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
TCL_RESULT Twapi_EnableTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
TCL_RESULT Twapi_ControlTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
TCL_RESULT Twapi_StartTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
TCL_RESULT Twapi_ProcessTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
TCL_RESULT Twapi_ParseEventMofData(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);

/* UI and window related */
int Twapi_SendUnicode(TwapiInterpContext *ticP, Tcl_Obj *input_obj);
int Twapi_SendInput(TwapiInterpContext *ticP, Tcl_Obj *input_obj);
Tcl_Obj *ObjFromLOGFONTW(LOGFONTW *lfP);
int Twapi_EnumWindowStations(Tcl_Interp *interp);
int Twapi_EnumWindows(Tcl_Interp *interp);
int Twapi_BlockInput(Tcl_Interp *interp, BOOL block);
int Twapi_WaitForInputIdle(Tcl_Interp *, HANDLE hProcess, DWORD dwMillisecs);
int Twapi_GetGUIThreadInfo(Tcl_Interp *interp, DWORD idThread);
int Twapi_EnumDesktops(Tcl_Interp *interp, HWINSTA hwinsta);
int Twapi_EnumDesktopWindows(Tcl_Interp *interp, HDESK desk_handle);
int Twapi_EnumChildWindows(Tcl_Interp *interp, HWND parent_handle);
DWORD Twapi_SetWindowLongPtr(HWND hWnd, int nIndex, LONG_PTR lValue, LONG_PTR *retP);
int Twapi_UnregisterHotKey(TwapiInterpContext *ticP, int id);
int Twapi_RegisterHotKey(TwapiInterpContext *ticP, int id, UINT modifiers, UINT vk);
LRESULT TwapiHotkeyHandler(TwapiInterpContext *, UINT, WPARAM, LPARAM);
HWND Twapi_GetNotificationWindow(TwapiInterpContext *ticP);

/* Power management */
LRESULT TwapiPowerHandler(TwapiInterpContext *, UINT, WPARAM, LPARAM);
int Twapi_PowerNotifyStart(TwapiInterpContext *ticP);
int Twapi_PowerNotifyStop(TwapiInterpContext *ticP);

/* Named pipes */
TCL_RESULT Twapi_NPipeServer(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);
TCL_RESULT Twapi_NPipeClient(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[]);

/* WMI */
TCL_RESULT Twapi_IMofCompiler_CompileFileOrBuffer(TwapiInterpContext *ticP, int type, int objc, Tcl_Obj *CONST objv[]);


/* Resource manipulation */
int Twapi_LoadImage(Tcl_Interp *, int objc, Tcl_Obj *CONST objv[]);
int Twapi_UpdateResource(Tcl_Interp *, int objc, Tcl_Obj *CONST objv[]);
int Twapi_FindResourceEx(Tcl_Interp *, int objc, Tcl_Obj *CONST objv[]);
int Twapi_LoadResource(Tcl_Interp *, int objc, Tcl_Obj *CONST objv[]);
int Twapi_EnumResourceNames(Tcl_Interp *,int objc,Tcl_Obj *CONST objv[]);
int Twapi_EnumResourceLanguages(Tcl_Interp *,int objc,Tcl_Obj *CONST objv[]);
int Twapi_EnumResourceTypes(Tcl_Interp *, HMODULE hmodule);
TCL_RESULT Twapi_SplitStringResource(Tcl_Interp *interp, int objc,
                                     Tcl_Obj *CONST objv[]);
Tcl_Obj *ObjFromResourceIntOrString(LPCWSTR s);
TCL_RESULT ObjToResourceIntOrString(Tcl_Interp *interp, Tcl_Obj *objP, LPCWSTR *wsP);

/* Typedef for callbacks invoked from the hidden window proc. Parameters are
 * those for a window procedure except for an additional interp pointer (which
 * may be NULL)
 */
typedef LRESULT TwapiHiddenWindowCallbackProc(TwapiInterpContext *, LONG_PTR, HWND, UINT, WPARAM, LPARAM);
int Twapi_CreateHiddenWindow(TwapiInterpContext *,
                             TwapiHiddenWindowCallbackProc *winProc,
                             LONG_PTR clientdata, HWND *winP);


/* Built-in commands */

typedef int TwapiTclObjCmd(
    ClientData dummy,           /* Not used. */
    Tcl_Interp *interp,         /* Current interpreter. */
    int objc,                   /* Number of arguments. */
    Tcl_Obj *CONST objv[]);     /* Argument objects. */

TwapiTclObjCmd Twapi_ParseargsObjCmd;
TwapiTclObjCmd Twapi_TryObjCmd;
TwapiTclObjCmd Twapi_KlGetObjCmd;
TwapiTclObjCmd Twapi_TwineObjCmd;
TwapiTclObjCmd Twapi_RecordArrayObjCmd;
TwapiTclObjCmd Twapi_GetTwapiBuildInfo;
TwapiTclObjCmd Twapi_IDispatch_InvokeObjCmd;
TwapiTclObjCmd Twapi_ComEventSinkObjCmd;
TwapiTclObjCmd Twapi_SHChangeNotify;
TwapiTclObjCmd Twapi_InternalCastObjCmd;
TwapiTclObjCmd Twapi_GetTclTypeObjCmd;

/* Dispatcher routines */
int Twapi_InitCalls(Tcl_Interp *interp, TwapiInterpContext *ticP);
TwapiTclObjCmd Twapi_CallObjCmd;
TwapiTclObjCmd Twapi_CallUObjCmd;
TwapiTclObjCmd Twapi_CallSObjCmd;
TwapiTclObjCmd Twapi_CallHObjCmd;
TwapiTclObjCmd Twapi_CallHSUObjCmd;
TwapiTclObjCmd Twapi_CallSSSDObjCmd;
TwapiTclObjCmd Twapi_CallWObjCmd;
TwapiTclObjCmd Twapi_CallWUObjCmd;
TwapiTclObjCmd Twapi_CallPSIDObjCmd;
TwapiTclObjCmd Twapi_CallNetEnumObjCmd;
TwapiTclObjCmd Twapi_CallPdhObjCmd;
TwapiTclObjCmd Twapi_CallCOMObjCmd;


/* General utility functions */
int WINAPI TwapiGlobCmp (const char *s, const char *pat);
int WINAPI TwapiGlobCmpCase (const char *s, const char *pat);

int Twapi_MemLifoDump(TwapiInterpContext *ticP, MemLifo *l);

#ifdef __cplusplus
} // extern "C"
#endif

/*
 * Exported functions
 */

/* Memory allocation */

TWAPI_EXTERN void *TwapiAlloc(size_t sz);
TWAPI_EXTERN void *TwapiAllocSize(size_t sz, size_t *);
TWAPI_EXTERN void *TwapiAllocZero(size_t sz);
TWAPI_EXTERN void TwapiFree(void *p);
TWAPI_EXTERN WCHAR *TwapiAllocWString(WCHAR *, int len);
TWAPI_EXTERN WCHAR *TwapiAllocWStringFromObj(Tcl_Obj *, int *lenP);
TWAPI_EXTERN char *TwapiAllocAString(char *, int len);
TWAPI_EXTERN char *TwapiAllocAStringFromObj(Tcl_Obj *, int *lenP);
TWAPI_EXTERN void *TwapiReallocTry(void *p, size_t sz);


/* C - Tcl result and parameter conversion  */
TWAPI_EXTERN TCL_RESULT TwapiSetResult(Tcl_Interp *interp, TwapiResult *result);
TWAPI_EXTERN void TwapiClearResult(TwapiResult *resultP);
TWAPI_EXTERN TCL_RESULT TwapiGetArgs(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[], char fmt, ...);

/* errors.c */
TWAPI_EXTERN TCL_RESULT TwapiReturnSystemError(Tcl_Interp *interp);
TWAPI_EXTERN TCL_RESULT TwapiReturnError(Tcl_Interp *interp, int code);
TWAPI_EXTERN TCL_RESULT TwapiReturnErrorEx(Tcl_Interp *interp, int code, Tcl_Obj *objP);
TWAPI_EXTERN TCL_RESULT TwapiReturnErrorMsg(Tcl_Interp *interp, int code, char *msg);

TWAPI_EXTERN DWORD TwapiNTSTATUSToError(NTSTATUS status);
TWAPI_EXTERN Tcl_Obj *Twapi_MakeTwapiErrorCodeObj(int err);
TWAPI_EXTERN Tcl_Obj *Twapi_MapWindowsErrorToString(DWORD err);
TWAPI_EXTERN Tcl_Obj *Twapi_MakeWindowsErrorCodeObj(DWORD err, Tcl_Obj *);
TWAPI_EXTERN TCL_RESULT Twapi_AppendWNetError(Tcl_Interp *interp, unsigned long err);
TWAPI_EXTERN TCL_RESULT Twapi_AppendSystemErrorEx(Tcl_Interp *, unsigned long err, Tcl_Obj *extra);
#define Twapi_AppendSystemError2 Twapi_AppendSystemErrorEx
TWAPI_EXTERN TCL_RESULT Twapi_AppendSystemError(Tcl_Interp *, unsigned long err);
TWAPI_EXTERN void TwapiWriteEventLogError(const char *msg);

/* Async handling related */
TWAPI_EXTERN void TwapiEnqueueTclEvent(TwapiInterpContext *ticP, Tcl_Event *evP);
#define TwapiCallbackRef(pcb_, incr_) InterlockedExchangeAdd(&(pcb_)->nrefs, (incr_))
TWAPI_EXTERN void TwapiCallbackUnref(TwapiCallback *pcbP, int);
TWAPI_EXTERN void TwapiCallbackDelete(TwapiCallback *pcbP);
TWAPI_EXTERN TwapiCallback *TwapiCallbackNew(
    TwapiInterpContext *ticP, TwapiCallbackFn *callback, size_t sz);
TWAPI_EXTERN int TwapiEnqueueCallback(
    TwapiInterpContext *ticP, TwapiCallback *pcbP,
    int enqueue_method,
    int timeout,
    TwapiCallback **responseP
    );
#define TWAPI_ENQUEUE_DIRECT 0
#define TWAPI_ENQUEUE_ASYNC  1
TWAPI_EXTERN int TwapiEvalAndUpdateCallback(TwapiCallback *cbP, int objc, Tcl_Obj *objv[], TwapiResultType response_type);

/* Tcl_Obj manipulation and conversion - basic Windows types */
int TwapiInitTclTypes(void);
int TwapiGetTclType(Tcl_Obj *objP);

TWAPI_EXTERN Tcl_Obj *ObjFromOpaque(void *pv, char *name);
#define ObjFromHANDLE(h) ObjFromOpaque((h), "HANDLE")
#define ObjFromHWND(h) ObjFromOpaque((h), "HWND")
#define ObjFromLPVOID(p) ObjFromOpaque((p), "void*")

TWAPI_EXTERN int ObjToOpaque(Tcl_Interp *interp, Tcl_Obj *obj, void **pvP, char *name);
TWAPI_EXTERN int ObjToOpaqueMulti(Tcl_Interp *interp, Tcl_Obj *obj, void **pvP, int ntypes, char **types);
#define ObjToLPVOID(interp, obj, vPP) ObjToOpaque((interp), (obj), (vPP), NULL)
#define ObjToHANDLE ObjToLPVOID
#define ObjToHWND(ip_, obj_, p_) ObjToOpaque((ip_), (obj_), (p_), "HWND")

/* Unsigned ints/longs need to be promoted to wide ints */
#define ObjFromDWORD(dw_) Tcl_NewWideIntObj((DWORD)(dw_))
#define ObjFromULONG      ObjFromDWORD

#ifdef _WIN64
#define ObjToDWORD_PTR        Tcl_GetWideIntFromObj
#define ObjFromDWORD_PTR(p_)  ObjFromULONGLONG(((ULONGLONG)(p_))
#else  // ! _WIN64
#define ObjToDWORD_PTR        Tcl_GetLongFromObj
#define ObjFromDWORD_PTR(p_)  ObjFromDWORD((DWORD_PTR)(p_))
#endif // _WIN64
#define ObjToULONG_PTR    ObjToDWORD_PTR
#define ObjFromULONG_PTR  ObjFromDWORD_PTR
#define ObjFromSIZE_T     ObjFromDWORD_PTR

#define ObjFromLARGE_INTEGER(val_) Tcl_NewWideIntObj((val_).QuadPart)
TWAPI_EXTERN Tcl_Obj *ObjFromULONGLONG(ULONGLONG ull);
TWAPI_EXTERN Tcl_Obj *ObjFromULONGHex(ULONG ull);
TWAPI_EXTERN Tcl_Obj *ObjFromULONGLONGHex(ULONGLONG ull);

TWAPI_EXTERN Tcl_Obj *TwapiUtf8ObjFromUnicode(CONST WCHAR *p, int len);
#if USE_UNICODE_OBJ
#define ObjFromUnicodeN(p_, n_)    Tcl_NewUnicodeObj((p_), (n_))
#else
#define ObjFromUnicodeN(p_, n_) TwapiUtf8ObjFromUnicode((p_), (n_))
#endif

#define ObjFromUnicode(p_)    ObjFromUnicodeN(p_, -1)
TWAPI_EXTERN Tcl_Obj *ObjFromStringLimited(const char *strP, int max, int *remain);
TWAPI_EXTERN Tcl_Obj *ObjFromUnicodeLimited(const WCHAR *wstrP, int max, int *remain);

TWAPI_EXTERN int ObjToWord(Tcl_Interp *interp, Tcl_Obj *obj, WORD *wordP);



TWAPI_EXTERN int ObjToArgvW(Tcl_Interp *interp, Tcl_Obj *objP, LPCWSTR *argv, int argc, int *argcP);
TWAPI_EXTERN int ObjToArgvA(Tcl_Interp *interp, Tcl_Obj *objP, char **argv, int argc, int *argcP);
TWAPI_EXTERN LPWSTR ObjToLPWSTR_NULL_IF_EMPTY(Tcl_Obj *objP);

#define NULL_TOKEN "__null__"
#define NULL_TOKEN_L L"__null__"
TWAPI_EXTERN LPWSTR ObjToLPWSTR_WITH_NULL(Tcl_Obj *objP);

TWAPI_EXTERN Tcl_Obj *ObjFromMODULEINFO(LPMODULEINFO miP);
TWAPI_EXTERN Tcl_Obj *ObjFromPIDL(LPCITEMIDLIST pidl);
TWAPI_EXTERN int ObjToPIDL(Tcl_Interp *interp, Tcl_Obj *objP, LPITEMIDLIST *idsPP);
TWAPI_EXTERN void TwapiFreePIDL(LPITEMIDLIST idlistP);

#define ObjFromIDispatch(p_) ObjFromOpaque((p_), "IDispatch")
TWAPI_EXTERN int ObjToIDispatch(Tcl_Interp *interp, Tcl_Obj *obj, void **pvP);
#define ObjFromIUnknown(p_) ObjFromOpaque((p_), "IUnknown")
#define ObjToIUnknown(ip_, obj_, ifc_) \
    ObjToOpaque((ip_), (obj_), (ifc_), "IUnknown")

TWAPI_EXTERN int ObjToVT(Tcl_Interp *interp, Tcl_Obj *obj, VARTYPE *vtP);
TWAPI_EXTERN Tcl_Obj *ObjFromBSTR (BSTR bstr);
TWAPI_EXTERN int ObjToBSTR (Tcl_Interp *, Tcl_Obj *, BSTR *);
TWAPI_EXTERN int ObjToRangedInt(Tcl_Interp *, Tcl_Obj *obj, int low, int high, int *iP);
TWAPI_EXTERN Tcl_Obj *ObjFromSYSTEMTIME(LPSYSTEMTIME timeP);
TWAPI_EXTERN int ObjToSYSTEMTIME(Tcl_Interp *interp, Tcl_Obj *timeObj, LPSYSTEMTIME timeP);
TWAPI_EXTERN Tcl_Obj *ObjFromFILETIME(FILETIME *ftimeP);
TWAPI_EXTERN int ObjToFILETIME(Tcl_Interp *interp, Tcl_Obj *obj, FILETIME *cyP);
TWAPI_EXTERN Tcl_Obj *ObjFromTIME_ZONE_INFORMATION(const TIME_ZONE_INFORMATION *tzP);
TWAPI_EXTERN TCL_RESULT ObjToTIME_ZONE_INFORMATION(Tcl_Interp *interp, Tcl_Obj *tzObj, TIME_ZONE_INFORMATION *tzP);
TWAPI_EXTERN Tcl_Obj *ObjFromCY(const CY *cyP);
TWAPI_EXTERN int ObjToCY(Tcl_Interp *interp, Tcl_Obj *obj, CY *cyP);
TWAPI_EXTERN Tcl_Obj *ObjFromDECIMAL(DECIMAL *cyP);
TWAPI_EXTERN int ObjToDECIMAL(Tcl_Interp *interp, Tcl_Obj *obj, DECIMAL *cyP);
TWAPI_EXTERN Tcl_Obj *ObjFromVARIANT(VARIANT *varP, int value_only);

/* Note: the returned multiszPP must be free()'ed */
TWAPI_EXTERN int ObjToMultiSz (Tcl_Interp *interp, Tcl_Obj *listPtr, LPCWSTR *multiszPP);
#define Twapi_ConvertTclListToMultiSz ObjToMultiSz
TWAPI_EXTERN Tcl_Obj *ObjFromMultiSz (LPCWSTR lpcw, int maxlen);
#define ObjFromMultiSz_MAX(lpcw) ObjFromMultiSz(lpcw, INT_MAX)
TWAPI_EXTERN Tcl_Obj *ObjFromRegValue(Tcl_Interp *interp, int regtype,
                         BYTE *bufP, int count);
TWAPI_EXTERN int ObjToRECT (Tcl_Interp *interp, Tcl_Obj *obj, RECT *rectP);
TWAPI_EXTERN int ObjToRECT_NULL (Tcl_Interp *interp, Tcl_Obj *obj, RECT **rectPP);
TWAPI_EXTERN Tcl_Obj *ObjFromRECT(RECT *rectP);
TWAPI_EXTERN Tcl_Obj *ObjFromPOINT(POINT *ptP);
TWAPI_EXTERN int ObjToPOINT (Tcl_Interp *interp, Tcl_Obj *obj, POINT *ptP);

/* GUIDs and UUIDs */
TWAPI_EXTERN Tcl_Obj *ObjFromGUID(GUID *guidP);
TWAPI_EXTERN int ObjToGUID(Tcl_Interp *interp, Tcl_Obj *objP, GUID *guidP);
TWAPI_EXTERN int ObjToGUID_NULL(Tcl_Interp *interp, Tcl_Obj *objP, GUID **guidPP);
TWAPI_EXTERN Tcl_Obj *ObjFromUUID (UUID *uuidP);
TWAPI_EXTERN int ObjToUUID(Tcl_Interp *interp, Tcl_Obj *objP, UUID *uuidP);
TWAPI_EXTERN int ObjToUUID_NULL(Tcl_Interp *interp, Tcl_Obj *objP, UUID **uuidPP);
TWAPI_EXTERN Tcl_Obj *ObjFromLUID (const LUID *luidP);

/* Security stuff */
#define TWAPI_SID_LENGTH(sid_) (8 + (4 * ((SID *)sid_)->SubAuthorityCount))
TWAPI_EXTERN int ObjToPSID(Tcl_Interp *interp, Tcl_Obj *obj, PSID *sidPP);
TWAPI_EXTERN int ObjFromSID (Tcl_Interp *interp, SID *sidP, Tcl_Obj **objPP);
TWAPI_EXTERN Tcl_Obj *ObjFromSIDNoFail(SID *sidP);
TWAPI_EXTERN int ObjToSID_AND_ATTRIBUTES(Tcl_Interp *interp, Tcl_Obj *obj, SID_AND_ATTRIBUTES *sidattrP);
TWAPI_EXTERN Tcl_Obj *ObjFromSID_AND_ATTRIBUTES (Tcl_Interp *, const SID_AND_ATTRIBUTES *);
TWAPI_EXTERN int ObjToPACL(Tcl_Interp *interp, Tcl_Obj *aclObj, ACL **aclPP);
TWAPI_EXTERN int ObjToPSECURITY_ATTRIBUTES(Tcl_Interp *interp, Tcl_Obj *secattrObj,
                                 SECURITY_ATTRIBUTES **secattrPP);
TWAPI_EXTERN void TwapiFreeSECURITY_ATTRIBUTES(SECURITY_ATTRIBUTES *secattrP);
TWAPI_EXTERN void TwapiFreeSECURITY_DESCRIPTOR(SECURITY_DESCRIPTOR *secdP);
TWAPI_EXTERN int ObjToPSECURITY_DESCRIPTOR(Tcl_Interp *, Tcl_Obj *, SECURITY_DESCRIPTOR **secdPP);
TWAPI_EXTERN Tcl_Obj *ObjFromSECURITY_DESCRIPTOR(Tcl_Interp *, SECURITY_DESCRIPTOR *);
TWAPI_EXTERN Tcl_Obj *ObjFromLUID_AND_ATTRIBUTES (Tcl_Interp *, const LUID_AND_ATTRIBUTES *);
TWAPI_EXTERN int ObjToLUID_AND_ATTRIBUTES (Tcl_Interp *interp, Tcl_Obj *listobj,
                              LUID_AND_ATTRIBUTES *luidattrP);
TWAPI_EXTERN int ObjToPTOKEN_PRIVILEGES(Tcl_Interp *interp,
                          Tcl_Obj *tokprivObj, TOKEN_PRIVILEGES **tokprivPP);
TWAPI_EXTERN Tcl_Obj *ObjFromTOKEN_PRIVILEGES(Tcl_Interp *interp,
                                 const TOKEN_PRIVILEGES *tokprivP);
TWAPI_EXTERN void TwapiFreeTOKEN_PRIVILEGES (TOKEN_PRIVILEGES *tokPrivP);

typedef NTSTATUS (WINAPI *NtQuerySystemInformation_t)(int, PVOID, ULONG, PULONG);
TWAPI_EXTERN NtQuerySystemInformation_t Twapi_GetProc_NtQuerySystemInformation();
TWAPI_EXTERN int TwapiFormatMessageHelper( Tcl_Interp *interp, DWORD dwFlags,
                              LPCVOID lpSource, DWORD dwMessageId,
                              DWORD dwLanguageId, int argc, LPCWSTR *argv );

/* LZMA */
TWAPI_EXTERN unsigned char *TwapiLzmaUncompressBuffer(TwapiInterpContext *ticP,
                                         unsigned char *buf,
                                         DWORD sz, DWORD *outsz);
TWAPI_EXTERN void TwapiLzmaFreeBuffer(unsigned char *buf);

/* General utility */
TWAPI_EXTERN TCL_RESULT Twapi_SourceResource(TwapiInterpContext *ticP, HANDLE dllH, const char *name);
TWAPI_EXTERN Tcl_Obj *TwapiTwine(Tcl_Interp *interp, Tcl_Obj *first, Tcl_Obj *second);

TWAPI_EXTERN void TwapiDebugOutput(char *s);
TWAPI_EXTERN TCL_RESULT TwapiReadMemory (Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
TWAPI_EXTERN TCL_RESULT TwapiWriteMemory (Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
typedef int TwapiOneTimeInitFn(void *);
TWAPI_EXTERN int TwapiDoOneTimeInit(TwapiOneTimeInitState *stateP, TwapiOneTimeInitFn *, ClientData);
TWAPI_EXTERN int Twapi_AppendLog(Tcl_Interp *interp, WCHAR *msg);
TWAPI_EXTERN TwapiId Twapi_NewId();

/* Interp context */
TWAPI_EXTERN TwapiInterpContext * TwapiInterpContextNew(Tcl_Interp *interp);
#define TwapiInterpContextRef(ticP_, incr_) InterlockedExchangeAdd(&(ticP_)->nrefs, (incr_))
TWAPI_EXTERN void TwapiInterpContextUnref(TwapiInterpContext *ticP, int);
TWAPI_EXTERN TwapiTls *Twapi_GetTls();
TWAPI_EXTERN int Twapi_AssignTlsSlot();


#endif // TWAPI_H