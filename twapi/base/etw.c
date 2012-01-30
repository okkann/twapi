/* 
 * Copyright (c) 2012, Ashok P. Nadkarni
 * All rights reserved.
 *
 * See the file LICENSE for license
 */

#include "twapi.h"

/* Max length of file and session name fields in a trace (documented in SDK) */
#define MAX_TRACE_NAME_CHARS (1024+1)

#define ObjFromTRACEHANDLE(val_) Tcl_NewWideIntObj(val_)
#define ObjToTRACEHANDLE(ip_, objP_, valP_) Tcl_GetWideIntFromObj((ip_), (objP_), (valP_))

/* IMPORTANT : 
 * Do not change order without changing ObjToPEVENT_TRACE_PROPERTIES()
 * and ObjFromEVENT_TRACE_PROPERTIES
 */
static const char * g_event_trace_fields[] = {
    "-logfile",
    "-sessionname",
    "-sessionguid",
    "-buffersize",
    "-minbuffers",
    "-maxbuffers",
    "-maximumfilesize",
    "-logfilemode",
    "-flushtimer",
    "-enableflags",
    "-clockresolution",
    "-agelimit",
    "-numbuffers",
    "-freebuffers",
    "-eventslost",
    "-bufferswritten",
    "-logbufferslost",
    "-realtimebufferslost",
    "-loggerthread",
    NULL,
};


/* Event Trace Consumer Support */
struct TwapiETWContext {
    ULONG pointer_size;
    ULONG timer_resolution;
    ULONG user_mode;
};

struct {
    int initialized;
    Tcl_HashTable contexts;
} gETWContexts;

CRITICAL_SECTION gETWCS; /* Access to gETWContexts */


/*
 * Event Trace Provider Support
 *
 * Currently only support *one* provider, shared among all interps and
 * modules. The extra code for locking/bookkeeping etc. for multiple 
 * providers not likely to be used.
 */
GUID   gETWProviderGuid;        /* GUID for our ETW Provider */
TRACEHANDLE gETWProviderRegistrationHandle; /* HANDLE for registered instance of provider */
GUID   gETWProviderEventClassGuid; /* GUID for the event class we use */
HANDLE gETWProviderEventClassRegistrationHandle; /* Handle returned by ETW for above */
ULONG  gETWProviderTraceEnableFlags;             /* Flags set by ETW controller */
ULONG  gETWProviderTraceEnableLevel;             /* Level set by ETW controller */
/* Session our provider is attached to */
TRACEHANDLE gETWProviderSessionHandle = (TRACEHANDLE) INVALID_HANDLE_VALUE;

TCL_RESULT ObjToPEVENT_TRACE_PROPERTIES(
    Tcl_Interp *interp,
    Tcl_Obj *objP,
    EVENT_TRACE_PROPERTIES **etPP)
{
    int i, buf_sz;
    Tcl_Obj **objv;
    int       objc;
    int       logfile_name_i = -1;
    int       session_name_i = -1;
    WCHAR    *logfile_name;
    WCHAR    *session_name;
    EVENT_TRACE_PROPERTIES *etP;

    int field;

    if (Tcl_ListObjGetElements(interp, objP, &objc, &objv) != TCL_OK)
        return TCL_ERROR;

    if (objc & 1) {
        Tcl_SetResult(interp, "EVENT_TRACE_PROPERTIES must contain even number of elements", TCL_STATIC);
        return TCL_ERROR;
    }

    /* First loop and find out required buffers size */
    for (i = 0 ; i < objc ; i += 2) {
        if (Tcl_GetIndexFromObj(interp, objv[i], g_event_trace_fields, "event trace field", TCL_EXACT, &field) != TCL_OK)
            return TCL_ERROR;
        switch (field) {
        case 0: // -logfile
            logfile_name_i = i; /* Duplicates result in last value being used */
            break;
        case 1: // -sessionname
            session_name_i = i;
            break;
        }
    }
    
    if (session_name_i >= 0) {
        session_name = Tcl_GetUnicodeFromObj(objv[session_name_i+1], &session_name_i);
    } else {
        session_name_i = 0;
    }

    if (logfile_name_i >= 0) {
        logfile_name = Tcl_GetUnicodeFromObj(objv[logfile_name_i+1], &logfile_name_i);
    } else {
        logfile_name_i = 0;
    }

    /* Note: session_name_i/logfile_name_i now contain lengths of names */

    buf_sz = sizeof(*etP);
    /* Note 0 length names means we do not specify it or know its length */
    if (session_name_i > 0) {
        buf_sz += sizeof(WCHAR) * (session_name_i+1);
    } else {
        /* Leave max space in case kernel wants to fill it in */
        buf_sz += sizeof(WCHAR) * MAX_TRACE_NAME_CHARS;
    }
    if (logfile_name_i > 0) {
        /* Logfile name may be appended with numeric strings (PID/sequence) */
        buf_sz += sizeof(WCHAR) * (logfile_name_i+1 + 20);
    } else {
        /* Leave max space in case kernel wants to fill it in on a query */
        buf_sz += sizeof(WCHAR) * MAX_TRACE_NAME_CHARS;
    }

    etP = TwapiAlloc(buf_sz);
    ZeroMemory(etP, buf_sz);
    etP->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    etP->Wnode.BufferSize = buf_sz;

    /* Copy the session name and log file name to end of struct.
     * Note that the session name MUST come first
     */
    if (session_name_i > 0) {
        etP->LoggerNameOffset = sizeof(*etP);
        /* TBD - not clear we need to actually copy this here or
           sufficient just to leave space for and StartTrace does it
           itself
        */
        CopyMemory(etP->LoggerNameOffset + (char *)etP,
                   session_name, sizeof(WCHAR) * (session_name_i + 1));
        etP->LogFileNameOffset = sizeof(*etP) + (sizeof(WCHAR) * (session_name_i + 1));
    } else {
        etP->LoggerNameOffset = 0; /* TBD or should it be sizeof(*etP) even in this case with an empty string? */
        /* We left max space for unknown session name */
        etP->LogFileNameOffset = sizeof(*etP) + (sizeof(WCHAR) * MAX_TRACE_NAME_CHARS);
    }
        
    if (logfile_name_i > 0) {
        CopyMemory(etP->LogFileNameOffset + (char *) etP,
                   logfile_name, sizeof(WCHAR) * (logfile_name_i + 1));
    } else {
        etP->LogFileNameOffset = 0;
    }

    /* Now deal with the rest of the fields, after setting some defaults */
    etP->Wnode.ClientContext = 2; /* Default - system timer resolution is cheape
st */
    etP->LogFileMode = EVENT_TRACE_USE_PAGED_MEMORY | EVENT_TRACE_FILE_MODE_APPEND;

    for (i = 0 ; i < objc ; i += 2) {
        ULONG *ulP;

        if (Tcl_GetIndexFromObj(interp, objv[i], g_event_trace_fields, "event trace field", TCL_EXACT, &field) == TCL_ERROR)
            return TCL_ERROR;
        ulP = NULL;
        switch (field) {
        case 0: // -logfile
        case 1: // -sessionname
            /* Already handled above */
            break;
        case 2: //-sessionguid
            if (ObjToGUID(interp, objv[i+1], &etP->Wnode.Guid) != TCL_OK)
                goto error_handler;
            break;
        case 3: // -buffersize
            ulP = &etP->BufferSize;
            break;
        case 4: // -minbuffers
            ulP = &etP->MinimumBuffers;
            break;
        case 5: // -maxbuffers
            ulP = &etP->MaximumBuffers;
            break;
        case 6: // -maximumfilesize
            ulP = &etP->MaximumFileSize;
            break;
        case 7: // -logfilemode
            ulP = &etP->LogFileMode;
            break;
        case 8: // -flushtimer
            ulP = &etP->FlushTimer;
            break;
        case 9: // -enableflags
            ulP = &etP->EnableFlags;
            break;
        case 10: // -clockresolution
            ulP = &etP->Wnode.ClientContext;
            break;
        case 11: // -agelimit
            ulP = &etP->AgeLimit;
            break;
        case 12: // -numbuffers
            ulP = &etP->NumberOfBuffers;
            break;
        case 13: // -freebuffers
            ulP = &etP->FreeBuffers;
            break;
        case 14: // -eventslost
            ulP = &etP->EventsLost;
            break;
        case 15: // -bufferswritten
            ulP = &etP->BuffersWritten;
            break;
        case 16: // -logbufferslost
            ulP = &etP->LogBuffersLost;
            break;
        case 17: // -realtimebufferslost
            ulP = &etP->RealTimeBuffersLost;
            break;
        case 18: // -loggerthread
            if (ObjToHANDLE(interp, objv[i+1], &etP->LoggerThreadId) != TCL_OK)
                goto error_handler;
            break;
        default:
            Tcl_SetResult(interp, "Internal error: Unexpected field index.", TCL_STATIC);
            goto error_handler;
        }
        
        if (ulP == NULL) {
            /* Nothing to do, value already set in the switch */
        } else {
            if (Tcl_GetLongFromObj(interp, objv[i+1], ulP) != TCL_OK)
                goto error_handler;
        }
    }
    
    *etPP = etP;
    return TCL_OK;

error_handler: /* interp must already contain error msg */
    if (etP)
        TwapiFree(etP);
    return TCL_ERROR;
}


static Tcl_Obj *ObjFromEVENT_TRACE_PROPERTIES(EVENT_TRACE_PROPERTIES *etP)
{
    int i;
    Tcl_Obj *objs[38];

    for (i = 0; g_event_trace_fields[i]; ++i) {
        objs[2*i] = Tcl_NewStringObj(g_event_trace_fields[i], -1);
    }

    if (etP->LogFileNameOffset)
        objs[1] = Tcl_NewUnicodeObj((WCHAR *)(etP->LogFileNameOffset + (char *) etP), -1);
    else
        objs[1] = Tcl_NewObj();

    if (etP->LoggerNameOffset)
        objs[3] = Tcl_NewUnicodeObj((WCHAR *)(etP->LoggerNameOffset + (char *) etP), -1);
    else
        objs[3] = Tcl_NewObj();

    objs[5] = ObjFromGUID(&etP->Wnode.Guid);
    objs[7] = Tcl_NewLongObj(etP->BufferSize);
    objs[9] = Tcl_NewLongObj(etP->MinimumBuffers);
    objs[11] = Tcl_NewLongObj(etP->MaximumBuffers);
    objs[13] = Tcl_NewLongObj(etP->MaximumFileSize);
    objs[15] = Tcl_NewLongObj(etP->LogFileMode);
    objs[17] = Tcl_NewLongObj(etP->FlushTimer);
    objs[19] = Tcl_NewLongObj(etP->EnableFlags);
    objs[21] = Tcl_NewLongObj(etP->Wnode.ClientContext);
    objs[23] = Tcl_NewLongObj(etP->AgeLimit);
    objs[25] = Tcl_NewLongObj(etP->NumberOfBuffers);
    objs[27] = Tcl_NewLongObj(etP->FreeBuffers);
    objs[29] = Tcl_NewLongObj(etP->EventsLost);
    objs[31] = Tcl_NewLongObj(etP->BuffersWritten);
    objs[33] = Tcl_NewLongObj(etP->LogBuffersLost);
    objs[35] = Tcl_NewLongObj(etP->RealTimeBuffersLost);
    objs[37] = ObjFromHANDLE(etP->LoggerThreadId);

    return Tcl_NewListObj(ARRAYSIZE(objs), objs);
}


static ULONG WINAPI TwapiETWProviderControlCallback(
    WMIDPREQUESTCODE request,
    PVOID contextP,
    ULONG* reserved, 
    PVOID headerP
    )
{
    ULONG rc = ERROR_SUCCESS;
    TRACEHANDLE session;
    ULONG enable_level;
    ULONG enable_flags;

    /* Mostly cloned from the SDK docs */

    switch (request) {
    case WMI_ENABLE_EVENTS:
        SetLastError(0);
        session = GetTraceLoggerHandle(headerP);
        if ((TRACEHANDLE) INVALID_HANDLE_VALUE == session) {
            /* Bad handle, ignore, but return the error code */
            rc = GetLastError();
            break;
        }

        /* If we are already logging to a session we will ignore nonmatching */
        if (gETWProviderSessionHandle != (TRACEHANDLE) INVALID_HANDLE_VALUE &&
            session != gETWProviderSessionHandle) {
            rc = ERROR_INVALID_PARAMETER;
            break;
        }

        SetLastError(0);
        enable_level = GetTraceEnableLevel(session); 
        if (enable_level == 0) {
            /* *Possible* error */
            rc = GetLastError();
            if (rc)
                break; /* Yep, real error */
        }

        SetLastError(0);
        enable_flags = GetTraceEnableFlags(session);
        if (enable_flags == 0) {
            /* *Possible* error */
            rc = GetLastError();
            if (rc)
                break;
        }

        /* OK, all calls succeeded. Set up the global values */
        gETWProviderSessionHandle = session;
        gETWProviderTraceEnableLevel = enable_level;
        gETWProviderTraceEnableFlags = enable_flags;
        break;
 
    case WMI_DISABLE_EVENTS:  //Disable Provider.
        /* Don't we need to check session handle ? Sample MSDN does not */
        gETWProviderSessionHandle = (TRACEHANDLE) INVALID_HANDLE_VALUE;
        break;

    default:
        rc = ERROR_INVALID_PARAMETER;
        break;
    }

    return rc;
}


TCL_RESULT Twapi_RegisterTraceGuids(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    Tcl_Interp *interp = ticP->interp;
    DWORD rc;
    GUID provider_guid, event_class_guid;
    TRACE_GUID_REGISTRATION event_class_reg;
    
    if (TwapiGetArgs(interp, objc, objv, GETUUID(provider_guid),
                     GETUUID(event_class_guid), ARGEND) != TCL_OK)
        return TCL_ERROR;
    
    if (IsEqualGUID(&provider_guid, &gTwapiNullGuid) ||
        IsEqualGUID(&event_class_guid, &gTwapiNullGuid)) {
        Tcl_SetResult(interp, "NULL provider GUID specified.", TCL_STATIC);
        return TCL_ERROR;
    }

    /* We should not already have registered a different provider */
    if (! IsEqualGUID(&gETWProviderGuid, &gTwapiNullGuid)) {
        if (IsEqualGUID(&gETWProviderGuid, &provider_guid))
            return TCL_OK;      /* Same GUID - ok */
        else {
            Tcl_SetResult(interp, "ETW Provider GUID already registered", TCL_STATIC);
            return TCL_ERROR;
        }
    }

    ZeroMemory(&event_class_reg, sizeof(event_class_reg));
    event_class_reg.Guid = &event_class_guid;
    rc = RegisterTraceGuids(
        (WMIDPREQUEST)TwapiETWProviderControlCallback,
        NULL,                          // No context
        &provider_guid,         // GUID that identifies the provider
        1,                      /* Number of event class GUIDs */
        &event_class_reg,      /* Event class GUID array */
        NULL,                          // Not used
        NULL,                          // Not used
        &gETWProviderRegistrationHandle // Used for UnregisterTraceGuids
        );

    if (rc == ERROR_SUCCESS) {
        gETWProviderGuid = provider_guid;
        gETWProviderEventClassGuid = event_class_guid;
        gETWProviderEventClassRegistrationHandle = event_class_reg.RegHandle;
        return TCL_OK;
    } else {
        return Twapi_AppendSystemError(interp, rc);
    }
}

TCL_RESULT Twapi_UnregisterTraceGuids(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    Tcl_Interp *interp = ticP->interp;
    DWORD rc;
    

    if (objc != 0)
        return TwapiReturnTwapiError(interp, NULL, TWAPI_BAD_ARG_COUNT);

    rc = UnregisterTraceGuids(gETWProviderRegistrationHandle);
    if (rc == ERROR_SUCCESS) {
        gETWProviderRegistrationHandle = 0;
        gETWProviderGuid = gTwapiNullGuid;
        return TCL_OK;
    } else {
        return Twapi_AppendSystemError(interp, rc);
    }
}

TCL_RESULT Twapi_TraceEvent(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    ULONG rc;
    struct {
        EVENT_TRACE_HEADER eth;
        MOF_FIELD mof;
    } event;
    WCHAR *msgP;
    int    msglen;
    int    type;
    int    level;

    /* If no tracing is on, just ignore, do not raise error */
    if (gETWProviderSessionHandle == 0)
        return TCL_OK;

    if (TwapiGetArgs(ticP->interp, objc, objv, GETWSTRN(msgP, msglen),
                     ARGUSEDEFAULT, GETINT(type), GETINT(level),
                     ARGEND) != TCL_OK)
        return TCL_ERROR;

    ZeroMemory(&event, sizeof(event));
    event.eth.Size = sizeof(event);
    event.eth.Flags = WNODE_FLAG_TRACED_GUID |
        WNODE_FLAG_USE_GUID_PTR | WNODE_FLAG_USE_MOF_PTR;
    event.eth.GuidPtr = (ULONGLONG) &gETWProviderEventClassGuid;
    event.eth.Class.Type = type;
    event.eth.Class.Level = level;

    event.mof.DataPtr = (ULONG64) msgP;
    /* Null Termination included to match event definition */
    event.mof.Length = (msglen+1) * sizeof(WCHAR);

    rc = TraceEvent(gETWProviderSessionHandle, &event.eth);
    return rc == ERROR_SUCCESS ?
        TCL_OK : Twapi_AppendSystemError(ticP->interp, rc);
}


TCL_RESULT Twapi_StartTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    EVENT_TRACE_PROPERTIES *etP;
    TRACEHANDLE htrace;
    Tcl_Interp *interp = ticP->interp;
    Tcl_Obj *objs[2];
    
    if (objc != 1)
        return TwapiReturnTwapiError(interp, NULL, TWAPI_BAD_ARG_COUNT);
    if (ObjToPEVENT_TRACE_PROPERTIES(interp, objv[0], &etP) != TCL_OK)
        return TCL_ERROR;

    if (etP->LoggerNameOffset == 0) {
        return TwapiReturnTwapiError(ticP->interp, "Session name not specified", TWAPI_INVALID_ARGS);
    }

    if (StartTraceW(&htrace,
                   (WCHAR *) (etP->LoggerNameOffset + (char *)etP),
                   etP) != ERROR_SUCCESS) {
        Twapi_AppendSystemError(interp, GetLastError());
        TwapiFree(etP);
        return TCL_ERROR;
    }

    objs[0] = ObjFromTRACEHANDLE(htrace);
    objs[1] = ObjFromEVENT_TRACE_PROPERTIES(etP);
    TwapiFree(etP);
    Tcl_SetObjResult(interp, Tcl_NewListObj(2, objs));
    return TCL_OK;
}

TCL_RESULT Twapi_ControlTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    TRACEHANDLE htrace;
    EVENT_TRACE_PROPERTIES *etP;
    WCHAR *session_name;
    Tcl_Interp *interp = ticP->interp;
    ULONG code;

    if (Tcl_GetLongFromObj(interp, objv[0], &code) != TCL_OK)
        return TCL_ERROR;

    if (objc == 2) {
        /* Two arguments - code and EVENT_TRACE_PROPERTIES structure */
        htrace = 0;
    } else if (objc == 3) {
        if (Tcl_GetWideIntFromObj(interp, objv[2], &htrace) != TCL_OK)
            return TCL_ERROR;
    } else {
        return TwapiReturnTwapiError(interp, NULL, TWAPI_BAD_ARG_COUNT);
    }

    if (ObjToPEVENT_TRACE_PROPERTIES(interp, objv[1], &etP) != TCL_OK)
        return TCL_ERROR;

    if (etP->LoggerNameOffset)
        session_name = (WCHAR *)(etP->LoggerNameOffset + (char *)etP);
    else
        session_name = NULL;

    if (ControlTraceW(htrace, session_name, etP, code) != ERROR_SUCCESS) {
        Twapi_AppendSystemError(interp, GetLastError());
        code = TCL_ERROR;
    } else {
        Tcl_SetObjResult(interp, ObjFromEVENT_TRACE_PROPERTIES(etP));
        code = TCL_OK;
    }

    TwapiFree(etP);
    return code;
}

TCL_RESULT Twapi_EnableTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    GUID guid;
    TRACEHANDLE htrace;
    ULONG enable, flags, level;
    Tcl_Interp *interp = ticP->interp;

    if (TwapiGetArgs(interp, objc, objv,
                     GETINT(enable), GETINT(flags), GETINT(level),
                     GETUUID(guid), GETWIDE(htrace),
                     ARGEND) != TCL_OK) {
        return TCL_ERROR;
    }

    if (EnableTrace(enable, flags, level, &guid, htrace) != ERROR_SUCCESS)
        return TwapiReturnSystemError(interp);

    return TCL_OK;
}


void WINAPI TwapiETWEventCallback(
  PEVENT_TRACE pEvent
)
{
    return;
}

ULONG WINAPI TwapiETWBufferCallback(
  PEVENT_TRACE_LOGFILEW Buffer
)
{
    return FALSE;
}



TCL_RESULT Twapi_OpenTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    TRACEHANDLE htrace;
    EVENT_TRACE_LOGFILEW etl;
    int real_time;
    Tcl_Interp *interp = ticP->interp;

    if (objc != 2)
        return TwapiReturnTwapiError(interp, NULL, TWAPI_BAD_ARG_COUNT);

    if (Tcl_GetIntFromObj(interp, objv[1], &real_time) != TCL_OK)
        return TCL_ERROR;

    ZeroMemory(&etl, sizeof(etl));
    if (real_time) {
        etl.LoggerName = Tcl_GetUnicode(objv[0]);
        etl.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    } else 
        etl.LogFileName = Tcl_GetUnicode(objv[0]);

    etl.BufferCallback = TwapiETWBufferCallback;
    etl.EventCallback = TwapiETWEventCallback;

    htrace = OpenTraceW(&etl);
    if ((TRACEHANDLE) INVALID_HANDLE_VALUE == htrace)
        return TwapiReturnSystemError(ticP->interp);

    Tcl_SetObjResult(ticP->interp, ObjFromTRACEHANDLE(htrace));
    return TCL_OK;
}

TCL_RESULT Twapi_CloseTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    TRACEHANDLE htrace;
    ULONG err;
    Tcl_Interp *interp = ticP->interp;

    if (objc != 1)
        return TwapiReturnTwapiError(interp, NULL, TWAPI_BAD_ARG_COUNT);

    if (ObjToTRACEHANDLE(interp, objv[0], &htrace) != TCL_OK)
        return TCL_ERROR;

    err = CloseTrace(htrace);
    if (err == ERROR_SUCCESS)
        return TCL_OK;
    else
        return Twapi_AppendSystemError(interp, err);
}


TCL_RESULT Twapi_ProcessTrace(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    FILETIME start, end, *startP, *endP;
    TRACEHANDLE htraces[1];
    Tcl_Interp *interp = ticP->interp;
    ULONG winerr;
    struct TwapiETWContext *tetcP;

    if (objc == 0 || objc > 3)
        return TwapiReturnTwapiError(interp, NULL, TWAPI_BAD_ARG_COUNT);

    if (ObjToTRACEHANDLE(interp, objv[0], &htraces[0]) != TCL_OK)
        return TCL_ERROR;

    startP = NULL;
    if (objc > 1) {
        if (ObjToFILETIME(interp, objv[1], &start) != TCL_OK)
            return TCL_ERROR;
        startP = &start;
    }
    
    endP = NULL;
    if (objc > 2) {
        if (ObjToFILETIME(interp, objv[2], &end) != TCL_OK)
            return TCL_ERROR;
        endP = &end;
    }

    EnterCriticalSection(&gETWCS);
    
    if (! gETWContexts.initialized) {
        Tcl_InitObjHashTable(&gETWContexts.contexts);
        gETWContexts.initialized = 1;
    }

    winerr = ProcessTrace(htraces, 1, startP, endP);

    LeaveCriticalSection(&gETWCS);

    if (winerr == ERROR_SUCCESS) {
        // TBD - deal with data

        return TCL_OK;
    } else {
        // TBD - clean up

        return Twapi_AppendSystemError(interp, winerr);
    }
}
