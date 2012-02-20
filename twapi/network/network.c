/*
 * Copyright (c) 2004-2010 Ashok P. Nadkarni
 * All rights reserved.
 *
 * See the file LICENSE for license
 */

#include "twapi.h"
#include <ntverp.h>

#if VER_PRODUCTBUILD <= 3790
typedef struct _TCPIP_OWNER_MODULE_BASIC_INFO {
    PWCHAR pModuleName;
    PWCHAR pModulePath;
} TCPIP_OWNER_MODULE_BASIC_INFO;
#endif

typedef struct _TwapiHostnameEvent {
    Tcl_Event tcl_ev;           /* Must be first field */
    TwapiInterpContext *ticP;
    TwapiId    id;             /* Passed from script as a request id */
    DWORD  status;         /* 0 -> success, else Win32 error code */
    union {
        struct addrinfo *addrinfolist; /* Returned by getaddrinfo, to be
                                          freed via freeaddrinfo
                                          Used for host->addr */
        char *hostname;      /* ckalloc'ed (used for addr->hostname) */
    };
    int family;                 /* AF_UNSPEC, AF_INET or AF_INET6 */
    char name[1];           /* Holds query for hostname->addr */
    /* VARIABLE SIZE SINCE name[] IS ARBITRARY SIZE */
} TwapiHostnameEvent;
/*
 * Macro to calculate struct size. Note terminating null and the sizeof
 * the name[] array cancel each other out. (namelen_) does not include
 * terminating null.
 */
#define SIZE_TwapiHostnameEvent(namelen_) \
    (sizeof(TwapiHostnameEvent) + (namelen_))

/* Undocumented functions */
typedef DWORD (WINAPI *GetOwnerModuleFromTcpEntry_t)(PVOID, int, PVOID, PDWORD);
MAKE_DYNLOAD_FUNC(GetOwnerModuleFromTcpEntry, iphlpapi, GetOwnerModuleFromTcpEntry_t)
typedef DWORD (WINAPI *GetOwnerModuleFromUdpEntry_t)(PVOID, int, PVOID, PDWORD);
MAKE_DYNLOAD_FUNC(GetOwnerModuleFromUdpEntry, iphlpapi, GetOwnerModuleFromUdpEntry_t)
typedef DWORD (WINAPI *GetOwnerModuleFromTcp6Entry_t)(PVOID, int, PVOID, PDWORD);
MAKE_DYNLOAD_FUNC(GetOwnerModuleFromTcp6Entry, iphlpapi, GetOwnerModuleFromTcp6Entry_t)
typedef DWORD (WINAPI *GetOwnerModuleFromUdp6Entry_t)(PVOID, int, PVOID, PDWORD);
MAKE_DYNLOAD_FUNC(GetOwnerModuleFromUdp6Entry, iphlpapi, GetOwnerModuleFromUdp6Entry_t)

typedef DWORD (WINAPI *GetBestInterfaceEx_t)(struct sockaddr*, DWORD *);
MAKE_DYNLOAD_FUNC(GetBestInterfaceEx, iphlpapi, GetBestInterfaceEx_t)

#ifndef TWAPI_STATIC_BUILD
HMODULE gModuleHandle;
#endif

Tcl_Obj *ObjFromMIB_IPNETROW(Tcl_Interp *interp, const MIB_IPNETROW *netrP);
Tcl_Obj *ObjFromMIB_IPNETTABLE(Tcl_Interp *interp, MIB_IPNETTABLE *nettP);
Tcl_Obj *ObjFromMIB_IPFORWARDROW(Tcl_Interp *interp, const MIB_IPFORWARDROW *ipfrP);
Tcl_Obj *ObjFromMIB_IPFORWARDTABLE(Tcl_Interp *interp, MIB_IPFORWARDTABLE *fwdP);
Tcl_Obj *ObjFromIP_ADAPTER_INDEX_MAP(Tcl_Interp *interp, IP_ADAPTER_INDEX_MAP *iaimP);
Tcl_Obj *ObjFromIP_INTERFACE_INFO(Tcl_Interp *interp, IP_INTERFACE_INFO *iiP);
Tcl_Obj *ObjFromMIB_TCPROW(Tcl_Interp *interp, const MIB_TCPROW *row, int size);
int ObjToMIB_TCPROW(Tcl_Interp *interp, Tcl_Obj *listObj, MIB_TCPROW *row);
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

/* Returns address family or AF_UNSPEC if s could not be parsed */
/* GetLastError() is set in latter case */
int TwapiStringToSOCKADDR_STORAGE(char *s, SOCKADDR_STORAGE *ssP, int family)
{
    int sz;

    /* Note Tcl_GetIntFromObj may have made s invalid */
    if (family != AF_UNSPEC) {
        ssP->ss_family = family; /* MSDN says this is required to be set */
        if (WSAStringToAddressA(s,
                                family, NULL,
                                (struct sockaddr *)ssP, &sz) != 0) {
            return AF_UNSPEC;
        }
    } else {
        /* Family not explicitly specified. */
        /* Try converting as IPv4 first, then IPv6 */
        ssP->ss_family = AF_INET; /* MSDN says this is required to be set */
        sz = sizeof(*ssP);
        if (WSAStringToAddressA(s,
                                AF_INET, NULL,
                                (struct sockaddr *)ssP, &sz) != 0) {
            sz = sizeof(*ssP);
            ssP->ss_family = AF_INET6;/* MSDN says this is required to be set */
            if (WSAStringToAddressA(s,
                                    AF_INET6, NULL,
                                    (struct sockaddr *)ssP, &sz) != 0)
                return AF_UNSPEC;
        }
    }
    return ssP->ss_family;
}


/* Note *ssP may be modified even on error return */
int ObjToSOCKADDR_STORAGE(Tcl_Interp *interp, Tcl_Obj *objP, SOCKADDR_STORAGE *ssP)
{
    Tcl_Obj **objv;
    int       objc;
    Tcl_Obj **addrv;
    int       addrc;
    int       family;
    int       sz = sizeof(*ssP);
    WORD      port;

    if (Tcl_ListObjGetElements(interp, objP, &objc, &objv) != TCL_OK)
        return TCL_ERROR;

    if (objc > 2)
        return TwapiReturnError(interp, TWAPI_BAD_ARG_COUNT);

    if (objc == 0) {
        /* Assume IP v4 0.0.0.0 */
        ((struct sockaddr_in *)ssP)->sin_family = AF_INET;
        ((struct sockaddr_in *)ssP)->sin_addr.s_addr = 0;
        ((struct sockaddr_in *)ssP)->sin_port = 0;
        return TCL_OK;
    }

    /*
     * An address may be a pair {ipversion addressstring} or just an
     * address string. If it is anything other than the first form,
     * we treat it as a string.
     */
    family = AF_UNSPEC;
    if (Tcl_ListObjGetElements(interp, objv[0], &addrc, &addrv) == TCL_OK &&
        addrc == 2) {
        char *s = Tcl_GetString(addrv[0]);
        if (!lstrcmpA(s, "inet"))
            family = AF_INET;
        else if (!lstrcmpA(s, "inet6"))
            family = AF_INET6;
        else if (Tcl_GetIntFromObj(NULL, addrv[0], &family) != TCL_OK ||
                (family != AF_INET && family != AF_INET6))
            family = AF_UNSPEC;
        /* Note Tcl_GetIntFromObj may have made s invalid */
        if (family != AF_UNSPEC) {
            if (TwapiStringToSOCKADDR_STORAGE(Tcl_GetString(addrv[1]), ssP, family) != family)
                goto error_return;
        }
    }

    if (family == AF_UNSPEC) {
        /* Family not explicitly specified. */
        /* Treat as a single string. Try converting as IPv4 first, then IPv6 */
        if (TwapiStringToSOCKADDR_STORAGE(Tcl_GetString(objv[0]), ssP, AF_INET) == AF_UNSPEC &&
            TwapiStringToSOCKADDR_STORAGE(Tcl_GetString(objv[0]), ssP, AF_INET6) == AF_UNSPEC) {
            goto error_return;
        }
    }
    
    /* OK, we have the address, see if a port was specified */
    if (objc == 1)
        return TCL_OK;

    /* Decipher port */
    if (ObjToWord(interp, objv[1], &port) != TCL_OK)
        return TCL_ERROR;

    port = htons(port);
    if (ssP->ss_family == AF_INET) {
        ((struct sockaddr_in *)ssP)->sin_port = port;
    } else {
        ((struct sockaddr_in6 *)ssP)->sin6_port = port;
    }

    return TCL_OK;

error_return:
    return Twapi_AppendSystemError(interp, WSAGetLastError());
}

/* TBD - see if can be replaced by ObjToSOCKADDR_STORAGE */
int ObjToSOCKADDR_IN(Tcl_Interp *interp, Tcl_Obj *objP, struct sockaddr_in *sinP)
{
    Tcl_Obj **objv;
    int       objc;

    if (Tcl_ListObjGetElements(interp, objP, &objc, &objv) != TCL_OK)
        return TCL_ERROR;

    sinP->sin_family = AF_INET;
    sinP->sin_addr.s_addr = 0;
    sinP->sin_port = 0;

    if (objc > 0) {
        sinP->sin_addr.s_addr = inet_addr(Tcl_GetString(objv[0]));
    }

    if (objc > 1) {
        if (ObjToWord(interp, objv[1], &sinP->sin_port) != TCL_OK)
            return TCL_ERROR;

        sinP->sin_port = htons(sinP->sin_port);
    }

    return TCL_OK;
}

/* Returns NULL on error */
static Tcl_Obj *ObjFromSOCKET_ADDRESS(SOCKET_ADDRESS *saP)
{

    if (saP
        && saP->lpSockaddr
        && ((((SOCKADDR_IN *) (saP->lpSockaddr))->sin_family == AF_INET
             && saP->iSockaddrLength == sizeof(SOCKADDR_IN))
            ||
            (((SOCKADDR_IN *) (saP->lpSockaddr))->sin_family == AF_INET6
             && saP->iSockaddrLength == sizeof(SOCKADDR_IN6)))) {
        return ObjFromSOCKADDR_address(saP->lpSockaddr);
        /* Error already set */
    } else {
        SetLastError(ERROR_INVALID_PARAMETER);
    }

    return NULL;
}

/*
 * Given a IP_ADDR_STRING list, return a Tcl_Obj containing only
 * the IP Address components
 */
static Tcl_Obj *ObjFromIP_ADDR_STRINGAddress (
    Tcl_Interp *interp, const IP_ADDR_STRING *ipaddrstrP
)
{
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
    while (ipaddrstrP) {
        if (ipaddrstrP->IpAddress.String[0])
            Tcl_ListObjAppendElement(interp, resultObj,
                                     Tcl_NewStringObj(ipaddrstrP->IpAddress.String, -1));
        ipaddrstrP = ipaddrstrP->Next;
    }

    return resultObj;
}


static Tcl_Obj *ObjFromIP_ADAPTER_UNICAST_ADDRESS(IP_ADAPTER_UNICAST_ADDRESS *iauaP)
{
    Tcl_Obj *objv[16];

    objv[0] = STRING_LITERAL_OBJ("-flags");
    objv[1] = Tcl_NewIntObj(iauaP->Flags);
    objv[2] = STRING_LITERAL_OBJ("-address");
    if (iauaP->Address.lpSockaddr == NULL ||
        (objv[3] = ObjFromSOCKET_ADDRESS(&iauaP->Address)) == NULL) {
        /* Did not recognize socket address type */
        objv[3] = Tcl_NewObj();
    }
    objv[4] = STRING_LITERAL_OBJ("-prefixorigin");
    objv[5] = Tcl_NewIntObj(iauaP->PrefixOrigin);
    objv[6] = STRING_LITERAL_OBJ("-suffixorigin");
    objv[7] = Tcl_NewIntObj(iauaP->SuffixOrigin);
    objv[8] = STRING_LITERAL_OBJ("-dadstate");
    objv[9] = Tcl_NewIntObj(iauaP->DadState);
    objv[10] = STRING_LITERAL_OBJ("-validlifetime");
    objv[11] = ObjFromULONG(iauaP->ValidLifetime);
    objv[12] = STRING_LITERAL_OBJ("-preferredlifetime");
    objv[13] = ObjFromULONG(iauaP->PreferredLifetime);
    objv[14] = STRING_LITERAL_OBJ("-leaselifetime");
    objv[15] = ObjFromULONG(iauaP->LeaseLifetime);


    return Tcl_NewListObj(ARRAYSIZE(objv), objv);
}


static Tcl_Obj *ObjFromIP_ADAPTER_ANYCAST_ADDRESS(IP_ADAPTER_ANYCAST_ADDRESS *iaaaP)
{
    Tcl_Obj *objv[4];

    objv[0] = STRING_LITERAL_OBJ("-flags");
    objv[1] = ObjFromDWORD(iaaaP->Flags);
    objv[2] = STRING_LITERAL_OBJ("-address");
    if (iaaaP->Address.lpSockaddr == NULL ||
        (objv[3] = ObjFromSOCKET_ADDRESS(&iaaaP->Address)) == NULL) {
        /* Did not recognize socket address type */
        objv[3] = Tcl_NewObj();
    }

    return Tcl_NewListObj(ARRAYSIZE(objv), objv);
}
#define ObjFromIP_ADAPTER_MULTICAST_ADDRESS(p_) ObjFromIP_ADAPTER_ANYCAST_ADDRESS((IP_ADAPTER_ANYCAST_ADDRESS*) (p_))
#define ObjFromIP_ADAPTER_DNS_SERVER_ADDRESS(p_) ObjFromIP_ADAPTER_ANYCAST_ADDRESS((IP_ADAPTER_ANYCAST_ADDRESS*) (p_))


static Tcl_Obj *ObjFromIP_ADAPTER_PREFIX(IP_ADAPTER_PREFIX *iapP)
{
    Tcl_Obj *objv[6];

    objv[0] = STRING_LITERAL_OBJ("-flags");
    objv[1] = ObjFromDWORD(iapP->Flags);
    objv[2] = STRING_LITERAL_OBJ("-address");
    if (iapP->Address.lpSockaddr == NULL ||
        (objv[3] = ObjFromSOCKET_ADDRESS(&iapP->Address)) == NULL) {
        /* Did not recognize socket address type */
        objv[3] = Tcl_NewObj();
    }
    objv[4] = STRING_LITERAL_OBJ("-prefixlength");
    objv[5] = ObjFromDWORD(iapP->PrefixLength);

    return Tcl_NewListObj(ARRAYSIZE(objv), objv);
}

Tcl_Obj *ObjFromIP_ADAPTER_ADDRESSES(IP_ADAPTER_ADDRESSES *iaaP)
{
    Tcl_Obj *objv[34];
    Tcl_Obj *fieldObjs[16];
    IP_ADAPTER_UNICAST_ADDRESS *unicastP;
    IP_ADAPTER_ANYCAST_ADDRESS *anycastP;
    IP_ADAPTER_MULTICAST_ADDRESS *multicastP;
    IP_ADAPTER_DNS_SERVER_ADDRESS *dnsserverP;
    IP_ADAPTER_PREFIX *prefixP;
    int i;
    
    objv[0] = STRING_LITERAL_OBJ("-ifindex");
    objv[1] = Tcl_NewIntObj(iaaP->IfIndex);
    objv[2] = STRING_LITERAL_OBJ("-adaptername");
    objv[3] = Tcl_NewStringObj(iaaP->AdapterName, -1);
    objv[4] = STRING_LITERAL_OBJ("-unicastaddresses");
    objv[5] = Tcl_NewListObj(0, NULL);
    unicastP = iaaP->FirstUnicastAddress;
    while (unicastP) {
        Tcl_ListObjAppendElement(NULL, objv[5], ObjFromIP_ADAPTER_UNICAST_ADDRESS(unicastP));
        unicastP = unicastP->Next;
    }
    objv[6] = STRING_LITERAL_OBJ("-anycastaddresses");
    objv[7] = Tcl_NewListObj(0, NULL);
    anycastP = iaaP->FirstAnycastAddress;
    while (anycastP) {
        Tcl_ListObjAppendElement(NULL, objv[7], ObjFromIP_ADAPTER_ANYCAST_ADDRESS(anycastP));
        anycastP = anycastP->Next;
    }
    objv[8] = STRING_LITERAL_OBJ("-multicastaddresses");
    objv[9] = Tcl_NewListObj(0, NULL);
    multicastP = iaaP->FirstMulticastAddress;
    while (multicastP) {
        Tcl_ListObjAppendElement(NULL, objv[9], ObjFromIP_ADAPTER_MULTICAST_ADDRESS(multicastP));
        multicastP = multicastP->Next;
    }
    objv[10] = STRING_LITERAL_OBJ("-dnsservers");
    objv[11] = Tcl_NewListObj(0, NULL);
    dnsserverP = iaaP->FirstDnsServerAddress;
    while (dnsserverP) {
        Tcl_ListObjAppendElement(NULL, objv[11], ObjFromIP_ADAPTER_DNS_SERVER_ADDRESS(dnsserverP));
        dnsserverP = dnsserverP->Next;
    }

    objv[12] = STRING_LITERAL_OBJ("-dnssuffix");
    objv[13] = ObjFromUnicode(iaaP->DnsSuffix ? iaaP->DnsSuffix : L"");

    objv[14] = STRING_LITERAL_OBJ("-description");
    objv[15] = ObjFromUnicode(iaaP->Description ? iaaP->Description : L"");
    objv[16] = STRING_LITERAL_OBJ("-friendlyname");
    objv[17] = ObjFromUnicode(iaaP->FriendlyName ? iaaP->FriendlyName : L"");

    objv[18] = STRING_LITERAL_OBJ("-physicaladdress");
    objv[19] = Tcl_NewByteArrayObj(iaaP->PhysicalAddress, iaaP->PhysicalAddressLength);

    objv[20] = STRING_LITERAL_OBJ("-flags");
    objv[21] = ObjFromDWORD(iaaP->Flags);

    objv[22] = STRING_LITERAL_OBJ("-mtu");
    objv[23] = ObjFromDWORD(iaaP->Mtu);

    objv[24] = STRING_LITERAL_OBJ("-type");
    objv[25] = ObjFromDWORD(iaaP->IfType);

    objv[26] = STRING_LITERAL_OBJ("-operstatus");
    objv[27] = ObjFromDWORD(iaaP->OperStatus);

    /*
     * Remaining fields are only available with XP SP1 or later. Check
     * length against the size of our struct definition.
     */

    objv[28] = STRING_LITERAL_OBJ("-ipv6ifindex");
    objv[30] = STRING_LITERAL_OBJ("-zoneindices");
    objv[32] = STRING_LITERAL_OBJ("-prefixes");
    if (iaaP->Length >= sizeof(*iaaP)) {
        objv[29] = ObjFromDWORD(iaaP->Ipv6IfIndex);
        for (i=0; i < 16; ++i) {
            fieldObjs[i] = ObjFromDWORD(iaaP->ZoneIndices[i]);
        }
        objv[31] = Tcl_NewListObj(16, fieldObjs);

        objv[33] = Tcl_NewListObj(0, NULL);
        prefixP = iaaP->FirstPrefix;
        while (prefixP) {
            Tcl_ListObjAppendElement(NULL, objv[33],
                                     ObjFromIP_ADAPTER_PREFIX(prefixP));
            prefixP = prefixP->Next;
        }
    } else {
        objv[29] = ObjFromDWORD(0);
        objv[31] = Tcl_NewObj(); /* Empty object */
        Tcl_IncrRefCount(objv[31]);
        objv[33] = objv[31];
    }

    return Tcl_NewListObj(ARRAYSIZE(objv), objv);
}


Tcl_Obj *ObjFromMIB_IPADDRROW(Tcl_Interp *interp, const MIB_IPADDRROW *iparP)
{
    Tcl_Obj *objv[5];

    objv[0] = IPAddrObjFromDWORD(iparP->dwAddr);
    objv[1] = Tcl_NewIntObj(iparP->dwIndex);
    objv[2] = IPAddrObjFromDWORD(iparP->dwMask);
    objv[3] = IPAddrObjFromDWORD(iparP->dwBCastAddr);
    objv[4] = Tcl_NewIntObj(iparP->dwReasmSize);
    return Tcl_NewListObj(5, objv);
}

Tcl_Obj *ObjFromMIB_IPADDRTABLE(Tcl_Interp *interp, MIB_IPADDRTABLE *ipatP)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < ipatP->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_IPADDRROW(interp,
                                                          &ipatP->table[i])
            );
    }
    return resultObj;
}

Tcl_Obj *ObjFromMIB_IFROW(Tcl_Interp *interp, const MIB_IFROW *ifrP)
{
    Tcl_Obj *objv[22];
    int len;
#if 0
    This field does not seem to contain a consistent format
    objv[0] = ObjFromUnicode(ifrP->wszName);
#else
    objv[0] = Tcl_NewStringObj("", 0);
#endif
    objv[1] = Tcl_NewIntObj(ifrP->dwIndex);
    objv[2] = Tcl_NewIntObj(ifrP->dwType);
    objv[3] = Tcl_NewIntObj(ifrP->dwMtu);
    objv[4] = Tcl_NewIntObj(ifrP->dwSpeed);
    objv[5] = Tcl_NewByteArrayObj(ifrP->bPhysAddr,ifrP->dwPhysAddrLen);
    objv[6] = Tcl_NewIntObj(ifrP->dwAdminStatus);
    objv[7] = Tcl_NewIntObj(ifrP->dwOperStatus);
    objv[8] = Tcl_NewIntObj(ifrP->dwLastChange);
    objv[9] = Tcl_NewWideIntObj(ifrP->dwInOctets);
    objv[10] = Tcl_NewWideIntObj(ifrP->dwInUcastPkts);
    objv[11] = Tcl_NewWideIntObj(ifrP->dwInNUcastPkts);
    objv[12] = Tcl_NewWideIntObj(ifrP->dwInDiscards);
    objv[13] = Tcl_NewWideIntObj(ifrP->dwInErrors);
    objv[14] = Tcl_NewIntObj(ifrP->dwInUnknownProtos);
    objv[15] = Tcl_NewWideIntObj(ifrP->dwOutOctets);
    objv[16] = Tcl_NewWideIntObj(ifrP->dwOutUcastPkts);
    objv[17] = Tcl_NewWideIntObj(ifrP->dwOutNUcastPkts);
    objv[18] = Tcl_NewWideIntObj(ifrP->dwOutDiscards);
    objv[19] = Tcl_NewWideIntObj(ifrP->dwOutErrors);
    objv[20] = Tcl_NewIntObj(ifrP->dwOutQLen);
    len =  ifrP->dwDescrLen;
    if (ifrP->bDescr[len-1] == 0)
        --len; /* Sometimes, not always, there is a terminating null */
    objv[21] = Tcl_NewStringObj(ifrP->bDescr, len);

    return Tcl_NewListObj(sizeof(objv)/sizeof(objv[0]), objv);
}

Tcl_Obj *ObjFromMIB_IFTABLE(Tcl_Interp *interp, MIB_IFTABLE *iftP)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < iftP->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_IFROW(interp,
                                                      &iftP->table[i])
            );
    }
    return resultObj;
}


Tcl_Obj *ObjFromMIB_IPNETROW(Tcl_Interp *interp, const MIB_IPNETROW *netrP)
{
    Tcl_Obj *objv[4];

    objv[0] = Tcl_NewIntObj(netrP->dwIndex);
    objv[1] = Tcl_NewByteArrayObj(netrP->bPhysAddr, netrP->dwPhysAddrLen);
    objv[2] = IPAddrObjFromDWORD(netrP->dwAddr);
    objv[3] = Tcl_NewIntObj(netrP->dwType);
    return Tcl_NewListObj(4, objv);
}

Tcl_Obj *ObjFromMIB_IPNETTABLE(Tcl_Interp *interp, MIB_IPNETTABLE *nettP)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < nettP->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_IPNETROW(interp,
                                                         &nettP->table[i])
            );
    }
    return resultObj;
}

Tcl_Obj *ObjFromMIB_IPFORWARDROW(Tcl_Interp *interp, const MIB_IPFORWARDROW *ipfrP)
{
    Tcl_Obj *objv[14];

    objv[0] = IPAddrObjFromDWORD(ipfrP->dwForwardDest);
    objv[1] = IPAddrObjFromDWORD(ipfrP->dwForwardMask);
    objv[2] = Tcl_NewIntObj(ipfrP->dwForwardPolicy);
    objv[3] = IPAddrObjFromDWORD(ipfrP->dwForwardNextHop);
    objv[4] = Tcl_NewIntObj(ipfrP->dwForwardIfIndex);
    objv[5] = Tcl_NewIntObj(ipfrP->dwForwardType);
    objv[6] = Tcl_NewIntObj(ipfrP->dwForwardProto);
    objv[7] = Tcl_NewIntObj(ipfrP->dwForwardAge);
    objv[8] = Tcl_NewIntObj(ipfrP->dwForwardNextHopAS);
    objv[9] = Tcl_NewIntObj(ipfrP->dwForwardMetric1);
    objv[10] = Tcl_NewIntObj(ipfrP->dwForwardMetric2);
    objv[11] = Tcl_NewIntObj(ipfrP->dwForwardMetric3);
    objv[12] = Tcl_NewIntObj(ipfrP->dwForwardMetric4);
    objv[13] = Tcl_NewIntObj(ipfrP->dwForwardMetric5);

    return Tcl_NewListObj(14, objv);
}

Tcl_Obj *ObjFromMIB_IPFORWARDTABLE(Tcl_Interp *interp, MIB_IPFORWARDTABLE *fwdP)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < fwdP->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_IPFORWARDROW(interp,
                                                             &fwdP->table[i])
            );
    }
    return resultObj;
}

Tcl_Obj *ObjFromIP_ADAPTER_INDEX_MAP(Tcl_Interp *interp, IP_ADAPTER_INDEX_MAP *iaimP)
{
    Tcl_Obj *objv[2];
    objv[0] = Tcl_NewIntObj(iaimP->Index);
    objv[1] = ObjFromUnicode(iaimP->Name);
    return Tcl_NewListObj(2, objv);
}

Tcl_Obj *ObjFromIP_INTERFACE_INFO(Tcl_Interp *interp, IP_INTERFACE_INFO *iiP)
{
    int i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < iiP->NumAdapters; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromIP_ADAPTER_INDEX_MAP(interp,
                                                                 &iiP->Adapter[i])
            );
    }
    return resultObj;
}


Tcl_Obj *ObjFromMIB_TCPROW(Tcl_Interp *interp, const MIB_TCPROW *row, int size)
{
    Tcl_Obj *obj[9];
    char     buf[sizeof(TCPIP_OWNER_MODULE_BASIC_INFO) + 4*MAX_PATH];
    DWORD    buf_sz;
    GetOwnerModuleFromTcpEntry_t  fn;

    obj[0] = Tcl_NewIntObj(row->dwState);
    obj[1] = IPAddrObjFromDWORD(row->dwLocalAddr);
    obj[2] = Tcl_NewIntObj(ntohs((WORD)row->dwLocalPort));
    obj[3] = IPAddrObjFromDWORD(row->dwRemoteAddr);
    obj[4] = Tcl_NewIntObj(ntohs((WORD)row->dwRemotePort));

    if (size < sizeof(MIB_TCPROW_OWNER_PID))
        return Tcl_NewListObj(5, obj);

    obj[5] = Tcl_NewIntObj(((MIB_TCPROW_OWNER_PID *)row)->dwOwningPid);

    if (size < sizeof(MIB_TCPROW_OWNER_MODULE))
        return Tcl_NewListObj(6, obj);

    obj[6] = Tcl_NewWideIntObj(((MIB_TCPROW_OWNER_MODULE *)row)->liCreateTimestamp.QuadPart);

    fn = Twapi_GetProc_GetOwnerModuleFromTcpEntry();
    buf_sz = sizeof(buf);
    if (fn && (*fn)((MIB_TCPROW_OWNER_MODULE *)row,
                    0, // TCPIP_OWNER_MODULE_BASIC_INFO
                    buf, &buf_sz) == NO_ERROR) {
        TCPIP_OWNER_MODULE_BASIC_INFO *modP;
        modP = (TCPIP_OWNER_MODULE_BASIC_INFO *) buf;
        obj[7] = ObjFromUnicode(modP->pModuleName);
        obj[8] = ObjFromUnicode(modP->pModulePath);
    } else {
        obj[7] = Tcl_NewStringObj("", -1);
        obj[8] = Tcl_NewStringObj("", -1);
    }

    return Tcl_NewListObj(9, obj);
}

int ObjToMIB_TCPROW(Tcl_Interp *interp, Tcl_Obj *listObj,
                    MIB_TCPROW *row)
{
    int  objc;
    Tcl_Obj **objv;

    if (Tcl_ListObjGetElements(interp, listObj, &objc, &objv) != TCL_OK)
        return TCL_ERROR;

    if (objc != 5) {
        if (interp)
            Tcl_AppendResult(interp, "Invalid TCP connection format: ",
                             Tcl_GetString(listObj),
                             NULL);
        return TCL_ERROR;
    }

    if ((Tcl_GetIntFromObj(interp, objv[0], &row->dwState) != TCL_OK) ||
        (IPAddrObjToDWORD(interp, objv[1], &row->dwLocalAddr) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[2], &row->dwLocalPort) != TCL_OK) ||
        (IPAddrObjToDWORD(interp, objv[3], &row->dwRemoteAddr) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[4], &row->dwRemotePort) != TCL_OK)) {
        /* interp already has error */
        return TCL_ERROR;
    }

    /* COnvert ports to network format */
    row->dwLocalPort = htons((short)row->dwLocalPort);
    row->dwRemotePort = htons((short)row->dwRemotePort);

    return TCL_OK;
}

Tcl_Obj *ObjFromMIB_TCP6ROW(Tcl_Interp *interp, const MIB_TCP6ROW_OWNER_PID *row, int size)
{
    Tcl_Obj *obj[9];
    char     buf[sizeof(TCPIP_OWNER_MODULE_BASIC_INFO) + 4*MAX_PATH];
    DWORD    buf_sz;
    GetOwnerModuleFromTcpEntry_t  fn;

    obj[0] = Tcl_NewIntObj(row->dwState);
    obj[1] = ObjFromIPv6Addr(row->ucLocalAddr, row->dwLocalScopeId);
    obj[2] = Tcl_NewIntObj(ntohs((WORD)row->dwLocalPort));
    obj[3] = ObjFromIPv6Addr(row->ucRemoteAddr, row->dwRemoteScopeId);
    obj[4] = Tcl_NewIntObj(ntohs((WORD)row->dwRemotePort));
    obj[5] = Tcl_NewIntObj(row->dwOwningPid);

    if (size < sizeof(MIB_TCP6ROW_OWNER_MODULE))
        return Tcl_NewListObj(6, obj);

    obj[6] = Tcl_NewWideIntObj(((MIB_TCP6ROW_OWNER_MODULE *)row)->liCreateTimestamp.QuadPart);

    fn = Twapi_GetProc_GetOwnerModuleFromTcp6Entry();
    buf_sz = sizeof(buf);
    if (fn && (*fn)((MIB_TCP6ROW_OWNER_MODULE *)row,
                    0, //TCPIP_OWNER_MODULE_BASIC_INFO
                    buf, &buf_sz) == NO_ERROR) {
        TCPIP_OWNER_MODULE_BASIC_INFO *modP;
        modP = (TCPIP_OWNER_MODULE_BASIC_INFO *) buf;
        obj[7] = ObjFromUnicode(modP->pModuleName);
        obj[8] = ObjFromUnicode(modP->pModulePath);
    } else {
        obj[7] = Tcl_NewStringObj("", -1);
        obj[8] = Tcl_NewStringObj("", -1);
    }

    return Tcl_NewListObj(9, obj);
}


Tcl_Obj *ObjFromMIB_UDPROW(Tcl_Interp *interp, MIB_UDPROW *row, int size)
{
    Tcl_Obj *obj[6];
    char     buf[sizeof(TCPIP_OWNER_MODULE_BASIC_INFO) + 4*MAX_PATH];
    DWORD    buf_sz;
    GetOwnerModuleFromUdpEntry_t fn;

    obj[0] = IPAddrObjFromDWORD(row->dwLocalAddr);
    obj[1] = Tcl_NewIntObj(ntohs((WORD)row->dwLocalPort));

    if (size < sizeof(MIB_UDPROW_OWNER_PID))
        return Tcl_NewListObj(2, obj);

    obj[2] = Tcl_NewIntObj(((MIB_UDPROW_OWNER_PID *)row)->dwOwningPid);

    if (size < sizeof(MIB_UDPROW_OWNER_MODULE))
        return Tcl_NewListObj(3, obj);

    obj[3] = Tcl_NewWideIntObj(((MIB_UDPROW_OWNER_MODULE *)row)->liCreateTimestamp.QuadPart);

    fn = Twapi_GetProc_GetOwnerModuleFromUdpEntry();
    buf_sz = sizeof(buf);
    if (fn && (*fn)((MIB_UDPROW_OWNER_MODULE *)row,
                    0, // TCPIP_OWNER_MODULE_BASIC_INFO enum
                    buf, &buf_sz) == NO_ERROR) {
        TCPIP_OWNER_MODULE_BASIC_INFO *modP;
        modP = (TCPIP_OWNER_MODULE_BASIC_INFO *) buf;
        obj[4] = ObjFromUnicode(modP->pModuleName);
        obj[5] = ObjFromUnicode(modP->pModulePath);
    } else {
        obj[4] = Tcl_NewStringObj("", -1);
        obj[5] = Tcl_NewStringObj("", -1);
    }

    return Tcl_NewListObj(6, obj);
}

Tcl_Obj *ObjFromMIB_UDP6ROW(Tcl_Interp *interp, MIB_UDP6ROW_OWNER_PID *row, int size)
{
    Tcl_Obj *obj[6];
    char     buf[sizeof(TCPIP_OWNER_MODULE_BASIC_INFO) + 4*MAX_PATH];
    DWORD    buf_sz;
    GetOwnerModuleFromUdp6Entry_t fn;

    obj[0] = ObjFromIPv6Addr(row->ucLocalAddr, row->dwLocalScopeId);
    obj[1] = Tcl_NewIntObj(ntohs((WORD)row->dwLocalPort));
    obj[2] = Tcl_NewIntObj(row->dwOwningPid);

    if (size < sizeof(MIB_UDP6ROW_OWNER_MODULE))
        return Tcl_NewListObj(3, obj);

    obj[3] = Tcl_NewWideIntObj(((MIB_UDP6ROW_OWNER_MODULE *)row)->liCreateTimestamp.QuadPart);

    fn = Twapi_GetProc_GetOwnerModuleFromUdp6Entry();
    buf_sz = sizeof(buf);
    if (fn && (*fn)((MIB_UDP6ROW_OWNER_MODULE *)row,
                    0, // TCPIP_OWNER_MODULE_BASIC_INFO enum
                    buf, &buf_sz) == NO_ERROR) {
        TCPIP_OWNER_MODULE_BASIC_INFO *modP;
        modP = (TCPIP_OWNER_MODULE_BASIC_INFO *) buf;
        obj[4] = ObjFromUnicode(modP->pModuleName);
        obj[5] = ObjFromUnicode(modP->pModulePath);
    } else {
        obj[4] = Tcl_NewStringObj("", -1);
        obj[5] = Tcl_NewStringObj("", -1);
    }

    return Tcl_NewListObj(6, obj);
}

Tcl_Obj *ObjFromMIB_TCPTABLE(Tcl_Interp *interp, MIB_TCPTABLE *tab)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < tab->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_TCPROW(interp, &(tab->table[i]), sizeof(MIB_TCPROW)));
    }

    return resultObj;
}

Tcl_Obj *ObjFromMIB_TCPTABLE_OWNER_PID(Tcl_Interp *interp, MIB_TCPTABLE_OWNER_PID *tab)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < tab->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_TCPROW(interp,  (MIB_TCPROW *) &(tab->table[i]), sizeof(MIB_TCPROW_OWNER_PID)));
    }

    return resultObj;
}

Tcl_Obj *ObjFromMIB_TCPTABLE_OWNER_MODULE(Tcl_Interp *interp, MIB_TCPTABLE_OWNER_MODULE *tab)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < tab->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_TCPROW(interp,  (MIB_TCPROW *) &(tab->table[i]), sizeof(MIB_TCPROW_OWNER_MODULE)));
    }

    return resultObj;
}

Tcl_Obj *ObjFromMIB_TCP6TABLE_OWNER_PID(Tcl_Interp *interp, MIB_TCP6TABLE_OWNER_PID *tab)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < tab->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_TCP6ROW(interp,  &(tab->table[i]), sizeof(MIB_TCP6ROW_OWNER_PID)));
    }

    return resultObj;
}

Tcl_Obj *ObjFromMIB_TCP6TABLE_OWNER_MODULE(Tcl_Interp *interp, MIB_TCP6TABLE_OWNER_MODULE *tab)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < tab->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_TCP6ROW(interp,  (MIB_TCP6ROW_OWNER_PID *) &(tab->table[i]), sizeof(MIB_TCP6ROW_OWNER_MODULE)));
    }

    return resultObj;
}

Tcl_Obj *ObjFromMIB_UDPTABLE(Tcl_Interp *interp, MIB_UDPTABLE *tab)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < tab->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_UDPROW(interp, &(tab->table[i]), sizeof(MIB_UDPROW)));
    }

    return resultObj;
}

Tcl_Obj *ObjFromMIB_UDPTABLE_OWNER_PID(Tcl_Interp *interp, MIB_UDPTABLE_OWNER_PID *tab)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < tab->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_UDPROW(interp, (MIB_UDPROW *) &(tab->table[i]), sizeof(MIB_UDPROW_OWNER_PID)));
    }

    return resultObj;
}


Tcl_Obj *ObjFromMIB_UDPTABLE_OWNER_MODULE(Tcl_Interp *interp, MIB_UDPTABLE_OWNER_MODULE *tab)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < tab->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_UDPROW(interp, (MIB_UDPROW *) &(tab->table[i]), sizeof(MIB_UDPTABLE_OWNER_MODULE)));
    }

    return resultObj;
}

Tcl_Obj *ObjFromMIB_UDP6TABLE_OWNER_PID(Tcl_Interp *interp, MIB_UDP6TABLE_OWNER_PID *tab)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < tab->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_UDP6ROW(interp, &(tab->table[i]), sizeof(MIB_UDP6ROW_OWNER_PID)));
    }

    return resultObj;
}


Tcl_Obj *ObjFromMIB_UDP6TABLE_OWNER_MODULE(Tcl_Interp *interp, MIB_UDP6TABLE_OWNER_MODULE *tab)
{
    DWORD i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (i=0; i < tab->dwNumEntries; ++i) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromMIB_UDP6ROW(interp, (MIB_UDP6ROW_OWNER_PID *) &(tab->table[i]), sizeof(MIB_UDP6TABLE_OWNER_MODULE)));
    }

    return resultObj;
}



Tcl_Obj *ObjFromTcpExTable(Tcl_Interp *interp, void *buf)
{
    /* It so happens the structure matches MIB_TCPTABLE_OWNER_PID */
    return ObjFromMIB_TCPTABLE_OWNER_PID(interp, (MIB_TCPTABLE_OWNER_PID *)buf);
}



Tcl_Obj *ObjFromUdpExTable(Tcl_Interp *interp, void *buf)
{
    /* It so happens the structure matches MIB_UDPTABLE_OWNER_PID */
    return ObjFromMIB_UDPTABLE_OWNER_PID(interp, (MIB_UDPTABLE_OWNER_PID *)buf);
}


int Twapi_FormatExtendedTcpTable(
    Tcl_Interp *interp,
    void *buf,
    int family,
    int table_class
    )
{
    Tcl_Obj *obj;

    if (family != AF_INET && family != AF_INET6)
        goto error_return;

    // The constants below are TCP_TABLE_CLASS enumerations. These
    // is not defined in the Win2k3 SDK we are using so use integer constants
    switch (table_class) {
    case 0: // TCP_TABLE_BASIC_LISTENER
    case 1: // TCP_TABLE_BASIC_CONNECTIONS
    case 2: // TCP_TABLE_BASIC_ALL
        if (family == AF_INET)
            obj = ObjFromMIB_TCPTABLE(interp, (MIB_TCPTABLE *)buf);
        else
            goto error_return;  /* Not supported for IP v6 */
        break;

    case 3: // TCP_TABLE_OWNER_PID_LISTENER
    case 4: // TCP_TABLE_OWNER_PID_CONNECTIONS
    case 5: // TCP_TABLE_OWNER_PID_ALL
        if (family == AF_INET)
            obj = ObjFromMIB_TCPTABLE_OWNER_PID(interp, (MIB_TCPTABLE_OWNER_PID *)buf);
        else
            obj = ObjFromMIB_TCP6TABLE_OWNER_PID(interp, (MIB_TCP6TABLE_OWNER_PID *)buf);
        break;

    case 6: // TCP_TABLE_OWNER_MODULE_LISTENER
    case 7: // TCP_TABLE_OWNER_MODULE_CONNECTIONS
    case 8: // TCP_TABLE_OWNER_MODULE_ALL
        if (family == AF_INET)
            obj = ObjFromMIB_TCPTABLE_OWNER_MODULE(interp, (MIB_TCPTABLE_OWNER_MODULE *)buf);
        else
            obj = ObjFromMIB_TCP6TABLE_OWNER_MODULE(interp, (MIB_TCP6TABLE_OWNER_MODULE *)buf);
        break;

    default:
        goto error_return;
    }

    Tcl_SetObjResult(interp, obj);
    return TCL_OK;

error_return:
    return Twapi_AppendSystemError(interp, ERROR_INVALID_PARAMETER);
}

int Twapi_FormatExtendedUdpTable(
    Tcl_Interp *interp,
    void *buf,
    int family,
    int table_class
    )
{
    Tcl_Obj *obj;

    if (family != AF_INET && family != AF_INET6)
        goto error_return;

    // The constants below are TCP_TABLE_CLASS enumerations. These
    // are not defined in the SDK we are using so use integer constants
    switch (table_class) {
    case 0: // UDP_TABLE_BASIC
        if (family != AF_INET)
            goto error_return;

        obj = ObjFromMIB_UDPTABLE(interp, (MIB_UDPTABLE *) buf);
        break;

    case 1: // UDP_TABLE_OWNER_PID
        if (family == AF_INET)
            obj = ObjFromMIB_UDPTABLE_OWNER_PID(interp, (MIB_UDPTABLE_OWNER_PID *) buf);
        else
            obj = ObjFromMIB_UDP6TABLE_OWNER_PID(interp, (MIB_UDP6TABLE_OWNER_PID *) buf);
        break;

    case 2: // UDP_TABLE_OWNER_MODULE
        if (family == AF_INET)
            obj = ObjFromMIB_UDPTABLE_OWNER_MODULE(interp, (MIB_UDPTABLE_OWNER_MODULE *) buf);
        else
            obj = ObjFromMIB_UDP6TABLE_OWNER_MODULE(interp, (MIB_UDP6TABLE_OWNER_MODULE *) buf);
        break;

    default:
        goto error_return;
    }

    Tcl_SetObjResult(interp, obj);
    return TCL_OK;

error_return:
    return Twapi_AppendSystemError(interp, ERROR_INVALID_PARAMETER);
}


Tcl_Obj *ObjFromIP_ADAPTER_INFO(Tcl_Interp *interp, IP_ADAPTER_INFO *ainfoP)
{
    Tcl_Obj        *objv[14];

    objv[0] = Tcl_NewStringObj(ainfoP->AdapterName, -1);
    objv[1] = Tcl_NewStringObj(ainfoP->Description, -1);
    objv[2] = Tcl_NewByteArrayObj(ainfoP->Address, ainfoP->AddressLength);
    objv[3] = Tcl_NewIntObj(ainfoP->Index);
    objv[4] = Tcl_NewIntObj(ainfoP->Type);
    objv[5] = Tcl_NewIntObj(ainfoP->DhcpEnabled);
    objv[6] = ObjFromIP_ADDR_STRING(interp, &ainfoP->IpAddressList);
    objv[7] = Tcl_NewStringObj(ainfoP->GatewayList.IpAddress.String, -1);
    objv[8] = Tcl_NewStringObj(ainfoP->DhcpServer.IpAddress.String, -1);
    objv[9] = Tcl_NewIntObj(ainfoP->HaveWins);
    objv[10] = Tcl_NewStringObj(ainfoP->PrimaryWinsServer.IpAddress.String, -1);
    objv[11] = Tcl_NewStringObj(ainfoP->SecondaryWinsServer.IpAddress.String, -1);
    objv[12] = Tcl_NewWideIntObj(ainfoP->LeaseObtained);
    objv[13] = Tcl_NewWideIntObj(ainfoP->LeaseExpires);

    /* Attach to list of adapter data */
    return Tcl_NewListObj(14, objv);

}

Tcl_Obj *ObjFromIP_ADAPTER_INFO_table(Tcl_Interp *interp, IP_ADAPTER_INFO *ainfoP)
{
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    while (ainfoP) {
        Tcl_ListObjAppendElement(interp, resultObj,
                                 ObjFromIP_ADAPTER_INFO(interp, ainfoP));
        ainfoP = ainfoP->Next;
    }

    return resultObj;
}


/* Helper function - common to all table retrieval functions */
static int TwapiIpConfigTableHelper(TwapiInterpContext *ticP, DWORD (FAR WINAPI *fn)(), Tcl_Obj *(*objbuilder)(Tcl_Interp *, ...), BOOL sortable, BOOL sort)
{
    int error;
    void *bufP;
    ULONG bufsz;
    int  tries;

    if (fn == NULL) {
        return Twapi_AppendSystemError(ticP->interp, ERROR_PROC_NOT_FOUND);
    }

    /*
     * Keep looping as long as we are told we need a bigger buffer.
     * For robustness, we set a limit on number of tries. Note required
     * size can keep changing so we try multiple times.
     */
    bufsz = 4000;
    bufP = MemLifoPushFrame(&ticP->memlifo, bufsz, &bufsz);
    for (tries=0; tries < 10 ; ++tries) {
        if (sortable)
            error = (*fn)(bufP, &bufsz, sort);
        else
            error = (*fn)(bufP, &bufsz);
        if (error != ERROR_INSUFFICIENT_BUFFER &&
            error != ERROR_BUFFER_OVERFLOW) {
            /* Either success or error unrelated to buffer size */
            break;
        }
        
        /* Retry with bigger buffer */
        /* bufsz contains required size as returned by the functions */
        MemLifoPopFrame(&ticP->memlifo);
        bufP = MemLifoPushFrame(&ticP->memlifo, bufsz, &bufsz);
    }

    if (error == NO_ERROR) {
        Tcl_SetObjResult(ticP->interp, (*objbuilder)(ticP->interp, bufP));
    } else {
        Twapi_AppendSystemError(ticP->interp, error);
    }

    MemLifoPopFrame(&ticP->memlifo);

    return error == NO_ERROR ? TCL_OK : TCL_ERROR;
}


int Twapi_GetNetworkParams(TwapiInterpContext *ticP)
{
    FIXED_INFO *netinfoP;
    ULONG netinfo_size;
    DWORD error;
    Tcl_Obj *objv[8];

    /* TBD - maybe allocate bigger space to start with ? */
    netinfoP = MemLifoPushFrame(&ticP->memlifo, sizeof(*netinfoP), &netinfo_size);
    error = GetNetworkParams(netinfoP, &netinfo_size);
    if (error == ERROR_BUFFER_OVERFLOW) {
        /* Allocate a bigger buffer of the required size. */
        MemLifoPopFrame(&ticP->memlifo);
        netinfoP = MemLifoPushFrame(&ticP->memlifo, netinfo_size, NULL);
        error = GetNetworkParams(netinfoP, &netinfo_size);
    }

    if (error == ERROR_SUCCESS) {
        objv[0] = Tcl_NewStringObj(netinfoP->HostName, -1);
        objv[1] = Tcl_NewStringObj(netinfoP->DomainName, -1);
        objv[2] = ObjFromIP_ADDR_STRINGAddress(ticP->interp,
                                               &netinfoP->DnsServerList);
        objv[3] = Tcl_NewIntObj(netinfoP->NodeType);
        objv[4] = Tcl_NewStringObj(netinfoP->ScopeId, -1);
        objv[5] = Tcl_NewIntObj(netinfoP->EnableRouting);
        objv[6] = Tcl_NewIntObj(netinfoP->EnableProxy);
        objv[7] = Tcl_NewIntObj(netinfoP->EnableDns);
        Tcl_SetObjResult(ticP->interp, Tcl_NewListObj(8, objv));
    } else {
        Twapi_AppendSystemError(ticP->interp, error);
    }

    MemLifoPopFrame(&ticP->memlifo);

    return error == ERROR_SUCCESS ? TCL_OK : TCL_ERROR;
}


/* TBD - obsoleted by GetAdaptersAddresses ? */
int Twapi_GetAdaptersInfo(TwapiInterpContext *ticP)
{
    return TwapiIpConfigTableHelper(
        ticP,
        GetAdaptersInfo,
        ObjFromIP_ADAPTER_INFO_table,
        0,
        0
        );
}


int Twapi_GetAdaptersAddresses(TwapiInterpContext *ticP, ULONG family,
                               ULONG flags, void *reserved)
{
    IP_ADAPTER_ADDRESSES *iaaP;
    ULONG bufsz;
    DWORD error;
    int   tries;
    Tcl_Obj *resultObj;

    /*
     * Keep looping as long as we are told we need a bigger buffer. For
     * robustness, we set a limit on number of tries. Note required size
     * can keep changing (unlikely, but possible ) so we try multiple times.
     * TBD - check for appropriate initial size
     */
    bufsz = 1000;
    iaaP = (IP_ADAPTER_ADDRESSES *) MemLifoPushFrame(&ticP->memlifo,
                                                     bufsz, &bufsz);
    for (tries=0; tries < 10 ; ++tries) {
        error = GetAdaptersAddresses(family, flags, NULL, iaaP, &bufsz);
        if (error != ERROR_BUFFER_OVERFLOW) {
            /* Either success or error unrelated to buffer size */
            break;
        }
        
        /* realloc - bufsz contains required size as returned by the functions */
        MemLifoPopFrame(&ticP->memlifo);
        iaaP = (IP_ADAPTER_ADDRESSES *) MemLifoPushFrame(&ticP->memlifo,
                                                         bufsz, &bufsz);
    }

    if (error != ERROR_SUCCESS) {
        Twapi_AppendSystemError(ticP->interp, error);
    } else {
        resultObj = Tcl_NewListObj(0, NULL);
        while (iaaP) {
            Tcl_ListObjAppendElement(NULL, resultObj, ObjFromIP_ADAPTER_ADDRESSES(iaaP));
            iaaP = iaaP->Next;
        }
        Tcl_SetObjResult(ticP->interp, resultObj);
    }

    MemLifoPopFrame(&ticP->memlifo);

    return error == ERROR_SUCCESS ? TCL_OK : TCL_ERROR;
}



int Twapi_GetPerAdapterInfo(TwapiInterpContext *ticP, int adapter_index)
{
    IP_PER_ADAPTER_INFO *ainfoP;
    ULONG                ainfo_size;
    DWORD                error;
    Tcl_Obj             *objv[3];

    /* Make first allocation assuming two ip addresses */
    ainfoP = MemLifoPushFrame(&ticP->memlifo,
                              sizeof(*ainfoP)+2*sizeof(IP_ADDR_STRING),
                              &ainfo_size);
    error = GetPerAdapterInfo(adapter_index, ainfoP, &ainfo_size);
    if (error == ERROR_BUFFER_OVERFLOW) {
        /* Retry with indicated size */
        MemLifoPopFrame(&ticP->memlifo);
        ainfoP = MemLifoPushFrame(&ticP->memlifo, ainfo_size, NULL);
        error = GetPerAdapterInfo(adapter_index, ainfoP, &ainfo_size);
    }

    if (error == ERROR_SUCCESS) {
        objv[0] = Tcl_NewIntObj(ainfoP->AutoconfigEnabled);
        objv[1] = Tcl_NewIntObj(ainfoP->AutoconfigActive);
        objv[2] = ObjFromIP_ADDR_STRINGAddress(ticP->interp, &ainfoP->DnsServerList);
        Tcl_SetObjResult(ticP->interp, Tcl_NewListObj(3, objv));
    } else
        Twapi_AppendSystemError(ticP->interp, error);

    MemLifoPopFrame(&ticP->memlifo);

    return error == ERROR_SUCCESS ? TCL_OK : TCL_ERROR;
}




int Twapi_GetInterfaceInfo(TwapiInterpContext *ticP)
{
    return TwapiIpConfigTableHelper(
        ticP,
        GetInterfaceInfo,
        ObjFromIP_INTERFACE_INFO,
        0,
        0
        );
}

int Twapi_GetIfEntry(Tcl_Interp *interp, int if_index)
{
    int error;
    MIB_IFROW ifr;

    ifr.dwIndex = if_index;
    error = GetIfEntry(&ifr);
    if (error) {
        return Twapi_AppendSystemError(interp, error);
    }
    Tcl_SetObjResult(interp, ObjFromMIB_IFROW(interp, &ifr));
    return TCL_OK;
}

int Twapi_GetIfTable(TwapiInterpContext *ticP, int sort)
{
    return TwapiIpConfigTableHelper(
        ticP,
        GetIfTable,
        ObjFromMIB_IFTABLE,
        1,
        sort
        );
}

int Twapi_GetIpAddrTable(TwapiInterpContext *ticP, int sort)
{
    return TwapiIpConfigTableHelper(
        ticP,
        GetIpAddrTable,
        ObjFromMIB_IPADDRTABLE,
        1,
        sort
        );
}


int Twapi_GetIpNetTable(TwapiInterpContext *ticP, int sort)
{
    return TwapiIpConfigTableHelper(
        ticP,
        GetIpNetTable,
        ObjFromMIB_IPNETTABLE,
        1,
        sort
        );
}

int Twapi_GetIpForwardTable(TwapiInterpContext *ticP, int sort)
{
    return TwapiIpConfigTableHelper(
        ticP,
        GetIpForwardTable,
        ObjFromMIB_IPFORWARDTABLE,
        1,
        sort
        );
}

typedef DWORD (WINAPI *GetExtendedTcpTable_t)(PVOID, PDWORD, BOOL, ULONG, ULONG, ULONG);
MAKE_DYNLOAD_FUNC(GetExtendedTcpTable, iphlpapi, GetExtendedTcpTable_t)
int Twapi_GetExtendedTcpTable(
    Tcl_Interp *interp,
    void *buf,
    DWORD buf_sz,
    BOOL sorted,
    ULONG family,
    int   table_class
    )
{
    int error;
    GetExtendedTcpTable_t fn = Twapi_GetProc_GetExtendedTcpTable();
    if (fn == NULL) {
        return Twapi_AppendSystemError(interp, ERROR_PROC_NOT_FOUND);
    }

    /* IPv6 Windows (at least on XP) has a bug in that it incorrectly
       calculates buffer size and overwrites the passed buffer when
       table_class is one of the MODULE values
       6: TCP_TABLE_OWNER_MODULE_LISTENER
       7: TCP_TABLE_OWNER_MODULE_CONNECTIONS
       8: TCP_TABLE_OWNER_MODULE_ALL
       Google for details
    */
    if (family == AF_INET6 && table_class > 5) {
        return Twapi_AppendSystemError(interp, ERROR_INVALID_PARAMETER);
    }
       
    error = (*fn)(buf, &buf_sz, sorted, family, table_class, 0);
    if (error == NO_ERROR || error == ERROR_INSUFFICIENT_BUFFER) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(buf_sz));
        return TCL_OK;
    } else {
        return Twapi_AppendSystemError(interp, error);
    }
}


typedef DWORD (WINAPI *GetExtendedUdpTable_t)(PVOID, PDWORD, BOOL, ULONG, ULONG, ULONG);
MAKE_DYNLOAD_FUNC(GetExtendedUdpTable, iphlpapi, GetExtendedUdpTable_t)
int Twapi_GetExtendedUdpTable(
    Tcl_Interp *interp,
    void *buf,
    DWORD buf_sz,
    BOOL sorted,
    ULONG family,
    int   table_class
    )
{
    int error;
    GetExtendedUdpTable_t fn = Twapi_GetProc_GetExtendedUdpTable();
    if (fn == NULL) {
        return Twapi_AppendSystemError(interp, ERROR_PROC_NOT_FOUND);
    }

    /* See note in Twapi_GetExtendedTcpTable function above */
    if (family == AF_INET6 && table_class > 5) {
        return Twapi_AppendSystemError(interp, ERROR_INVALID_PARAMETER);
    }
    error = (*fn)(buf, &buf_sz, sorted, family, table_class, 0);
    if (error == NO_ERROR || error == ERROR_INSUFFICIENT_BUFFER) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(buf_sz));
        return TCL_OK;
    } else {
        return Twapi_AppendSystemError(interp, error);
    }
}


typedef DWORD (WINAPI *AllocateAndGetTcpExTableFromStack_t)(PVOID *,BOOL,HANDLE,DWORD, DWORD);
MAKE_DYNLOAD_FUNC(AllocateAndGetTcpExTableFromStack, iphlpapi, AllocateAndGetTcpExTableFromStack_t)
int Twapi_AllocateAndGetTcpExTableFromStack(
    TwapiInterpContext *ticP,
    BOOL sorted,
    DWORD flags
)
{
    int error;
    AllocateAndGetTcpExTableFromStack_t fn = Twapi_GetProc_AllocateAndGetTcpExTableFromStack();

    if (fn) {
        void *buf = NULL;

        /* 2 -> AF_INET (IP v4) */
        error = (*fn)(&buf, sorted, GetProcessHeap(), flags, 2);
        if (error)
            return Twapi_AppendSystemError(ticP->interp, error);

        Tcl_SetObjResult(ticP->interp, ObjFromTcpExTable(ticP->interp, buf));
        HeapFree(GetProcessHeap(), 0, buf);
        return TCL_OK;
    } else {
        DWORD sz;
        MIB_TCPTABLE *tab = NULL;
        int i;

        /* TBD - can remove this code since Win2K no longer supported ? */

        /*
         * First get the required  buffer size.
         * Do this in a loop since size might change with an upper limit
         * on number of iterations.
         * We do not use MemLifo because allocations are likely quite large
         * so little benefit.
         */
        for (tab = NULL, sz = 0, i = 0; i < 10; ++i) {
            error = GetTcpTable(tab, &sz, sorted);
            if (error != ERROR_INSUFFICIENT_BUFFER)
                break;
            /* Retry with larger buffer */
            if (tab)
                TwapiFree(tab);
            tab = (MIB_TCPTABLE *) TwapiAlloc(sz);
        }
        
        if (error == ERROR_SUCCESS)
            Tcl_SetObjResult(ticP->interp, ObjFromMIB_TCPTABLE(ticP->interp, tab));
        else
            Twapi_AppendSystemError(ticP->interp, error);
        if (tab)
            TwapiFree(tab);
        return error == ERROR_SUCCESS ? TCL_OK : TCL_ERROR;
    }
}


typedef DWORD (WINAPI *AllocateAndGetUdpExTableFromStack_t)(PVOID *,BOOL,HANDLE,DWORD, DWORD);
MAKE_DYNLOAD_FUNC(AllocateAndGetUdpExTableFromStack, iphlpapi, AllocateAndGetUdpExTableFromStack_t)
int Twapi_AllocateAndGetUdpExTableFromStack(
    TwapiInterpContext *ticP,
    BOOL sorted,
    DWORD flags
)
{
    int error;
    AllocateAndGetUdpExTableFromStack_t fn = Twapi_GetProc_AllocateAndGetUdpExTableFromStack();

    if (fn) {
        void *buf = NULL;

        /* 2 -> AF_INET (IP v4) */
        error = (*fn)(&buf, sorted, GetProcessHeap(), flags, 2);
        if (error)
            return Twapi_AppendSystemError(ticP->interp, error);

        Tcl_SetObjResult(ticP->interp, ObjFromUdpExTable(ticP->interp, buf));
        HeapFree(GetProcessHeap(), 0, buf);
        return TCL_OK;
    } else {
        DWORD sz;
        MIB_UDPTABLE *tab = NULL;
        int i;

        /* TBD - can remove this code since Win2K no longer supported ? */

        /*
         * First get the required  buffer size.
         * Do this in a loop since size might change with an upper limit
         * on number of iterations.
         * We do not use MemLifo because allocations are likely quite large
         * so little benefit.
         */
        for (tab = NULL, sz = 0, i = 0; i < 10; ++i) {
            error = GetUdpTable(tab, &sz, sorted);
            if (error != ERROR_INSUFFICIENT_BUFFER)
                break;
            /* Retry with larger buffer */
            if (tab)
                TwapiFree(tab);
            tab = (MIB_UDPTABLE *) TwapiAlloc(sz);
        }
        
        if (error == ERROR_SUCCESS)
            Tcl_SetObjResult(ticP->interp, ObjFromMIB_UDPTABLE(ticP->interp, tab));
        else
            Twapi_AppendSystemError(ticP->interp, error);
        if (tab)
            TwapiFree(tab);
        return error == ERROR_SUCCESS ? TCL_OK : TCL_ERROR;
    }
}


int Twapi_GetNameInfo(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int status;
    SOCKADDR_STORAGE ss;
    char hostname[NI_MAXHOST];
    char portname[NI_MAXSERV];
    Tcl_Obj *objs[2];
    int flags;

    if (TwapiGetArgs(interp, objc, objv,
                     ARGSKIP, GETINT(flags),
                     ARGEND) != TCL_OK)
        return TCL_ERROR;

    if (ObjToSOCKADDR_STORAGE(interp, objv[0], &ss) != TCL_OK)
        return TCL_ERROR;

    status = getnameinfo((SOCKADDR *)&ss,
                         ss.ss_family == AF_INET6 ? sizeof(SOCKADDR_IN6) : sizeof(SOCKADDR_IN),
                         hostname, sizeof(hostname)/sizeof(hostname[0]),
                         portname, sizeof(portname)/sizeof(portname[0]),
                         flags);
    if (status != 0)
        return Twapi_AppendSystemError(interp, status);

    objs[0] = Tcl_NewStringObj(hostname, -1);
    objs[1] = Tcl_NewStringObj(portname, -1);

    Tcl_SetObjResult(interp, Tcl_NewListObj(2,objs));
    return TCL_OK;
}

Tcl_Obj *TwapiCollectAddrInfo(struct addrinfo *addrP, int family)
{
    Tcl_Obj *resultObj;

    resultObj = Tcl_NewListObj(0, NULL);
    while (addrP) {
        Tcl_Obj *objP;
        SOCKADDR *saddrP = addrP->ai_addr;

        if (family == AF_UNSPEC || family == addrP->ai_family) {
            if ((addrP->ai_family == PF_INET &&
                 addrP->ai_addrlen == sizeof(SOCKADDR_IN) &&
                 saddrP && saddrP->sa_family == AF_INET)
                ||
                (addrP->ai_family == PF_INET6 &&
                 addrP->ai_addrlen == sizeof(SOCKADDR_IN6) &&
                 saddrP && saddrP->sa_family == AF_INET6)) {
                objP = ObjFromSOCKADDR(saddrP);
                if (objP) {
                    Tcl_ListObjAppendElement(NULL, resultObj, objP);
                }
            }
        }
        addrP = addrP->ai_next;
    }
    return resultObj;
}

int Twapi_GetAddrInfo(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    const char *hostname;
    const char *svcname;
    int status;
    struct addrinfo hints;
    struct addrinfo *addrP;

    TwapiZeroMemory(&hints, sizeof(hints));
    if (TwapiGetArgs(interp, objc, objv,
                     GETASTR(hostname), GETASTR(svcname),
                     ARGUSEDEFAULT,
                     GETINT(hints.ai_family),
                     GETINT(hints.ai_protocol),
                     GETINT(hints.ai_socktype),
                     ARGEND) != TCL_OK)
        return TCL_ERROR;

    status = getaddrinfo(hostname, svcname, &hints, &addrP);
    if (status != 0) {
        return Twapi_AppendSystemError(interp, status);
    }

    Tcl_SetObjResult(interp, TwapiCollectAddrInfo(addrP, hints.ai_family));
    if (addrP)
        freeaddrinfo(addrP);

    return TCL_OK;
}

int Twapi_GetBestRoute(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    MIB_IPFORWARDROW route;
    int error;
    DWORD dest, src;

    if (TwapiGetArgs(ticP->interp, objc, objv,
                     GETVAR(dest, IPAddrObjToDWORD),
                     GETVAR(src, IPAddrObjToDWORD),
                     ARGEND) != TCL_OK) {
        return TCL_ERROR;
    }

    error = GetBestRoute(dest, src, &route);
    if (error == NO_ERROR) {
        Tcl_SetObjResult(ticP->interp, ObjFromMIB_IPFORWARDROW(ticP->interp, &route));
        return TCL_OK;
    } else {
        return Twapi_AppendSystemError(ticP->interp, error);
    }
}

int Twapi_GetBestInterface(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    GetBestInterfaceEx_t fn;
    int result;
    DWORD ifindex;
    DWORD ipaddr;
    Tcl_Obj *objP;

    if (objc != 1)
        return TwapiReturnError(ticP->interp, TWAPI_BAD_ARG_COUNT);

    fn = Twapi_GetProc_GetBestInterfaceEx();
    if (fn) {
        SOCKADDR_STORAGE ss;

        /* We only have the address, ObjToSOCKADDR_STORAGE expects
         * it as first element of a list with optional second param
         */
        objP = Tcl_NewListObj(1, objv);
        Tcl_IncrRefCount(objP);
        result = ObjToSOCKADDR_STORAGE(ticP->interp, objP, &ss);
        Tcl_DecrRefCount(objP);
        if (result != TCL_OK)
            return result;
        result = (*fn)((struct sockaddr *)&ss, &ifindex);
        if (result)
            return Twapi_AppendSystemError(ticP->interp, result);
    } else {
        /* GetBestInterfaceEx not available before XP SP2 */
        if (IPAddrObjToDWORD(ticP->interp, objv[0], &ipaddr) == TCL_ERROR)
            return TCL_ERROR;
        result = GetBestInterface(ipaddr, &ifindex);
        if (result)
            return Twapi_AppendSystemError(ticP->interp, result);
    }

    Tcl_SetObjResult(ticP->interp, Tcl_NewLongObj(ifindex));
    return TCL_OK;
}



/* Called from the Tcl event loop with the result of a hostname lookup */
static int TwapiHostnameEventProc(Tcl_Event *tclevP, int flags)
{
    TwapiHostnameEvent *theP = (TwapiHostnameEvent *) tclevP;

    if (theP->ticP->interp != NULL &&
        ! Tcl_InterpDeleted(theP->ticP->interp)) {
        /* Invoke the script */
        Tcl_Interp *interp = theP->ticP->interp;
        Tcl_Obj *objP = Tcl_NewListObj(0, NULL);

        Tcl_ListObjAppendElement(
            interp, objP, STRING_LITERAL_OBJ(TWAPI_TCL_NAMESPACE "::_hostname_resolve_handler"));
        Tcl_ListObjAppendElement(interp, objP, ObjFromTwapiId(theP->id));
        if (theP->status == ERROR_SUCCESS) {
            /* Success */
            Tcl_ListObjAppendElement(interp, objP, STRING_LITERAL_OBJ("success"));
            Tcl_ListObjAppendElement(interp, objP, TwapiCollectAddrInfo(theP->addrinfolist, theP->family));
        } else {
            /* Failure */
            Tcl_ListObjAppendElement(interp, objP, STRING_LITERAL_OBJ("fail"));
            Tcl_ListObjAppendElement(interp, objP,
                                     Tcl_NewLongObj(theP->status));
        }
        /* Invoke the script */
        Tcl_IncrRefCount(objP);
        (void) Tcl_EvalObjEx(interp, objP, TCL_EVAL_DIRECT|TCL_EVAL_GLOBAL);
        Tcl_DecrRefCount(objP);
        /* TBD - check for error and add to background ? */
    }

    /* Done with the interp context */
    TwapiInterpContextUnref(theP->ticP, 1);

    /* Assumes we can free this from different thread than allocated it ! */
    if (theP->addrinfolist)
        freeaddrinfo(theP->addrinfolist);

    return 1;                   /* So Tcl removes from queue */
}


/* Called from the Win2000 thread pool */
static DWORD WINAPI TwapiHostnameHandler(TwapiHostnameEvent *theP)
{
    struct addrinfo hints;

    TwapiZeroMemory(&hints, sizeof(hints));
    hints.ai_family = theP->family;

    theP->tcl_ev.proc = TwapiHostnameEventProc;
    theP->status = getaddrinfo(theP->name, "0", &hints, &theP->addrinfolist);
    TwapiEnqueueTclEvent(theP->ticP, &theP->tcl_ev);
    return 0;               /* Return value does not matter */
}


int Twapi_ResolveHostnameAsync(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    TwapiId id;
    char *name;
    int   len;
    TwapiHostnameEvent *theP;
    DWORD winerr;
    int family;

    ERROR_IF_UNTHREADED(ticP->interp);

    if (TwapiGetArgs(ticP->interp, objc, objv,
                     GETASTRN(name, len), ARGUSEDEFAULT, GETINT(family),
                     ARGEND) != TCL_OK)
        return TCL_ERROR;

    id =  TWAPI_NEWID(ticP);
    /* Allocate the callback context, must be allocated via ckalloc
     * as it will be passed to Tcl_QueueEvent.
     */
    theP = (TwapiHostnameEvent *) ckalloc(SIZE_TwapiHostnameEvent(len));
    theP->tcl_ev.proc = NULL;
    theP->tcl_ev.nextPtr = NULL;
    theP->id = id;
    theP->status = ERROR_SUCCESS;
    theP->ticP = ticP;
    TwapiInterpContextRef(ticP, 1); /* So it does not go away */
    theP->addrinfolist = NULL;
    theP->family = family;
    CopyMemory(theP->name, name, len+1);

    if (QueueUserWorkItem(TwapiHostnameHandler, theP, WT_EXECUTEDEFAULT)) {
        Tcl_SetObjResult(ticP->interp, ObjFromTwapiId(id));
        return TCL_OK;
    }

    winerr = GetLastError();    /* Remember the error */

    TwapiInterpContextUnref(ticP, 1); /* Undo above ref */
    Tcl_Free((char*) theP);
    return Twapi_AppendSystemError(ticP->interp, winerr);
}


/* Called from the Tcl event loop with the result of a address lookup */
static int TwapiAddressEventProc(Tcl_Event *tclevP, int flags)
{
    TwapiHostnameEvent *theP = (TwapiHostnameEvent *) tclevP;

    if (theP->ticP->interp != NULL &&
        ! Tcl_InterpDeleted(theP->ticP->interp)) {
        /* Invoke the script */
        Tcl_Interp *interp = theP->ticP->interp;
        Tcl_Obj *objP = Tcl_NewListObj(0, NULL);

        Tcl_ListObjAppendElement(
            interp, objP, STRING_LITERAL_OBJ(TWAPI_TCL_NAMESPACE "::_address_resolve_handler"));
        Tcl_ListObjAppendElement(interp, objP, ObjFromTwapiId(theP->id));
        if (theP->status == ERROR_SUCCESS) {
            /* Success. Note theP->hostname may still be NULL */
            Tcl_ListObjAppendElement(interp, objP, STRING_LITERAL_OBJ("success"));
            Tcl_ListObjAppendElement(
                interp, objP,
                Tcl_NewStringObj((theP->hostname ? theP->hostname : ""), -1));
        } else {
            /* Failure */
            Tcl_ListObjAppendElement(interp, objP, STRING_LITERAL_OBJ("fail"));
            Tcl_ListObjAppendElement(interp, objP,
                                     Tcl_NewLongObj(theP->status));
        }
        /* Invoke the script */
        /* Do we need TclSave/RestoreResult ? */
        Tcl_IncrRefCount(objP);
        (void) Tcl_EvalObjEx(interp, objP, TCL_EVAL_DIRECT|TCL_EVAL_GLOBAL);
        Tcl_DecrRefCount(objP);
        /* TBD - check for error and add to background ? */
    }
    
    /* Done with the interp context */
    TwapiInterpContextUnref(theP->ticP, 1);
    if (theP->hostname)
        TwapiFree(theP->hostname);
    
    return 1;                   /* So Tcl removes from queue */
}


/* Called from the Win2000 thread pool */
static DWORD WINAPI TwapiAddressHandler(TwapiHostnameEvent *theP)
{
    SOCKADDR_STORAGE ss;
    char hostname[NI_MAXHOST];
    char portname[NI_MAXSERV];
    int family;

    theP->tcl_ev.proc = TwapiAddressEventProc;
    family = TwapiStringToSOCKADDR_STORAGE(theP->name, &ss, theP->family);
    if (family == AF_UNSPEC) {
        // Fail, invalid address string
        theP->status = 10022;         /* WSAINVAL error code */
    } else {    
        theP->status = getnameinfo((struct sockaddr *)&ss,
                                   ss.ss_family == AF_INET6 ? sizeof(SOCKADDR_IN6) : sizeof(SOCKADDR_IN),
                                   hostname, sizeof(hostname)/sizeof(hostname[0]),
                                   portname, sizeof(portname)/sizeof(portname[0]),
                                   NI_NUMERICSERV);
    }
    if (theP->status == 0) {
        /* If the function just returned back the address, then there
           was really no name found so return empty string (NULL) */
        theP->hostname = NULL;
        if (lstrcmpA(theP->name, hostname)) {
            /* Really do have a name */
            theP->hostname = TwapiAllocAString(hostname, -1);
        }
    }

    TwapiEnqueueTclEvent(theP->ticP, &theP->tcl_ev);
    return 0;                   /* Return value ignored anyways */
}

int Twapi_ResolveAddressAsync(TwapiInterpContext *ticP, int objc, Tcl_Obj *CONST objv[])
{
    TwapiId id;
    char *addrstr;
    int   len;
    TwapiHostnameEvent *theP;
    DWORD winerr;
    int family;

    ERROR_IF_UNTHREADED(ticP->interp);

    if (TwapiGetArgs(ticP->interp, objc, objv,
                     GETASTRN(addrstr, len), ARGUSEDEFAULT, GETINT(family),
                     ARGEND) != TCL_OK)
        return TCL_ERROR;

    id =  TWAPI_NEWID(ticP);

    /* Allocate the callback context, must be allocated via ckalloc
     * as it will be passed to Tcl_QueueEvent.
     */
    theP = (TwapiHostnameEvent *) ckalloc(SIZE_TwapiHostnameEvent(len));
    theP->tcl_ev.proc = NULL;
    theP->tcl_ev.nextPtr = NULL;
    theP->id = id;
    theP->status = ERROR_SUCCESS;
    theP->ticP = ticP;
    TwapiInterpContextRef(ticP, 1); /* So it does not go away */
    theP->hostname = NULL;
    theP->family = family;

    /* We do not syntactically validate address string here. All failures
       are delivered asynchronously */
    CopyMemory(theP->name, addrstr, len+1);

    if (QueueUserWorkItem(TwapiAddressHandler, theP, WT_EXECUTEDEFAULT)) {
        Tcl_SetObjResult(ticP->interp, ObjFromTwapiId(id));
        return TCL_OK;
    }
    winerr = GetLastError();    /* Remember the error */

    TwapiInterpContextUnref(ticP, 1); /* Undo above ref */
    Tcl_Free((char*) theP);
    return Twapi_AppendSystemError(ticP->interp, winerr);
}


static int Twapi_NetworkCallObjCmd(TwapiInterpContext *ticP, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    TwapiResult result;
    int func;
    union {
        MIB_TCPROW tcprow;
        SOCKADDR_STORAGE ss;
    } u;
    DWORD dw, dw2, dw3, dw4;
    LPWSTR s;
    LPVOID pv;

    if (objc < 2)
        return TwapiReturnError(interp, TWAPI_BAD_ARG_COUNT);
    CHECK_INTEGER_OBJ(interp, func, objv[1]);

    result.type = TRT_BADFUNCTIONCODE;

    if (func < 100) {
        /* Functions taking no arguments */
        if (objc != 2)
            return TwapiReturnError(interp, TWAPI_BAD_ARG_COUNT);

        switch (func) {
        case 1:
            return Twapi_GetNetworkParams(ticP);
        case 2:
            return Twapi_GetAdaptersInfo(ticP);
        case 3:
            return Twapi_GetInterfaceInfo(ticP);
        case 4:
            result.type = GetNumberOfInterfaces(&result.value.ival) ? TRT_GETLASTERROR : TRT_DWORD;
            break;

        }
    } else if (func < 300) {
        if (objc != 3)
            return TwapiReturnError(interp, TWAPI_BAD_ARG_COUNT);
        if (func == 101) {
            if (ObjToMIB_TCPROW(interp, objv[2], &u.tcprow) != TCL_OK)
                return TCL_ERROR;
            result.type = TRT_EXCEPTION_ON_ERROR;
            result.value.ival = SetTcpEntry(&u.tcprow);
        } else if (func < 250) {
            CHECK_INTEGER_OBJ(interp, dw, objv[2]);
            switch (func) {
            case 201:
                return Twapi_GetPerAdapterInfo(ticP, dw);
            case 202:
                return Twapi_GetIfEntry(interp, dw);
            case 203:
                return Twapi_GetIfTable(ticP, dw);
            case 204:
                return Twapi_GetIpAddrTable(ticP, dw);
            case 205:
                return Twapi_GetIpNetTable(ticP, dw);
            case 206:
                return Twapi_GetIpForwardTable(ticP, dw);
            case 207:
                result.value.ival = FlushIpNetTable(dw);
                result.type = TRT_EXCEPTION_ON_ERROR;
                break;
            }
        } else {
            s = Tcl_GetUnicode(objv[2]);
            switch (func) {
            case 251:
                result.type = GetAdapterIndex((LPWSTR)s, &result.value.ival)
                    ? TRT_GETLASTERROR
                    : TRT_DWORD;
                break;
            case 252: // Twapi_IPAddressFamily - TBD - optimizable?
                result.value.ival = 0;
                result.type = TRT_DWORD;
                dw = sizeof(u.ss);
                dw2 = sizeof(u.ss); /* Since first call might change dw */
                if (WSAStringToAddressW(s, AF_INET, NULL, (struct sockaddr *)&u.ss, &dw) == 0 ||
                    WSAStringToAddressW(s, AF_INET6, NULL, (struct sockaddr *)&u.ss, &dw2) == 0) {
                    result.value.ival = u.ss.ss_family;
                }
                break;

            case 26: // Twapi_NormalizeIPAddress
                dw = sizeof(u.ss);
                dw2 = sizeof(u.ss); /* Since first call might change dw */
                if (WSAStringToAddressW(s, AF_INET, NULL, (struct sockaddr *)&u.ss, &dw) == 0 ||
                    WSAStringToAddressW(s, AF_INET6, NULL, (struct sockaddr *)&u.ss, &dw2) == 0) {
                    result.type = TRT_OBJ;
                    if (u.ss.ss_family == AF_INET6) {
                        /* Do not want scope id in normalized form */
                        ((SOCKADDR_IN6 *)&u.ss)->sin6_scope_id = 0;
                    }
                    result.value.obj = ObjFromSOCKADDR_address((struct sockaddr *)&u.ss);
                } else {
                    result.type = TRT_GETLASTERROR;
                }
                break;
            }
        }
    } else if (func < 400) {
        if (objc != 4)
            return TwapiReturnError(interp, TWAPI_BAD_ARG_COUNT);
        CHECK_INTEGER_OBJ(interp, dw, objv[2]);
        CHECK_INTEGER_OBJ(interp, dw2, objv[3]);
        switch (func) {
        case 301:
            return Twapi_GetAdaptersAddresses(ticP, dw, dw2, NULL);
        case 302:
            return Twapi_AllocateAndGetTcpExTableFromStack(ticP, dw, dw2);
        case 303:
            return Twapi_AllocateAndGetUdpExTableFromStack(ticP, dw, dw2);
        }
    } else {
        /* Free-for-all - each func responsible for checking arguments */
        /* At least one arg present */
        if (objc < 3)
            return TwapiReturnError(interp, TWAPI_BAD_ARG_COUNT);

        switch (func) {
        case 10000: // Twapi_FormatExtendedTcpTable
        case 10001: // Twapi_FormatExtendedUdpTable
            if (TwapiGetArgs(interp, objc-2, objv+2,
                             GETVOIDP(pv), GETINT(dw), GETINT(dw2),
                             ARGEND) != TCL_OK)
                return TCL_ERROR;
            return (func == 10000 ? Twapi_FormatExtendedTcpTable : Twapi_FormatExtendedUdpTable)
                (interp, pv, dw, dw2);
        case 10002: // GetExtendedTcpTable
        case 10003: // GetExtendedUdpTable
            if (TwapiGetArgs(interp, objc-2, objv+2,
                             GETVOIDP(pv), GETINT(dw), GETBOOL(dw2),
                             GETINT(dw3), GETINT(dw4),
                             ARGEND) != TCL_OK)
                return TCL_ERROR;
            return (func == 10002 ? Twapi_GetExtendedTcpTable : Twapi_GetExtendedUdpTable)
                (interp, pv, dw, dw2, dw3, dw4);

        case 10004: // ResolveAddressAsync
        case 10005: // ResolveHostnameAsync
            return (func == 10004 ? Twapi_ResolveAddressAsync : Twapi_ResolveHostnameAsync)
                (ticP, objc-2, objv+2);
        case 10006:
            return Twapi_GetAddrInfo(interp, objc-2, objv+2);
        case 10007:
            return Twapi_GetNameInfo(interp, objc-2, objv+2);
        case 10008:
            return Twapi_GetBestRoute(ticP, objc-2, objv+2);
        case 10009: // GetBestInterface
            return Twapi_GetBestInterface(ticP, objc-2, objv+2);




        }
    }

    return TwapiSetResult(interp, &result);
}


static int Twapi_NetworkInitCalls(Tcl_Interp *interp, TwapiInterpContext *ticP)
{
    /* Create the underlying call dispatch commands */
    Tcl_CreateObjCommand(interp, "twapi::NetworkCall", Twapi_NetworkCallObjCmd, ticP, NULL);

    /* Now add in the aliases for the Win32 calls pointing to the dispatcher */
#define CALL_(fn_, call_, code_)                                         \
    do {                                                                \
        Twapi_MakeCallAlias(interp, "twapi::" #fn_, "twapi::Network" #call_, # code_); \
    } while (0);

    CALL_(GetNetworkParams, Call, 1);
    CALL_(GetAdaptersInfo, Call, 2);
    CALL_(GetInterfaceInfo, Call, 3);
    CALL_(GetNumberOfInterfaces, Call, 4);
    CALL_(SetTcpEntry, Call, 101);
    CALL_(GetPerAdapterInfo, Call, 201);
    CALL_(GetIfEntry, Call, 202);
    CALL_(GetIfTable, Call, 203);
    CALL_(GetIpAddrTable, Call, 204);
    CALL_(GetIpNetTable, Call, 205);
    CALL_(GetIpForwardTable, Call, 206);
    CALL_(FlushIpNetTable, Call, 207);
    CALL_(GetAdapterIndex, Call, 251);
    CALL_(Twapi_IPAddressFamily, Call, 252); // TBD - Tcl interface
    CALL_(Twapi_NormalizeIPAddress, Call, 253); // TBD - Tcl interface
    CALL_(GetAdaptersAddresses, Call, 301);
    CALL_(AllocateAndGetTcpExTableFromStack, Call, 302);
    CALL_(AllocateAndGetUdpExTableFromStack, Call, 303);
    CALL_(Twapi_FormatExtendedTcpTable, Call, 10000);
    CALL_(Twapi_FormatExtendedUdpTable, Call, 10001);
    CALL_(GetExtendedTcpTable, Call, 10002);
    CALL_(GetExtendedUdpTable, Call, 10003);
    CALL_(Twapi_ResolveAddressAsync, Call, 10004);
    CALL_(Twapi_ResolveHostnameAsync, Call, 10005);
    CALL_(getaddrinfo, Call, 10006);
    CALL_(getnameinfo, Call, 10007);
    CALL_(GetBestRoute, Call, 10008);
    CALL_(GetBestInterface, Call, 10009); /* Also mapped to GetBestInterfaceEx */
    CALL_(GetBestInterfaceEx, Call, 10009);



#undef CALL_

    return TCL_OK;
}

#ifndef TWAPI_STATIC_BUILD
BOOL WINAPI DllMain(HINSTANCE hmod, DWORD reason, PVOID unused)
{
    if (reason == DLL_PROCESS_ATTACH)
        gModuleHandle = hmod;
    return TRUE;
}
#endif

/* Main entry point */
#ifndef TWAPI_STATIC_BUILD
__declspec(dllexport) 
#endif
int Twapi_network_Init(Tcl_Interp *interp)
{
    /* IMPORTANT */
    /* MUST BE FIRST CALL as it initializes Tcl stubs - should this be the
       done for EVERY interp creation or move into one-time above ? TBD
     */
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
        return TCL_ERROR;
    }

    return Twapi_ModuleInit(interp, MODULENAME, MODULE_HANDLE,
                            Twapi_NetworkInitCalls, NULL) ? TCL_OK : TCL_ERROR;
}
