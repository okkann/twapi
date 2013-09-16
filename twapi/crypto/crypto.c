/* 
 * Copyright (c) 2007-2009 Ashok P. Nadkarni
 * All rights reserved.
 *
 * See the file LICENSE for license
 */

/* Interface to CryptoAPI */

#include "twapi.h"
#include "twapi_crypto.h"

#ifndef TWAPI_SINGLE_MODULE
HMODULE gModuleHandle;     /* DLL handle to ourselves */
#endif


void TwapiRegisterCertPointer(Tcl_Interp *interp, PCCERT_CONTEXT certP)
{
    if (TwapiRegisterCountedPointer(interp, certP, CertFreeCertificateContext)
        != TCL_OK)
        Tcl_Panic("Failed to register CERT_CONTEXT pointer: %s", Tcl_GetStringResult(interp));
}

void TwapiRegisterCertPointerTic(TwapiInterpContext *ticP, PCCERT_CONTEXT certP)
{
    /* Certificate pointers are counted pointers since the Cert* API
       can return the same pointer multiple ways.
    */
    if (TwapiRegisterCountedPointerTic(ticP, certP, CertFreeCertificateContext)
        != TCL_OK)
        Tcl_Panic("Failed to register CERT_CONTEXT pointer: %s", Tcl_GetStringResult(ticP->interp));
}

TCL_RESULT TwapiUnregisterCertPointer(Tcl_Interp *interp, PCCERT_CONTEXT certP)
{
    return TwapiUnregisterPointer(interp, certP, CertFreeCertificateContext);
}

TCL_RESULT TwapiUnregisterCertPointerTic(TwapiInterpContext *ticP, PCCERT_CONTEXT certP)
{
    return TwapiUnregisterPointerTic(ticP, certP, CertFreeCertificateContext);
}

#ifdef NOTNEEDED
/* RtlGenRandom in base provides this */
int Twapi_CryptGenRandom(Tcl_Interp *interp, HCRYPTPROV provH, DWORD len)
{
    BYTE buf[256];

    if (len > sizeof(buf)) {
        Tcl_SetObjErrorCode(interp,
                            Twapi_MakeTwapiErrorCodeObj(TWAPI_INTERNAL_LIMIT));
        ObjSetStaticResult(interp, "Too many random bytes requested.");
        return TCL_ERROR;
    }

    if (CryptGenRandom(provH, len, buf)) {
        ObjSetResult(interp, ObjFromByteArray(buf, len));
        return TCL_OK;
    } else {
        return TwapiReturnSystemError(interp);
    }
}
#endif

static Tcl_Obj *ObjFromCRYPT_BIT_BLOB(CRYPT_BIT_BLOB *blobP)
{
    Tcl_Obj *objs[2];
    if (blobP->cbData && blobP->pbData)
        objs[0] = ObjFromByteArray(blobP->pbData, blobP->cbData);
    else
        objs[0] = ObjFromEmptyString();
    objs[1] = ObjFromDWORD(blobP->cUnusedBits);
    return ObjNewList(2, objs);
}

static Tcl_Obj *ObjFromCERT_ALT_NAME_ENTRY(CERT_ALT_NAME_ENTRY *caneP)
{
    Tcl_Obj *objs[2];
    int nobjs;

    nobjs = 2;
    switch (caneP->dwAltNameChoice) {
    case CERT_ALT_NAME_RFC822_NAME: /* FALLTHRU */
    case CERT_ALT_NAME_DNS_NAME:    /* FALLTHRU */
    case CERT_ALT_NAME_URL:
        objs[1] = ObjFromUnicode(caneP->pwszURL);
        break;
    case CERT_ALT_NAME_OTHER_NAME:
        objs[0] = ObjFromString(caneP->pOtherName->pszObjId);
        objs[1] = ObjFromByteArray(caneP->pOtherName->Value.pbData,
                                   caneP->pOtherName->Value.cbData);
        objs[1] = ObjNewList(2, objs);
        break;
    case CERT_ALT_NAME_DIRECTORY_NAME: /* FALLTHRU */
    case CERT_ALT_NAME_IP_ADDRESS:
        objs[1] = ObjFromByteArray(caneP->IPAddress.pbData,
                                   caneP->IPAddress.cbData);
        break;
    case CERT_ALT_NAME_REGISTERED_ID:
        objs[1] = ObjFromString(caneP->pszRegisteredID);
        break;
    default:
        nobjs = 1;              /* Only report type */
    }

    objs[0] = ObjFromDWORD(caneP->dwAltNameChoice);
    return ObjNewList(nobjs, objs);
}

static Tcl_Obj *ObjFromCERT_ALT_NAME_INFO(CERT_ALT_NAME_INFO *infP)
{
    Tcl_Obj *objP;
    DWORD i;

    objP = ObjNewList(infP->cAltEntry, NULL);
    for (i = 0; i < infP->cAltEntry; ++i) {
        ObjAppendElement(NULL, objP,
                         ObjFromCERT_ALT_NAME_ENTRY(&infP->rgAltEntry[i]));
    }
    return objP;
}

static Tcl_Obj *ObjFromCRYPT_KEY_PROV_INFO(CRYPT_KEY_PROV_INFO *infP)
{
    Tcl_Obj *objs[6];
    DWORD i;
    
    objs[0] = ObjFromUnicode(infP->pwszContainerName);
    objs[1] = ObjFromUnicode(infP->pwszProvName);
    objs[2] = ObjFromDWORD(infP->dwProvType);
    objs[3] = ObjFromDWORD(infP->dwFlags);
    objs[4] = ObjNewList(infP->cProvParam, NULL);
    objs[5] = ObjFromDWORD(infP->dwKeySpec);
    
    if (infP->rgProvParam) {
        for (i=0; i < infP->cProvParam; ++i) {
            /* TBD - for now just return raw bytes. */
            Tcl_Obj *parObjs[3];
            parObjs[0] = ObjFromDWORD(infP->rgProvParam[i].dwParam);
            parObjs[1] = ObjFromByteArray(infP->rgProvParam[i].pbData,
                                         infP->rgProvParam[i].cbData);
            parObjs[2] = ObjFromDWORD(infP->rgProvParam[i].dwFlags);
            ObjAppendElement(NULL, objs[4], ObjNewList(3, parObjs));
        }
    }

    return ObjNewList(6, objs);
}


/* Note caller has to clean up ticP->memlifo irrespective of success/error */
static TCL_RESULT ParseCRYPT_BIT_BLOB(
    TwapiInterpContext *ticP,
    Tcl_Obj *pkObj,
    CRYPT_BIT_BLOB *blobP
    )
{
    Tcl_Obj **objs;
    int       nobjs;
    Tcl_Interp *interp = ticP->interp;

    if (ObjGetElements(NULL, pkObj, &nobjs, &objs) != TCL_OK ||
        TwapiGetArgsEx(ticP, nobjs, objs, GETBA(blobP->pbData, blobP->cbData),
                       GETINT(blobP->cUnusedBits), ARGEND) != TCL_OK ||
        blobP->cUnusedBits > 7) {
        ObjSetStaticResult(interp, "Invalid CERT_PUBLIC_KEY_INFO structure");
        return TCL_ERROR;
    }
    return TCL_OK;
}

/* Returns CERT_ALT_NAME_ENTRY structure in *caneP
   using memory from ticP->memlifo. Caller responsible for storage
   in both success and error cases
*/
static TCL_RESULT ParseCERT_ALT_NAME_ENTRY(
    TwapiInterpContext *ticP,
    Tcl_Obj *nameObj,
    CERT_ALT_NAME_ENTRY *caneP
    )
{
    Tcl_Obj **objs;
    int nobjs;
    DWORD name_type;
    int n;
    Tcl_Obj **otherObjs;
    void *pv;
    
    if (ObjGetElements(NULL, nameObj, &nobjs, &objs) != TCL_OK ||
        nobjs != 2 ||
        ObjToDWORD(NULL, objs[0], &name_type) != TCL_OK) {
        goto format_error;
    }

    switch (name_type) {
    case CERT_ALT_NAME_RFC822_NAME: /* FALLTHROUGH */
    case CERT_ALT_NAME_DNS_NAME: /* FALLTHROUGH */
    case CERT_ALT_NAME_URL:
        pv = ObjToUnicodeN(objs[1], &n);
        caneP->pwszRfc822Name = MemLifoCopy(&ticP->memlifo, pv, sizeof(WCHAR) * (n+1));
        break;
    case CERT_ALT_NAME_REGISTERED_ID:
        pv = ObjToStringN(objs[1], &n);
        caneP->pszRegisteredID = MemLifoCopy(&ticP->memlifo, pv, n+1);
        break;
    case CERT_ALT_NAME_OTHER_NAME:
        caneP->pOtherName = MemLifoAlloc(&ticP->memlifo, sizeof(CERT_OTHER_NAME), NULL);
        if (ObjGetElements(NULL, objs[1], &n, &otherObjs) != TCL_OK ||
            n != 2)
            goto format_error;
        pv = ObjToStringN(otherObjs[0], &n);
        caneP->pOtherName->pszObjId = MemLifoCopy(&ticP->memlifo, pv, n+1);
        pv = ObjToByteArray(otherObjs[1], &n);
        caneP->pOtherName->Value.pbData = MemLifoCopy(&ticP->memlifo, pv, n);
        caneP->pOtherName->Value.cbData = n;
        break;
    case CERT_ALT_NAME_DIRECTORY_NAME: /* FALLTHRU */
    case CERT_ALT_NAME_IP_ADDRESS: /* FALLTHRU */
        pv = ObjToByteArray(objs[1], &n);
        caneP->IPAddress.pbData = MemLifoCopy(&ticP->memlifo, pv, n);
        caneP->IPAddress.cbData = n;
        break;
        
    default:
        goto format_error;
    }

    caneP->dwAltNameChoice = name_type;
    return TCL_OK;

format_error:
    ObjSetResult(ticP->interp,
                 Tcl_ObjPrintf("Invalid or unsupported name format \"%s\"", ObjToString(nameObj)));
    return TCL_ERROR;
}



/* Returns CERT_ALT_NAME_INFO structure in *caniP
   using memory from ticP->memlifo. Caller responsible for storage
   in both success and error cases
*/
static TCL_RESULT ParseCERT_ALT_NAME_INFO(
    TwapiInterpContext *ticP,
    Tcl_Obj *altnameObj,
    CERT_ALT_NAME_INFO *caniP
    )
{
    Tcl_Obj **nameObjs;
    int       nnames, i;
    TCL_RESULT res;
    CERT_ALT_NAME_ENTRY *entriesP;

    if ((res = ObjGetElements(ticP->interp, altnameObj, &nnames, &nameObjs))
        != TCL_OK)
        return res;

    if (nnames == 0) {
        caniP->cAltEntry = 0;
        caniP->rgAltEntry = NULL;
        return TCL_OK;
    }

    entriesP = MemLifoAlloc(&ticP->memlifo, nnames * sizeof(*entriesP), NULL);

    for (i = 0; i < nnames; ++i) {
        res = ParseCERT_ALT_NAME_ENTRY(ticP, nameObjs[i], &entriesP[i]);
        if (res != TCL_OK)
            return res;
    }

    caniP->cAltEntry = nnames;
    caniP->rgAltEntry = entriesP;
    return TCL_OK;
}


/* Returns CERT_ENHKEY_USAGE structure in *cekuP
   using memory from ticP->memlifo. Caller responsible for storage
   in both success and error cases
*/
static TCL_RESULT ParseCERT_ENHKEY_USAGE(
    TwapiInterpContext *ticP,
    Tcl_Obj *cekuObj,
    CERT_ENHKEY_USAGE *cekuP
    )
{
    Tcl_Obj **objs;
    int       nobjs;
    int       i, n;
    char     *p;
    TCL_RESULT res;

    if ((res = ObjGetElements(ticP->interp, cekuObj, &nobjs, &objs)) != TCL_OK)
        return res;

    cekuP->cUsageIdentifier = nobjs;
    cekuP->rgpszUsageIdentifier = MemLifoAlloc(&ticP->memlifo,
                                               nobjs * sizeof(cekuP->rgpszUsageIdentifier[0]),
                                               NULL);
    for (i = 0; i < nobjs; ++i) {
        p = ObjToStringN(objs[i], &n);
        cekuP->rgpszUsageIdentifier[i] = MemLifoCopy(&ticP->memlifo, p, n+1);
    }

    return TCL_OK;
}

/* Returns CERT_CHAIN_PARA structure using memory from ticP->memlifo.
 * Caller responsible for storage in both success and error cases
 */
static TCL_RESULT ParseCERT_CHAIN_PARA(
    TwapiInterpContext *ticP,
    Tcl_Obj *paramObj,
    CERT_CHAIN_PARA *paramP
    )
{
    Tcl_Obj **objs;
    int       i, n;
    Tcl_Interp *interp = ticP->interp;

    /*
     * CERT_CHAIN_PARA is a list, currently containing exactly one element -
     * the RequestedUsage field. This is a list of two elements, a DWORD
     * indicating the boolean operation and an array of CERT_ENHKEY_USAGE
     */
    if (ObjGetElements(NULL, paramObj, &n, &objs) != TCL_OK ||
        n != 1 ||
        ObjGetElements(NULL, objs[0], &n, &objs) != TCL_OK ||
        n != 2 ||
        ObjToInt(NULL, objs[0], &i) != TCL_OK)
        return TwapiReturnErrorMsg(interp, TWAPI_INVALID_ARGS, "Invalid CERT_CHAIN_PARA");

    ZeroMemory(paramP, sizeof(paramP));

    paramP->cbSize = sizeof(*paramP);
    paramP->RequestedUsage.dwType = i;
    if (ParseCERT_ENHKEY_USAGE(ticP, objs[1], &paramP->RequestedUsage.Usage)
        != TCL_OK)
        return TCL_ERROR;

    return TCL_OK;
}

/* Returns CERT_CHAIN_POLICY_PARA structure using memory from ticP->memlifo.
 * Caller responsible for releasing storage in both success and error cases
 */
static TCL_RESULT ParseCERT_CHAIN_POLICY_PARA_SSL(
    TwapiInterpContext *ticP,
    Tcl_Obj *paramObj,
    CERT_CHAIN_POLICY_PARA **policy_paramPP
    )
{
    Tcl_Obj **objs;
    int       flags, n;
    CERT_CHAIN_POLICY_PARA *policy_paramP;
    
    /*
     * CERT_CHAIN_POLICY_PARA is a list, currently containing one or two
     * elements - a flags field, and optionally an
     * SSL_EXTRA_CERT_CHAIN_POLICY_PARA field.
     */
    if (ObjGetElements(NULL, paramObj, &n, &objs) != TCL_OK ||
        (n != 1 && n != 2) ||
        ObjToInt(NULL, objs[0], &flags) != TCL_OK)
        return TwapiReturnErrorMsg(ticP->interp, TWAPI_INVALID_ARGS, "Invalid CERT_CHAIN_POLICY_PARA");

    policy_paramP = MemLifoAlloc(&ticP->memlifo, sizeof(*policy_paramP), NULL);
    ZeroMemory(policy_paramP, sizeof(*policy_paramP));
    policy_paramP->cbSize = sizeof(*policy_paramP);
    policy_paramP->dwFlags = flags;

    if (n == 1)
        policy_paramP->pvExtraPolicyPara = NULL;
    else {
        SSL_EXTRA_CERT_CHAIN_POLICY_PARA *sslP;
        /* Parse the SSL_EXTRA_CERT_CHAIN_POLICY_PARA */
        if (ObjGetElements(NULL, objs[1], &n, &objs) != TCL_OK)
            goto error_return;
        sslP = MemLifoAlloc(&ticP->memlifo, sizeof(*sslP), NULL);
        sslP->cbSize = sizeof(*sslP);
        if (TwapiGetArgsEx(ticP, n, objs,
                           GETINT(sslP->dwAuthType),
                           GETINT(sslP->fdwChecks),
                           GETSTRW(sslP->pwszServerName),
                           ARGEND) != TCL_OK)
            goto error_return;
        policy_paramP->pvExtraPolicyPara = sslP;
    }

    *policy_paramPP = policy_paramP;
    return TCL_OK;

error_return:
    return TwapiReturnErrorMsg(ticP->interp, TWAPI_INVALID_ARGS, "Invalid SSL_EXTRA_CERT_CHAIN_POLICY_PARA");

}

/* Fill in CRYPT_ALGORITHM_IDENTIFIER structure in *algidP
   using memory from ticP->memlifo. Caller responsible for storage
   in both success and error cases
*/
static TCL_RESULT ParseCRYPT_ALGORITHM_IDENTIFIER(
    TwapiInterpContext *ticP,
    Tcl_Obj *algObj,
    CRYPT_ALGORITHM_IDENTIFIER *algidP
    )
{
    TCL_RESULT res;
    Tcl_Obj **objs;
    int       n, nobjs;
    char     *p;

    if ((res = ObjGetElements(ticP->interp, algObj, &nobjs, &objs)) != TCL_OK)
        return res;

    if (nobjs != 1) {
        ObjSetStaticResult(ticP->interp, "Invalid algorithm identifier format or unsupported parameters");
        return TCL_ERROR;
    }

    p = ObjToStringN(objs[0], &n);
    algidP->pszObjId = MemLifoCopy(&ticP->memlifo, p, n+1);
    algidP->Parameters.cbData = 0;
    algidP->Parameters.pbData = 0;
    return TCL_OK;
}


/* Note caller has to clean up ticP->memlifo irrespective of success/error */
static TCL_RESULT ParseCERT_PUBLIC_KEY_INFO(
    TwapiInterpContext *ticP,
    Tcl_Obj *pkObj,
    CERT_PUBLIC_KEY_INFO *pkP
    )
{
    Tcl_Obj **objs;
    int       nobjs;

    if (ObjGetElements(NULL, pkObj, &nobjs, &objs) != TCL_OK || nobjs != 2) {
        ObjSetStaticResult(ticP->interp,
                             "Invalid CERT_PUBLIC_KEY_INFO structure");
        return TCL_ERROR;
    }
    
    if (ParseCRYPT_ALGORITHM_IDENTIFIER(ticP, objs[0], &pkP->Algorithm) != TCL_OK ||
        ParseCRYPT_BIT_BLOB(ticP, objs[1], &pkP->PublicKey) != TCL_OK)
        return TCL_ERROR;
    
    return TCL_OK;
}

static TCL_RESULT TwapiCryptDecodeObject(
    Tcl_Interp *interp,
    void *poid, /* Either a Tcl_Obj or a #define X509* int value */
    void *penc,
    DWORD nenc,
    Tcl_Obj **objPP)
{
    Tcl_Obj *objP;
    union {
        void *pv;
        CERT_ENHKEY_USAGE *enhkeyP;
        CERT_ALT_NAME_INFO *altnameP;
        CRYPT_BIT_BLOB     *bitsP;
    } u;
    DWORD n;
    LPCSTR oid;
    DWORD_PTR dwoid;

    /*
     * poid may be a Tcl_Obj or a dword corresponding to a Win32 #define
     * This is how the CryptDecodeObjEx API works
     */
    if ((DWORD_PTR) poid <= 65535) {
        dwoid = (DWORD_PTR) poid;
        oid = poid;
    } else {
        /* It's a Tcl_Obj */
        Tcl_Obj *oidObj = poid;
        if (ObjToDWORD(NULL, oidObj, &n) == TCL_OK && n < 65536) {
            dwoid = (DWORD_PTR) n;
            oid = (LPSTR) (DWORD_PTR) n;
        } else {
            oid = ObjToString(oidObj);
            if (STREQ(oid, szOID_ENHANCED_KEY_USAGE))
                dwoid = (DWORD_PTR) X509_ENHANCED_KEY_USAGE;
            else if (STREQ(oid, szOID_ENHANCED_KEY_USAGE))
                dwoid = (DWORD_PTR) X509_KEY_USAGE;
            else if (STREQ(oid, szOID_SUBJECT_ALT_NAME2) ||
                     STREQ(oid, szOID_ISSUER_ALT_NAME2) ||
                     STREQ(oid, szOID_SUBJECT_ALT_NAME) ||
                     STREQ(oid, szOID_ISSUER_ALT_NAME)) {
                dwoid = (DWORD_PTR) X509_ALTERNATE_NAME;
            } else
                dwoid = 65536;      /* Will return as a byte array */
        }
    }

    if (! CryptDecodeObjectEx(
            X509_ASN_ENCODING|PKCS_7_ASN_ENCODING,
            oid, penc, nenc,
            CRYPT_DECODE_ALLOC_FLAG | CRYPT_DECODE_NOCOPY_FLAG | CRYPT_DECODE_SHARE_OID_STRING_FLAG,
            NULL,
            &u.pv,
            &n))
        return TwapiReturnSystemError(interp);
    
    switch (dwoid) {
    case (DWORD_PTR) X509_KEY_USAGE:
        objP = ObjFromCRYPT_BIT_BLOB(u.bitsP);
        break;
    case (DWORD_PTR) X509_ENHANCED_KEY_USAGE:
        objP = ObjFromArgvA(u.enhkeyP->cUsageIdentifier,
                            u.enhkeyP->rgpszUsageIdentifier);
        break;
    case (DWORD_PTR) X509_ALTERNATE_NAME:
        objP = ObjFromCERT_ALT_NAME_INFO(u.altnameP);
        break;
    default:
        objP = ObjFromByteArray(u.pv, n);
        break;
    }

    LocalFree(u.pv);
    *objPP = objP;
    return TCL_OK;
}
    
/*
 * Note: Allocates memory for blobP from lifoP. Note structure internal
 * pointers may point to Tcl_Obj areas within valObj so
 *  TREAT RETURNED STRUCTURES AS VOLATILE.
 *
 * We use MemLifo instead of letting CryptEncodeObjectEx do its own
 * memory allocation because it greatly simplifies freeing memory in
 * caller when multiple allocations are made.
 */
static TCL_RESULT TwapiCryptEncodeObject(
    TwapiInterpContext *ticP,
    void *poid, /* Either a Tcl_Obj or a #define X509* int value */
    Tcl_Obj *valObj,
    CRYPT_OBJID_BLOB *blobP)
{
    DWORD     dw;
    TCL_RESULT res;
    void     *penc;
    int       nenc;
    union {
        CERT_ALT_NAME_INFO cani;
        CERT_ENHKEY_USAGE  ceku;
        CRYPT_BIT_BLOB     bitblob;
    } u;
    Tcl_Interp *interp = ticP->interp;
    LPCSTR oid;
    DWORD_PTR dwoid;

    /*
     * poid may be a Tcl_Obj or a dword corresponding to a Win32 #define
     * This is how the CryptEncodeObjEx API works
     */
    if ((DWORD_PTR) poid <= 65535) {
        dwoid = (DWORD_PTR) poid;
        oid = poid;
    } else {
        /* It's a Tcl_Obj */
        Tcl_Obj *oidObj = poid;
        if (ObjToDWORD(NULL, oidObj, &dw) == TCL_OK && dw < 65536) {
            dwoid = (DWORD_PTR) dw;
            oid = (LPSTR) (DWORD_PTR) dw;
        } else {
            oid = ObjToString(oidObj);
            if (STREQ(oid, szOID_ENHANCED_KEY_USAGE))
                dwoid = (DWORD_PTR) X509_ENHANCED_KEY_USAGE;
            else if (STREQ(oid, szOID_KEY_USAGE))
                dwoid = (DWORD_PTR) X509_KEY_USAGE;
            else if (STREQ(oid, szOID_SUBJECT_ALT_NAME2) ||
                     STREQ(oid, szOID_ISSUER_ALT_NAME2) ||
                     STREQ(oid, szOID_SUBJECT_ALT_NAME) ||
                     STREQ(oid, szOID_ISSUER_ALT_NAME)) {
                dwoid = (DWORD_PTR) X509_ALTERNATE_NAME;
            } else
                dwoid = 65536;      /* Will return as a byte array */
        }
    }


    switch (dwoid) {
    case (DWORD_PTR) X509_ALTERNATE_NAME:
        if ((res = ParseCERT_ALT_NAME_INFO(ticP, valObj, &u.cani)) != TCL_OK)
            return res;
        break;
    case (DWORD_PTR) X509_ENHANCED_KEY_USAGE:
        if ((res = ParseCERT_ENHKEY_USAGE(ticP, valObj, &u.ceku)) != TCL_OK)
            return res;
        break;
    case (DWORD_PTR) X509_KEY_USAGE:
        if ((res = ParseCRYPT_BIT_BLOB(ticP, valObj, &u.bitblob)) != TCL_OK)
            return res;
        break;
    default:
        return TwapiReturnErrorMsg(interp, TWAPI_UNSUPPORTED_TYPE, "Unsupported OID");
    }

    /* Assume 1000 bytes enough but get as much as we can */
    penc = MemLifoAlloc(&ticP->memlifo, 1000, &nenc);
    if (CryptEncodeObjectEx(PKCS_7_ASN_ENCODING|X509_ASN_ENCODING,
                            oid, &u, 
                            0, NULL, penc, &nenc) == 0) {
        if (GetLastError() != ERROR_MORE_DATA)
            return TwapiReturnSystemError(interp);
        /* Retry with specified buffer size */
        penc = MemLifoAlloc(&ticP->memlifo, nenc, &nenc);
        if (CryptEncodeObjectEx(PKCS_7_ASN_ENCODING|X509_ASN_ENCODING,
                                oid, &u, 0, NULL, penc, &nenc) == 0)
            return TwapiReturnSystemError(interp);
    }
    
    blobP->cbData = nenc;
    blobP->pbData = penc;

    /* Note caller has to MemLifoPopFrame to release lifo memory */
    return TCL_OK;
}

/* Returns pointer to a CERT_EXTENSIONS_IDENTIFIER structure in *extsPP
   using memory from ticP->memlifo. Caller responsible for storage in both
   success and error cases.
   Can return NULL in *extsPP if extObj is empty list.
*/
static TCL_RESULT ParseCERT_EXTENSION(
    TwapiInterpContext *ticP,
    Tcl_Obj *extObj,
    CERT_EXTENSION *extP
    )
{
    Tcl_Obj **objs;
    int       nobjs, n;
    TCL_RESULT res;
    void      *pv;
    BOOL       bval;

    if ((res = ObjGetElements(ticP->interp, extObj, &nobjs, &objs)) != TCL_OK)
        return res;
    if (nobjs != 2 && nobjs != 3) {
        ObjSetStaticResult(ticP->interp, "Certificate extension format invalid or not implemented");
        return TCL_ERROR;
    }
    if ((res = ObjToBoolean(ticP->interp, objs[1], &bval)) != TCL_OK)
        return res;

    pv = ObjToStringN(objs[0], &n);
    extP->pszObjId = MemLifoCopy(&ticP->memlifo, pv, n+1);
    extP->fCritical = (BOOL) bval;
    if (nobjs == 3) {
        res = TwapiCryptEncodeObject(ticP,
                                     objs[0], objs[2],
                                     &extP->Value);
        if (res != TCL_OK)
            return res;
    } else {
        extP->Value.cbData = 0;
        extP->Value.pbData = NULL;
    }

    return TCL_OK;
}

/* Returns pointer to a CERT_EXTENSIONS_IDENTIFIER structure in *extsPP
   using memory from ticP->memlifo. Caller responsible for storage in both
   success and error cases.
   Can return NULL in *extsPP if extObj is empty list.
*/
static TCL_RESULT ParseCERT_EXTENSIONS(
    TwapiInterpContext *ticP,
    Tcl_Obj *extsObj,
    DWORD *nextsP,
    CERT_EXTENSION **extsPP
    )
{
    CERT_EXTENSION *extsP;
    Tcl_Obj **objs;
    int       i, nobjs;
    TCL_RESULT res;
    Tcl_Interp *interp = ticP->interp;

    if ((res = ObjGetElements(interp, extsObj, &nobjs, &objs)) != TCL_OK)
        return res;

    if (nobjs == 0) {
        *extsPP = NULL;
        *nextsP = 0;
        return TCL_OK;
    }

    extsP = MemLifoAlloc(&ticP->memlifo, nobjs * sizeof(CERT_EXTENSION), NULL);
    for (i = 0; i < nobjs; ++i) {
        if ((res = ParseCERT_EXTENSION(ticP, objs[i], &extsP[i])) != TCL_OK)
            return res;
    }
    *extsPP = extsP;
    *nextsP = nobjs;
    return TCL_OK;
}

/* Note caller has to clean up ticP->memlifo irrespective of success/error */
static TCL_RESULT ParseCERT_INFO(
    TwapiInterpContext *ticP,
    Tcl_Obj *ciObj,
    CERT_INFO *ciP             /* Will contain garbage in case of errors */
    )
{
    Tcl_Obj **objs;
    int       nobjs;
    Tcl_Interp *interp = ticP->interp;
    Tcl_Obj *algObj, *pubkeyObj, *issuerIdObj, *subjectIdObj, *extsObj;

    if (ObjGetElements(NULL, ciObj, &nobjs, &objs) != TCL_OK ||
        TwapiGetArgsEx(ticP, nobjs, objs,
                       GETINT(ciP->dwVersion),
                       GETBA(ciP->SerialNumber.pbData, ciP->SerialNumber.cbData),
                       GETOBJ(algObj),
                       GETBA(ciP->Issuer.pbData, ciP->Issuer.cbData),
                       GETVAR(ciP->NotBefore, ObjToFILETIME),
                       GETVAR(ciP->NotAfter, ObjToFILETIME),
                       GETBA(ciP->Subject.pbData, ciP->Subject.cbData),
                       GETOBJ(pubkeyObj),
                       GETOBJ(issuerIdObj),
                       GETOBJ(subjectIdObj),
                       GETOBJ(extsObj), ARGEND) != TCL_OK) {
        ObjSetStaticResult(interp, "Invalid CERT_INFO structure");
        return TCL_ERROR;
    }

    if (ParseCRYPT_ALGORITHM_IDENTIFIER(ticP, algObj, &ciP->SignatureAlgorithm) != TCL_OK ||
        ParseCERT_PUBLIC_KEY_INFO(ticP, pubkeyObj, &ciP->SubjectPublicKeyInfo) != TCL_OK ||
        ParseCRYPT_BIT_BLOB(ticP, issuerIdObj, &ciP->IssuerUniqueId) != TCL_OK ||
        ParseCRYPT_BIT_BLOB(ticP, subjectIdObj, &ciP->SubjectUniqueId) != TCL_OK ||
        ParseCERT_EXTENSIONS(ticP, extsObj, &ciP->cExtension, &ciP->rgExtension) != TCL_OK)
        return TCL_ERROR;
        
    return TCL_OK;
}

/* 
 * Parses a non-empty Tcl_Obj into a SYSTEMTIME structure *timeP 
 * and stores timeP in *timePP. If the Tcl_Obj is empty (meaning use default)
 * stores NULL in *timePP (and still return TCL_OK)
 */
static TCL_RESULT ParseSYSTEMTIME(
    Tcl_Interp *interp,
    Tcl_Obj *timeObj,
    SYSTEMTIME *timeP,
    SYSTEMTIME **timePP
    )
{
    Tcl_Obj **objs;
    int       nobjs;
    TCL_RESULT res;

    if ((res = ObjGetElements(interp, timeObj, &nobjs, &objs)) != TCL_OK)
        return res;
    if (nobjs == 0)
        *timePP = NULL;
    else {
        if ((res = ObjToSYSTEMTIME(interp, timeObj, timeP)) != TCL_OK)
            return res;
        *timePP = timeP;
    }
    return TCL_OK;
}


static int Twapi_CertCreateSelfSignCertificate(TwapiInterpContext *ticP, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    void *pv;
    HCRYPTPROV hprov;
    DWORD flags;
    int status;
    CERT_NAME_BLOB name_blob;
    CRYPT_KEY_PROV_INFO ki, *kiP;
    CRYPT_ALGORITHM_IDENTIFIER algid, *algidP;
    Tcl_Obj **objs;
    int       nobjs;
    SYSTEMTIME start, end, *startP, *endP;
    PCERT_CONTEXT certP;
    CERT_EXTENSIONS exts;
    Tcl_Obj *algidObj, *startObj, *endObj, *extsObj, *provinfoObj, *provparaObj;
    MemLifoMarkHandle mark;

    mark = MemLifoPushMark(&ticP->memlifo);

    if ((status = TwapiGetArgsEx(ticP, objc-1, objv+1,
                                 GETHANDLET(pv, HCRYPTPROV),
                                 GETBA(name_blob.pbData, name_blob.cbData),
                                 GETINT(flags),
                                 GETOBJ(provinfoObj),
                                 GETOBJ(algidObj),
                                 GETOBJ(startObj),
                                 GETOBJ(endObj),
                                 GETOBJ(extsObj),
                                 ARGEND)) != TCL_OK)
        goto vamoose;
    

    hprov = (HCRYPTPROV) pv;
 
    /* Parse CRYPT_KEY_PROV_INFO */
    if ((status = ObjGetElements(interp, provinfoObj, &nobjs, &objs)) != TCL_OK)
        goto vamoose;

    if (nobjs == 0)
        kiP = NULL;
    else {
        if (TwapiGetArgsEx(ticP, nobjs, objs,
                           GETSTRW(ki.pwszContainerName),
                           GETSTRW(ki.pwszProvName),
                           GETINT(ki.dwProvType),
                           GETINT(ki.dwFlags),
                           GETOBJ(provparaObj),
                           GETINT(ki.dwKeySpec),
                           ARGEND) != TCL_OK
            ||
            ObjListLength(NULL, provparaObj, &ki.cProvParam) != TCL_OK
            ||
            ki.cProvParam != 0) {
            ObjSetStaticResult(interp, "Invalid or unimplemented provider parameters");
            status = TCL_ERROR;
            goto vamoose;
        }
        ki.rgProvParam = NULL;
        kiP = &ki;
    }

    /* Parse CRYPT_ALGORITHM_IDENTIFIER */
    if ((status = ObjListLength(interp, algidObj, &nobjs)) != TCL_OK)
        goto vamoose;
    if (nobjs == 0)
        algidP = NULL;
    else {
        algidP = &algid;
        status = ParseCRYPT_ALGORITHM_IDENTIFIER(ticP, algidObj, algidP);
        if (status != TCL_OK)
            goto vamoose;
    }

    if ((status = ParseSYSTEMTIME(interp, startObj, &start, &startP)) != TCL_OK)
        goto vamoose;
    if ((status = ParseSYSTEMTIME(interp, endObj, &end, &endP)) != TCL_OK)
        goto vamoose;

    if ((status = ParseCERT_EXTENSIONS(ticP, extsObj,
                                       &exts.cExtension, &exts.rgExtension))
        != TCL_OK)
        goto vamoose;

    certP = (PCERT_CONTEXT) CertCreateSelfSignCertificate(
        hprov, &name_blob, flags, kiP, algidP, startP, endP,
        exts.rgExtension ? &exts : NULL);

    if (certP) {
        TwapiRegisterCertPointerTic(ticP, certP);
        ObjSetResult(interp, ObjFromOpaque(certP, "CERT_CONTEXT*"));
        status = TCL_OK;
    } else {
        status = TwapiReturnSystemError(interp);
    }

vamoose:
    MemLifoPopMark(mark);
    return status;
}

static int Twapi_CertGetCertificateContextProperty(Tcl_Interp *interp, PCCERT_CONTEXT certP, DWORD prop_id, int cooked)
{
    DWORD n = 0;
    TwapiResult result;
    void *pv;
    CERT_KEY_CONTEXT ckctx;
    char *s;
    DWORD_PTR dwp;
    TCL_RESULT res;

    result.type = TRT_BADFUNCTIONCODE;
    if (cooked) {
        switch (prop_id) {
        case CERT_ACCESS_STATE_PROP_ID:
        case CERT_KEY_SPEC_PROP_ID:
            result.type = TRT_DWORD; 
            n = sizeof(result.value.ival);
            result.type = CertGetCertificateContextProperty(certP, prop_id, &result.value.uval, &n) ? TRT_DWORD : TRT_GETLASTERROR;
            break;
        case CERT_DATE_STAMP_PROP_ID:
            n = sizeof(result.value.filetime);
            result.type = CertGetCertificateContextProperty(certP, prop_id,
                                                            &result.value.filetime, &n)
                ? TRT_FILETIME : TRT_GETLASTERROR;
            break;
        case CERT_ARCHIVED_PROP_ID:
            result.type = TRT_BOOL;
            if (! CertGetCertificateContextProperty(certP, prop_id, NULL, &n)) {
                if ((result.value.ival = GetLastError()) == CRYPT_E_NOT_FOUND)
                    result.value.bval = 0;
                else
                    result.type = TRT_EXCEPTION_ON_ERROR;
            } else
                result.value.bval = 1;
            break;

        case CERT_ENHKEY_USAGE_PROP_ID:
            if (! CertGetCertificateContextProperty(certP, prop_id, NULL, &n))
                return TwapiReturnSystemError(interp);
            pv = TwapiAlloc(n);
            if (! CertGetCertificateContextProperty(certP, prop_id, pv, &n)) {
                TwapiFree(pv);
                return TwapiReturnSystemError(interp);
            }        
            res = TwapiCryptDecodeObject(interp, (void*)X509_ENHANCED_KEY_USAGE, pv, n, &result.value.obj);
            TwapiFree(pv);
            if (res != TCL_OK)
                return res;
            result.type = TRT_OBJ;
            break;

        case CERT_KEY_PROV_INFO_PROP_ID:
            if (! CertGetCertificateContextProperty(certP, prop_id, NULL, &n))
                return TwapiReturnSystemError(interp);
            pv = TwapiAlloc(n);
            if (! CertGetCertificateContextProperty(certP, prop_id, pv, &n)) {
                TwapiFree(pv);
                return TwapiReturnSystemError(interp);
            }        
            result.value.obj = ObjFromCRYPT_KEY_PROV_INFO(pv);
            TwapiFree(pv);
            result.type = TRT_OBJ;
            break;

        case CERT_KEY_CONTEXT_PROP_ID:
            n = ckctx.cbSize = sizeof(ckctx);
            if (CertGetCertificateContextProperty(certP, prop_id, &ckctx, &n)) {
                result.value.obj = ObjNewList(0, NULL);
                if (ckctx.dwKeySpec == AT_KEYEXCHANGE ||
                    ckctx.dwKeySpec == AT_SIGNATURE) 
                    s = "HCRYPTPROV";
                else
                    s = "NCRYPT_KEY_HANDLE";
                ObjAppendElement(NULL, result.value.obj, ObjFromOpaque((void*)ckctx.hCryptProv, s));
                ObjAppendElement(NULL, result.value.obj, ObjFromDWORD(ckctx.dwKeySpec));
            } else
                result.type = TRT_GETLASTERROR;
            break;
        
        case CERT_KEY_PROV_HANDLE_PROP_ID:
            n = sizeof(dwp);
            if (CertGetCertificateContextProperty(certP, prop_id, &dwp, &n)) {
                TwapiResult_SET_PTR(result, HCRYPTPROV, (void*)dwp);
            } else
                result.type = TRT_GETLASTERROR;
            break;

#ifndef CERT_REQUEST_ORIGINATOR_PROP_ID
# define CERT_REQUEST_ORIGINATOR_PROP_ID 71
#endif
        case CERT_REQUEST_ORIGINATOR_PROP_ID:
        case CERT_AUTO_ENROLL_PROP_ID:
        case CERT_EXTENDED_ERROR_INFO_PROP_ID:
        case CERT_FRIENDLY_NAME_PROP_ID:
        case CERT_PVK_FILE_PROP_ID:
            if (! CertGetCertificateContextProperty(certP, prop_id, NULL, &n))
                return TwapiReturnSystemError(interp);
            result.value.unicode.str = TwapiAlloc(n);
            if (CertGetCertificateContextProperty(certP, prop_id,
                                                  result.value.unicode.str, &n)) {
                result.value.unicode.len = -1;
                result.type = TRT_UNICODE_DYNAMIC; /* Will also free memory */
            } else {
                TwapiReturnSystemError(interp);
                TwapiFree(result.value.unicode.str);
                return TCL_ERROR;
            }
            break;
        }
    } 

    if (result.type == TRT_BADFUNCTIONCODE) {
        /* Either raw format wanted or binary data */

        /*        
         * The following are handled via defaults for now
         *  CERT_DESCRIPTION_PROP_ID: // TBD - is this unicode?
         *  CERT_HASH_PROP_ID:
         *  CERT_ISSUER_PUBLIC_KEY_MD5_HASH_PROP_ID:
         *  CERT_ISSUER_SERIAL_NUMBER_MD5_HASH_PROP_ID:
         *  CERT_ARCHIVED_KEY_HASH_PROP_ID:
         *  CERT_KEY_IDENTIFIER_PROP_ID:
         *  CERT_KEY_PROV_INFO_PROP_ID
         *  CERT_MD5_HASH_PROP_ID
         *  CERT_RENEWAL_PROP_ID
         *  CERT_SHA1_HASH_PROP_ID
         *  CERT_SIGNATURE_HASH_PROP_ID
         *  CERT_SUBJECT_PUBLIC_KEY_MD5_HASH_PROP_ID
         */

        if (! CertGetCertificateContextProperty(certP, prop_id, NULL, &n))
            return TwapiReturnSystemError(interp);
        result.type = TRT_OBJ;
        result.value.obj = ObjFromByteArray(NULL, n);
        if (! CertGetCertificateContextProperty(
                certP, prop_id,
                ObjToByteArray(result.value.obj, &n),
                &n)) {
            TwapiReturnSystemError(interp);
            Tcl_DecrRefCount(result.value.obj);
            return TCL_ERROR;
        }
        Tcl_SetByteArrayLength(result.value.obj, n);
    }

    return TwapiSetResult(interp, &result);
}

static TCL_RESULT Twapi_SetCertContextKeyProvInfo(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    PCCERT_CONTEXT certP;
    CRYPT_KEY_PROV_INFO ckpi;
    Tcl_Obj **objs;
    int       nobjs;
    TCL_RESULT status;
    Tcl_Obj *connameObj, *provnameObj;

    /* Note - objc/objv have initial command name arg removed by caller */
    if ((status = TwapiGetArgs(interp, objc, objv,
                               GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                               ARGSKIP, ARGEND)) != TCL_OK)
        return status;

    if ((status = ObjGetElements(interp, objv[1], &nobjs, &objs)) != TCL_OK)
        return status;

    /* As always, extract WSTR AFTER other args to avoid shimmering */
    if ((status = TwapiGetArgs(interp, nobjs, objs,
                               GETOBJ(connameObj),
                               GETOBJ(provnameObj),
                               GETINT(ckpi.dwProvType),
                               GETINT(ckpi.dwFlags),
                               ARGSKIP, // cProvParam+rgProvParam
                               GETINT(ckpi.dwKeySpec),
                               ARGEND)) != TCL_OK)
        return status;

    ckpi.cProvParam = 0;
    ckpi.rgProvParam = NULL;

    ckpi.pwszContainerName = ObjToUnicode(connameObj);
    ckpi.pwszProvName = ObjToUnicode(provnameObj);
    if (CertSetCertificateContextProperty(certP, CERT_KEY_PROV_INFO_PROP_ID,
                                          0, &ckpi))
        return TCL_OK;
    else
        return TwapiReturnSystemError(interp);
}

static TCL_RESULT TwapiCertGetNameString(
    Tcl_Interp *interp,
    PCCERT_CONTEXT certP,
    DWORD type,
    DWORD flags,
    Tcl_Obj *owhat)
{
    void *pv;
    DWORD dw, nchars;
    WCHAR buf[1024];

    switch (type) {
    case CERT_NAME_EMAIL_TYPE: // 1
    case CERT_NAME_SIMPLE_DISPLAY_TYPE: // 4
    case CERT_NAME_FRIENDLY_DISPLAY_TYPE: // 5
    case CERT_NAME_DNS_TYPE: // 6
    case CERT_NAME_URL_TYPE: // 7
    case CERT_NAME_UPN_TYPE: // 8
        pv = NULL;
        break;
    case CERT_NAME_RDN_TYPE: // 2
        if (ObjToInt(interp, owhat, &dw) != TCL_OK)
            return TCL_ERROR;
        pv = &dw;
        break;
    case CERT_NAME_ATTR_TYPE: // 3
        pv = ObjToString(owhat);
        break;
    default:
        ObjSetResult(interp, Tcl_ObjPrintf("CertGetNameString: unknown type %d", type));
        return TCL_ERROR;
    }

    // 1 -> CERT_NAME_ISSUER_FLAG 
    // 0x00010000 -> CERT_NAME_DISABLE_IE4_UTF8_FLAG 
    // are supported.
    // 2 -> CERT_NAME_SEARCH_ALL_NAMES_FLAG
    // 0x00200000 -> CERT_NAME_STR_ENABLE_PUNYCODE_FLAG 
    // are post Win8 AND they will change output encoding/format
    // Only support what we know
    if (flags & ~(0x00010001)) {
        ObjSetResult(interp, Tcl_ObjPrintf("CertGetNameString: unsupported flags %d", flags));
        return TCL_ERROR;
    }

    nchars = CertGetNameStringW(certP, type, flags, pv, buf, ARRAYSIZE(buf));
    /* Note nchars includes terminating NULL */
    if (nchars > 1) {
        if (nchars < ARRAYSIZE(buf)) {
            ObjSetResult(interp, ObjFromUnicodeN(buf, nchars-1));
        } else {
            /* Buffer might have been truncated. Explicitly get buffer size */
            WCHAR *bufP;
            nchars = CertGetNameStringW(certP, type, flags, pv, NULL, 0);
            bufP = TwapiAlloc(nchars*sizeof(WCHAR));
            nchars = CertGetNameStringW(certP, type, flags, pv, bufP, nchars);
            ObjSetResult(interp, ObjFromUnicodeN(bufP, nchars-1));
            TwapiFree(bufP);
        }
    }
    return TCL_OK;
}

static TCL_RESULT Twapi_CryptSetProvParam(Tcl_Interp *interp,
                                          HCRYPTPROV hprov, DWORD param,
                                          DWORD flags, Tcl_Obj *objP)
{
    TCL_RESULT res;
    void *pv;
    HWND hwnd;
    SECURITY_DESCRIPTOR *secdP;

    switch (param) {
    case PP_CLIENT_HWND:
        if ((res = ObjToHWND(interp, objP, &hwnd)) != TCL_OK)
            return res;
        pv = &hwnd;
        break;
    case PP_DELETEKEY:
        pv = NULL;
        break;
    case PP_KEYEXCHANGE_PIN: /* FALLTHRU */
    case PP_SIGNATURE_PIN:
        pv = ObjToString(objP);
        break;
    case PP_KEYSET_SEC_DESCR:
        if ((res = ObjToPSECURITY_DESCRIPTOR(interp, objP, &secdP)) != TCL_OK)
            return res;
        /* TBD - check what happens with NULL secdP (which is valid) */
        pv = secdP;
        break;
#ifdef PP_PIN_PROMPT_STRING
    case PP_PIN_PROMPT_STRING:
#else
    case 44:
#endif
        /* FALLTHRU */
    case PP_UI_PROMPT:
        pv = ObjToUnicode(objP);
        break;
    default:
        return TwapiReturnErrorEx(interp, TWAPI_INVALID_ARGS, Tcl_ObjPrintf("Provider parameter %d not implemented", param));
    }

    if (CryptSetProvParam(hprov, param, pv, flags)) {
        res = TCL_OK;
    } else {
        res = TwapiReturnSystemError(interp);
    }

    TwapiFreeSECURITY_DESCRIPTOR(secdP); /* OK if NULL */
    
    return res;
}


static TCL_RESULT Twapi_CryptGetProvParam(Tcl_Interp *interp,
                                          HCRYPTPROV hprov,
                                          DWORD param, DWORD flags)
{
    Tcl_Obj *objP;
    DWORD n;
    void *pv;

    n = 0;
    /* Special case PP_ENUMCONTAINERS because of how the iteration
       works. We return ALL containers as opposed to one at a time */
    if (param == PP_ENUMCONTAINERS) {
        if (! CryptGetProvParam(hprov, param, NULL, &n, CRYPT_FIRST))
            return TwapiReturnSystemError(interp);
        /* n is now the max size buffer. Subsequent calls will not change that value */
        pv = TwapiAlloc(n * sizeof(char));
        objP = Tcl_NewListObj(0, NULL);
        flags = CRYPT_FIRST;
        while (CryptGetProvParam(hprov, param, pv, &n, flags)) {
            ObjAppendElement(NULL, objP, ObjFromString(pv));
            flags = CRYPT_NEXT;
        }
        n = GetLastError();
        TwapiFree(pv);
        if (n != ERROR_NO_MORE_ITEMS) {
            Tcl_DecrRefCount(objP);
            return Twapi_AppendSystemError(interp, n);
        }
        ObjSetResult(interp, objP);
        return TCL_OK;
    }
    
    if (! CryptGetProvParam(hprov, param, NULL, &n, flags))
        return TwapiReturnSystemError(interp);
    
    if (param == PP_KEYSET_SEC_DESCR) {
        objP = NULL;
        pv = TwapiAlloc(n);
    } else {
        objP = ObjFromByteArray(NULL, n);
        pv = ObjToByteArray(objP, &n);
    }

    if (! CryptGetProvParam(hprov, param, pv, &n, flags)) {
        if (objP)
            Tcl_DecrRefCount(objP);
        TwapiReturnSystemError(interp);
        return TCL_ERROR;
    }

    if (param == PP_KEYSET_SEC_DESCR) {
        if (n == 0)
            objP = ObjFromEmptyString();
        else
            objP = ObjFromSECURITY_DESCRIPTOR(interp, pv);
        TwapiFree(pv);
        if (objP == NULL)
            return TCL_ERROR;   /* interp already contains error */
    } else
        Tcl_SetByteArrayLength(objP, n);

    ObjSetResult(interp, objP);
    return TCL_OK;
}

static int Twapi_CertOpenStore(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    DWORD store_provider, enc_type, flags;
    void *pv = NULL;
    HCERTSTORE hstore;
    HANDLE h;
    TCL_RESULT res;

    if (TwapiGetArgs(interp, objc, objv,
                     GETINT(store_provider), GETINT(enc_type), ARGUNUSED,
                     GETINT(flags), ARGSKIP, ARGEND) != TCL_OK)
        return TCL_ERROR;
    
    /* Using literals because the #defines are cast as LPCSTR */
    switch (store_provider) {
    case 2: // CERT_STORE_PROV_MEMORY
    case 11: // CERT_STORE_PROV_COLLECTION
        break;

    case 3: // CERT_STORE_PROV_FILE
        if ((res = ObjToOpaque(interp, objv[4], &h, "HANDLE")) != TCL_OK)
            return res;
        pv = &h;
        break;

    case 4: // CERT_STORE_PROV_REG
        /* Docs imply pv itself is the handle unlike the FILE case above */
        if ((res = ObjToOpaque(interp, objv[4], &pv, "HANDLE")) != TCL_OK)
            return res;
        break;

    case 8: // CERT_STORE_PROV_FILENAME_W
    case 14: // CERT_STORE_PROV_PHYSICAL_W
    case 10: // CERT_STORE_PROV_SYSTEM_W
    case 13: // CERT_STORE_PROV_SYSTEM_REGISTRY_W
        pv = ObjToUnicode(objv[4]);
        break;

    case 5: // CERT_STORE_PROV_PKCS7
    case 6: // CERT_STORE_PROV_SERIALIZED
    case 15: // CERT_STORE_PROV_SMART_CARD
    case 16: // CERT_STORE_PROV_LDAP
    case 1: // CERT_STORE_PROV_MSG
    default:
        ObjSetResult(interp,
                         Tcl_ObjPrintf("Invalid or unsupported store provider \"%d\"", store_provider));
        return TCL_ERROR;
    }

    hstore = CertOpenStore(IntToPtr(store_provider), enc_type, 0, flags, pv);
    if (hstore) {
        /* CertCloseStore does not check ponter validity! So do ourselves*/
        if (TwapiRegisterPointer(interp, hstore, CertCloseStore) != TCL_OK)
            Tcl_Panic("Failed to register pointer: %s", Tcl_GetStringResult(interp));
        ObjSetResult(interp, ObjFromOpaque(hstore, "HCERTSTORE"));
        return TCL_OK;
    } else {
        if (flags & CERT_STORE_DELETE_FLAG) {
            /* Return value can mean success as well */
            if (GetLastError() == 0)
                return TCL_OK;
        }
        return TwapiReturnSystemError(interp);
    }
}

static TCL_RESULT Twapi_PFXExportCertStoreEx(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    HCERTSTORE hstore;
    LPWSTR password;
    int password_len;
    Tcl_Obj *objP;
    CRYPT_DATA_BLOB blob;
    BOOL status;
    int flags;
    
    if (TwapiGetArgs(interp, objc, objv,
                     GETVERIFIEDPTR(hstore, HCERTSTORE, CertCloseStore),
                     ARGSKIP, ARGUNUSED, 
                     GETINT(flags), ARGEND) != TCL_OK)
        return TCL_ERROR;
    
    password = ObjDecryptUnicode(interp, objv[1], &password_len);
    if (password == NULL)
        return TCL_ERROR;
    
    blob.cbData = 0;
    blob.pbData = NULL;

    status = PFXExportCertStoreEx(hstore, &blob, password, NULL, flags);
    
    SecureZeroMemory(password, sizeof(WCHAR) * password_len);
    TwapiFree(password);

    if (!status)
        return TwapiReturnSystemError(interp);

    if (blob.cbData == 0)
        return TCL_OK;        /* Nothing to export ? */

    objP = ObjFromByteArray(NULL, blob.cbData);
    blob.pbData = ObjToByteArray(objP, &blob.cbData);
    status = PFXExportCertStoreEx(hstore, &blob, password, NULL, flags);
    if (! status) {
        TwapiReturnSystemError(interp);
        Tcl_DecrRefCount(objP);
        return TCL_ERROR;
    }
    ObjSetResult(interp, objP);
    return TCL_OK;
}


static TCL_RESULT Twapi_CertFindCertificateInStoreObjCmd(
    TwapiInterpContext *ticP, Tcl_Interp *interp, int objc,
    Tcl_Obj *CONST objv[])
{
    HCERTSTORE hstore;
    PCCERT_CONTEXT certP, cert2P;
    DWORD         enctype, flags, findtype, dw;
    Tcl_Obj      *findObj;
    void         *pv;
    CERT_BLOB     blob;
    CERT_INFO     cinfo;
    TCL_RESULT    res;
    CERT_PUBLIC_KEY_INFO pki;
    MemLifoMarkHandle mark = NULL;

    certP = NULL;
    res = TwapiGetArgs(interp, objc, objv,
                       GETVERIFIEDPTR(hstore, HCERTSTORE, CertCloseStore),
                       GETINT(enctype), GETINT(flags), GETINT(findtype),
                       GETOBJ(findObj), GETVERIFIEDORNULL(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                       ARGEND);
    if (res != TCL_OK) {
        /* We have guaranteed caller certP will be freed even on error */
        if (certP && TwapiUnregisterCertPointerTic(ticP, certP) == TCL_OK)
            CertFreeCertificateContext(certP);
        return res;
    }    

    switch (findtype) {
    case CERT_FIND_ANY:
        pv = NULL;
        break;
    case CERT_FIND_EXISTING:
        res = ObjToVerifiedPointerTic(ticP, findObj, (void **)&cert2P, "CERT_CONTEXT*", CertFreeCertificateContext);
        if (res == TCL_OK)
            pv = (void *)cert2P;
        break;
    case CERT_FIND_SUBJECT_CERT:
        pv = &cinfo;
        mark = MemLifoPushMark(&ticP->memlifo);
        res = ParseCERT_INFO(ticP, findObj, &cinfo);
        break;
    case CERT_FIND_KEY_IDENTIFIER: /* FALLTHRU */
    case CERT_FIND_MD5_HASH:    /* FALLTHRU */
    case CERT_FIND_PUBKEY_MD5_HASH:    /* FALLTHRU */
    case CERT_FIND_SHA1_HASH:   /* FALLTHRU */
    case CERT_FIND_SIGNATURE_HASH: /* FALLTHRU */
    case CERT_FIND_ISSUER_NAME: /* FALLTHRU */
    case CERT_FIND_SUBJECT_NAME:
        blob.pbData = ObjToByteArray(findObj, &blob.cbData);
        pv = &blob;
        break;
    case CERT_FIND_ISSUER_STR_W: /* FALLTHRU */
    case CERT_FIND_SUBJECT_STR_W:
        pv = ObjToUnicode(findObj);
        break;
    case CERT_FIND_PROPERTY: /* FALLTHRU */
    case CERT_FIND_KEY_SPEC:
        res = ObjToDWORD(interp, findObj, &dw);
        pv = &dw;
        break;
    case CERT_FIND_PUBLIC_KEY:
        pv = &pki;
        mark = MemLifoPushMark(&ticP->memlifo);
        res = ParseCERT_PUBLIC_KEY_INFO(ticP, findObj, &pki);
        break;
    default:
        res = TwapiReturnError(interp, TWAPI_UNSUPPORTED_TYPE);
        break;
    }

    /*
     * CertFindCertificateInStore ALWAYS releases certP (even in error case)
     * Caller expects that to happen in all cases so if we are not
     * calling CertFindCertificateInStore because of previous errors,
     * do so ourselves
     */
    if (certP) {
        /* Do not change res unless it is an error */
        if (TwapiUnregisterCertPointerTic(ticP, certP) != TCL_OK)
            res = TCL_ERROR;
    }
    if (res != TCL_OK)
        CertFreeCertificateContext(certP);
    else {
        certP = CertFindCertificateInStore(
            pv,
            X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            0,
            CERT_FIND_SUBJECT_STR_W,
            pv,
            certP);
        if (certP) {
            TwapiRegisterCertPointerTic(ticP, certP);
            ObjSetResult(interp, ObjFromOpaque((void*)certP, "CERT_CONTEXT*"));
        } else {
            /* EOF is not an error */
            if (GetLastError() != CRYPT_E_NOT_FOUND)
                res = TwapiReturnSystemError(interp);
        }
    }

    if (mark)
        MemLifoPopMark(mark);

    return res;
}

static TCL_RESULT Twapi_CryptSignAndEncodeCertObjCmd(
    TwapiInterpContext *ticP, Tcl_Interp *interp, int objc,
    Tcl_Obj *CONST objv[])
{
    Tcl_Obj *algidObj, *certinfoObj, *encodedObj;
    TCL_RESULT res;
    CRYPT_ALGORITHM_IDENTIFIER algid;
    DWORD keyspec, enctype;
    CERT_INFO ci;
    HCRYPTPROV hprov;
    MemLifoMarkHandle mark;
    DWORD nbytes;
    DWORD structtype;

    mark = MemLifoPushMark(&ticP->memlifo);
    res = TwapiGetArgsEx(ticP, objc, objv, GETHANDLET(hprov, HCRYPTPROV),
                         GETINT(keyspec), GETINT(enctype),
                         GETINT(structtype),
                         GETOBJ(certinfoObj), GETOBJ(algidObj),
                         ARGEND);
    if (res != TCL_OK)
        goto vamoose;

    if ((DWORD_PTR) structtype != (DWORD_PTR) X509_CERT_TO_BE_SIGNED) {
        res = TwapiReturnError(interp, TWAPI_UNSUPPORTED_TYPE);
        goto vamoose;
    }

    res = ParseCRYPT_ALGORITHM_IDENTIFIER(ticP, algidObj, &algid);
    if (res != TCL_OK)
        goto vamoose;

    res = ParseCERT_INFO(ticP, certinfoObj, &ci);
    if (res != TCL_OK)
        goto vamoose;
    
    if (! CryptSignAndEncodeCertificate(hprov, keyspec, enctype,
                                        X509_CERT_TO_BE_SIGNED, &ci,
                                        &algid, NULL, NULL, &nbytes))
        res = TwapiReturnSystemError(ticP->interp);
    else {
        encodedObj = ObjFromByteArray(NULL, nbytes);
        if (CryptSignAndEncodeCertificate(hprov, keyspec, enctype,
                                          X509_CERT_TO_BE_SIGNED, &ci,
                                          &algid, NULL,
                                          ObjToByteArray(encodedObj, NULL),
                                          &nbytes)) {
            Tcl_SetByteArrayLength(encodedObj, nbytes);
            ObjSetResult(ticP->interp, encodedObj);
        } else
            res = TwapiReturnSystemError(ticP->interp);
    }

vamoose:                       
    MemLifoPopMark(mark);
    return res;
}

BOOL WINAPI TwapiCertEnumSystemStoreCallback(
    const void *storeP,
    DWORD flags,
    PCERT_SYSTEM_STORE_INFO storeinfoP,
    void *reserved,
    Tcl_Obj *listObj
)
{
    Tcl_Obj *objs[2];

    if (flags & CERT_SYSTEM_STORE_RELOCATE_FLAG)
        return FALSE;     /* We do not know how to handle this currently */

    objs[0] = ObjFromUnicode(storeP);
    objs[1] = ObjFromDWORD(flags);
    ObjAppendElement(NULL, listObj, ObjNewList(2, objs));
    return TRUE;          /* Continue iteration */
}

BOOL WINAPI TwapiCertEnumSystemStoreLocationCallback(
    const void *locationP,
    DWORD flags,
    void *reserved,
    Tcl_Obj *listObj
)
{
    Tcl_Obj *objs[2];

    if (flags & CERT_SYSTEM_STORE_RELOCATE_FLAG)
        return FALSE;     /* We do not know how to handle this currently */

    objs[0] = ObjFromUnicode(locationP);
    objs[1] = ObjFromDWORD(flags);
    ObjAppendElement(NULL, listObj, ObjNewList(2, objs));
    return TRUE;          /* Continue iteration */
}

BOOL WINAPI TwapiCertEnumPhysicalStoreCallback(
    const void *system_storeP,
    DWORD flags,
    LPCWSTR store_nameP,
    PCERT_PHYSICAL_STORE_INFO storeinfoP,
    void *reserved,
    Tcl_Obj *listObj
)
{
    Tcl_Obj *objs[4];
    Tcl_Obj *infoObjs[6];

    if (flags & CERT_SYSTEM_STORE_RELOCATE_FLAG)
        return FALSE;     /* We do not know how to handle this currently */
    
    objs[0] = ObjFromUnicode(system_storeP);
    objs[1] = ObjFromDWORD(flags);
    objs[2] = ObjFromUnicode(store_nameP);

    infoObjs[0] = ObjFromString(storeinfoP->pszOpenStoreProvider);
    infoObjs[1] = ObjFromDWORD(storeinfoP->dwOpenEncodingType);
    infoObjs[2] = ObjFromDWORD(storeinfoP->dwOpenFlags);
    infoObjs[3] = ObjFromByteArray(storeinfoP->OpenParameters.pbData,
                                  storeinfoP->OpenParameters.cbData);
    infoObjs[4] = ObjFromDWORD(storeinfoP->dwFlags);
    infoObjs[5] = ObjFromDWORD(storeinfoP->dwPriority);
    objs[3] = ObjNewList(6, infoObjs);

    ObjAppendElement(NULL, listObj, ObjNewList(4, objs));
    return TRUE;          /* Continue iteration */
}

static TCL_RESULT Twapi_CertGetCertificateChainObjCmd(TwapiInterpContext *ticP, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    HCERTCHAINENGINE hce;
    CERT_CONTEXT *certP;
    Tcl_Obj *paramObj, *ftObj;
    FILETIME  ft, *ftP;
    HCERTSTORE hstore;
    DWORD flags;
    MemLifoMarkHandle mark;
    TCL_RESULT res;
    CERT_CHAIN_PARA chain_params;
    PCCERT_CHAIN_CONTEXT chainP;

    if (TwapiGetArgs(interp, objc-1, objv+1,
                     GETHANDLET(hce, HCERTCHAINENGINE),
                     GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                     GETOBJ(ftObj),
                     GETVERIFIEDORNULL(hstore, HCERTSTORE, CertCloseStore),
                     GETOBJ(paramObj), GETINT(flags),
                     ARGEND) != TCL_OK)
        return TCL_ERROR;
                     
    if (ObjToFILETIME(NULL, ftObj, &ft) == TCL_OK)
        ftP = &ft;
    else {
        if (ObjCharLength(ftObj) != 0)
            return TwapiReturnErrorMsg(interp, TWAPI_INVALID_ARGS, "Invalid time format");
        ftP = NULL;
    }

    mark = MemLifoPushMark(&ticP->memlifo);
    res = ParseCERT_CHAIN_PARA(ticP, paramObj, &chain_params);
    if (res == TCL_OK) {
        if (CertGetCertificateChain(hce, certP, ftP, hstore, &chain_params, flags, NULL, &chainP)) {
            /* TBD - do we need to register and verify this pointer ? */
            ObjSetResult(ticP->interp, ObjFromOpaque((void*)chainP, "CERT_CHAIN_CONTEXT*"));
        } else
            res = TwapiReturnSystemError(ticP->interp);
    }
    
    MemLifoPopMark(mark);
    return res;
}

static TCL_RESULT Twapi_CertVerifyChainPolicySSLObjCmd(TwapiInterpContext *ticP, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    PCERT_CHAIN_POLICY_PARA policy_paramP;
    CERT_CHAIN_POLICY_STATUS policy_status;
    CERT_CHAIN_CONTEXT *chainP;
    Tcl_Obj *paramObj;
    TCL_RESULT res;
    MemLifoMarkHandle mark;

    if (TwapiGetArgs(interp, objc-1, objv+1,
                     GETHANDLET(chainP, CERT_CHAIN_CONTEXT*),
                     GETOBJ(paramObj), ARGEND) != TCL_OK)
        return TCL_ERROR;

    mark = MemLifoPushMark(&ticP->memlifo);
    res = ParseCERT_CHAIN_POLICY_PARA_SSL(ticP, paramObj, &policy_paramP);
    if (res == TCL_OK) {
        ZeroMemory(&policy_status, sizeof(policy_status));
        policy_status.cbSize = sizeof(policy_status);
        if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL,
                                             chainP,
                                             policy_paramP,
                                             &policy_status)) {
            ObjSetResult(interp, ObjFromLong(policy_status.dwError));
        } else
            res = TwapiReturnSystemError(interp);
    }

    MemLifoPopMark(mark);
    return res;
}


static int Twapi_CryptoCallObjCmd(ClientData clientdata, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    TwapiResult result;
    DWORD dw, dw2, dw3;
    DWORD_PTR dwp;
    LPVOID pv;
    LPWSTR s1;
    LPSTR  cP;
    struct _CRYPTOAPI_BLOB blob;
    PCCERT_CONTEXT certP, cert2P;
    void *bufP;
    DWORD buf_sz;
    Tcl_Obj *s1Obj, *s2Obj;
    BOOL bval;
    int func = PtrToInt(clientdata);
    Tcl_Obj *objs[3];
    TCL_RESULT res;

    --objc;
    ++objv;

    TWAPI_ASSERT(sizeof(HCRYPTPROV) <= sizeof(pv));
    TWAPI_ASSERT(sizeof(HCRYPTKEY) <= sizeof(pv));
    TWAPI_ASSERT(sizeof(dwp) <= sizeof(void*));

    result.type = TRT_BADFUNCTIONCODE;
    switch (func) {
    case 10000: // CryptAcquireContext
        if (TwapiGetArgs(interp, objc, objv,
                         GETOBJ(s1Obj), GETOBJ(s2Obj), GETINT(dw), GETINT(dw2),
                         ARGEND) != TCL_OK)
            return TCL_ERROR;
        if (CryptAcquireContextW(&dwp,
                                 ObjToLPWSTR_NULL_IF_EMPTY(s1Obj),
                                 ObjToLPWSTR_NULL_IF_EMPTY(s2Obj),
                                 dw, dw2)) {
            if (dw2 & CRYPT_DELETEKEYSET)
                result.type = TRT_EMPTY;
            else {
                TwapiResult_SET_PTR(result, HCRYPTPROV, (void*)dwp);
            }
        } else {
            result.type = TRT_GETLASTERROR;
        }
        break;

    case 10001: // CryptReleaseContext
        if (TwapiGetArgs(interp, objc, objv,
                         GETHANDLET(pv, HCRYPTPROV),
                         ARGUSEDEFAULT, GETINT(dw), ARGEND) != TCL_OK)
            return TCL_ERROR;
        result.value.ival = CryptReleaseContext((HCRYPTPROV)pv, dw);
        result.type = TRT_EXCEPTION_ON_FALSE;
        break;

    case 10002: // CryptGetProvParam
        if (TwapiGetArgs(interp, objc, objv,
                         GETHANDLET(pv, HCRYPTPROV),
                         GETINT(dw), GETINT(dw2), ARGEND) != TCL_OK)
            return TCL_ERROR;
        return Twapi_CryptGetProvParam(interp, (HCRYPTPROV) pv, dw, dw2);

    case 10003: // CertOpenSystemStore
        if (objc != 1)
            return TwapiReturnError(interp, TWAPI_BAD_ARG_COUNT);
        pv = CertOpenSystemStoreW(0, ObjToUnicode(objv[0]));
        /* CertCloseStore does not check ponter validity! So do ourselves*/
        if (TwapiRegisterPointer(interp, pv, CertCloseStore) != TCL_OK)
            Tcl_Panic("Failed to register pointer: %s", Tcl_GetStringResult(interp));
        TwapiResult_SET_NONNULL_PTR(result, HCERTSTORE, pv);
        break;

    case 10004: // CertDeleteCertificateFromStore
        if (TwapiGetArgs(interp, objc, objv,
                         GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext), ARGEND) != TCL_OK)
            return TCL_ERROR;
        /* Unregister previous context since the next call will free it,
           EVEN ON FAILURES */
        if (TwapiUnregisterCertPointer(interp, certP) != TCL_OK)
            return TCL_ERROR;
        result.type = TRT_EXCEPTION_ON_FALSE;
        result.value.ival = CertDeleteCertificateFromStore(certP);
        break;

    case 10005: // Twapi_SetCertContextKeyProvInfo
        return Twapi_SetCertContextKeyProvInfo(interp, objc, objv);

    case 10006: // CertEnumCertificatesInStore
        if (TwapiGetArgs(interp, objc, objv,
                         GETVERIFIEDPTR(pv, HCERTSTORE, CertCloseStore),
                         GETPTR(certP, CERT_CONTEXT*), ARGEND) != TCL_OK)
            return TCL_ERROR;
        /* Unregister previous context since the next call will free it */
        if (certP &&
            TwapiUnregisterCertPointer(interp, certP) != TCL_OK)
            return TCL_ERROR;
        certP = CertEnumCertificatesInStore(pv, certP);
        if (certP) {
            TwapiRegisterCertPointer(interp, certP);
            TwapiResult_SET_NONNULL_PTR(result, CERT_CONTEXT*, (void*)certP);
        } else {
            result.value.ival = GetLastError();
            if (result.value.ival == CRYPT_E_NOT_FOUND ||
                result.value.ival == ERROR_NO_MORE_FILES)
                result.type = TRT_EMPTY;
            else
                result.type = TRT_GETLASTERROR;
        }
        break;
    case 10007: // CertEnumCertificateContextProperties
        if (TwapiGetArgs(interp, objc, objv,
                         GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                         GETINT(dw), ARGEND) != TCL_OK)
            return TCL_ERROR;
        result.type = TRT_DWORD;
        result.value.ival = CertEnumCertificateContextProperties(certP, dw);
        break;

    case 10008: // CertGetCertificateContextProperty
        if (TwapiGetArgs(interp, objc, objv,
                         GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                         GETINT(dw), ARGUSEDEFAULT, GETINT(dw2), ARGEND) != TCL_OK)
            return TCL_ERROR;
        return Twapi_CertGetCertificateContextProperty(interp, certP, dw, dw2);

    case 10009: // CryptDestroyKey
        if (TwapiGetArgs(interp, objc, objv,
                         GETPTR(pv, HCRYPTKEY),
                         ARGEND) != TCL_OK)
            return TCL_ERROR;
        result.type = TRT_EXCEPTION_ON_FALSE;
        result.value.ival = CryptDestroyKey((HCRYPTKEY) pv);
        break;
            
    case 10010: // CryptGenKey
        if (TwapiGetArgs(interp, objc, objv,
                         GETHANDLET(pv, HCRYPTPROV),
                         GETINT(dw), GETINT(dw2), ARGEND) != TCL_OK)
            return TCL_ERROR;
        if (CryptGenKey((HCRYPTPROV) pv, dw, dw2, &dwp)) {
            TwapiResult_SET_PTR(result, HCRYPTKEY, (void*)dwp);
        } else
            result.type = TRT_GETLASTERROR;
        break;

    case 10011: // CertStrToName
        if (TwapiGetArgs(interp, objc, objv, GETOBJ(s1Obj), ARGUSEDEFAULT,
                         GETINT(dw), ARGEND) != TCL_OK)
            return TCL_ERROR;
        result.type = TRT_GETLASTERROR;
        dw2 = 0;
        s1 = ObjToUnicode(s1Obj); /* Do AFTER extracting other args above */
        if (CertStrToNameW(X509_ASN_ENCODING, s1,
                           dw, NULL, NULL, &dw2, NULL)) {
            result.value.obj = ObjFromByteArray(NULL, dw2);
            if (CertStrToNameW(X509_ASN_ENCODING, s1, dw, NULL,
                               ObjToByteArray(result.value.obj, &dw2),
                               &dw2, NULL)) {
                Tcl_SetByteArrayLength(result.value.obj, dw2);
                result.type = TRT_OBJ;
            } else {
                Tcl_DecrRefCount(result.value.obj);
            }
        }
        break;

    case 10012: // CertNameToStr
        if (TwapiGetArgs(interp, objc, objv, ARGSKIP, GETINT(dw), ARGEND)
            != TCL_OK)
            return TCL_ERROR;
        blob.pbData = ObjToByteArray(objv[0], &blob.cbData);
        dw2 = CertNameToStrW(X509_ASN_ENCODING, &blob, dw, NULL, 0);
        result.value.unicode.str = TwapiAlloc(dw2*sizeof(WCHAR));
        result.value.unicode.len = CertNameToStrW(X509_ASN_ENCODING, &blob, dw, result.value.unicode.str, dw2) - 1;
        result.type = TRT_UNICODE_DYNAMIC;
        break;

    case 10013: // CertGetNameString
        if (TwapiGetArgs(interp, objc, objv,
                         GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                         GETINT(dw), GETINT(dw2), ARGSKIP, ARGEND) != TCL_OK)
            return TCL_ERROR;
            
        return TwapiCertGetNameString(interp, certP, dw, dw2, objv[3]);

    case 10014: // CertFreeCertificateContext
        if (TwapiGetArgs(interp, objc, objv,
                         GETPTR(certP, CERT_CONTEXT*), ARGEND) != TCL_OK ||
            TwapiUnregisterCertPointer(interp, certP) != TCL_OK)
            return TCL_ERROR;
        TWAPI_ASSERT(certP);
        result.type = TRT_EMPTY;
        CertFreeCertificateContext(certP);
        break;

    case 10015: // Twapi_CertGetEncoded
        if (TwapiGetArgs(interp, objc, objv,
                         GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext), ARGEND) != TCL_OK)
            return TCL_ERROR;
        if (certP->pbCertEncoded && certP->cbCertEncoded) {
            objs[0] = ObjFromDWORD(certP->dwCertEncodingType);
            objs[1] = ObjFromByteArray(certP->pbCertEncoded, certP->cbCertEncoded);
            result.value.objv.objPP = objs;
            result.value.objv.nobj = 2;
            result.type = TRT_OBJV;
        }
        /* else empty result */

        break;
            
    case 10016: // CertUnregisterSystemStore
        /* This command is there to primarily clean up mistakes in testing */
        if (TwapiGetArgs(interp, objc, objv,
                         GETOBJ(s1Obj), GETINT(dw), ARGEND) != TCL_OK)
            return TCL_ERROR;
        result.type = TRT_EXCEPTION_ON_FALSE;
        result.value.ival = CertUnregisterSystemStore(ObjToUnicode(s1Obj), dw);
        break;
    case 10017:
        if (TwapiGetArgs(interp, objc, objv,
                         GETHANDLET(pv, HCERTSTORE), ARGUSEDEFAULT,
                         GETINT(dw), ARGEND) != TCL_OK ||
            TwapiUnregisterPointer(interp, pv, CertCloseStore) != TCL_OK)
            return TCL_ERROR;

        result.type = TRT_BOOL;
        result.value.bval = CertCloseStore(pv, dw);
        if (result.value.bval == FALSE) {
            if (GetLastError() != CRYPT_E_PENDING_CLOSE)
                result.type = TRT_GETLASTERROR;
        }
        break;

    case 10018: // CryptGetUserKey
        if (TwapiGetArgs(interp, objc, objv,
                         GETHANDLET(pv, HCRYPTPROV),
                         GETINT(dw), ARGEND) != TCL_OK)
            return TCL_ERROR;
        if (CryptGetUserKey((HCRYPTPROV) pv, dw, &dwp)) {
            TwapiResult_SET_PTR(result, HCRYPTKEY, (void*)dwp);
        } else
            result.type = TRT_GETLASTERROR;
        break;

    case 10019: // CryptSetProvParam
        if (TwapiGetArgs(interp, objc, objv,
                         GETHANDLET(pv, HCRYPTPROV),
                         GETINT(dw), GETINT(dw2), ARGSKIP, ARGEND) != TCL_OK)
            return TCL_ERROR;
        return Twapi_CryptSetProvParam(interp, (HCRYPTPROV) pv, dw, dw2, objv[3]);

    case 10020: // CertOpenStore
        return Twapi_CertOpenStore(interp, objc, objv);

    case 10021: // PFXExportCertStoreEx
        return Twapi_PFXExportCertStoreEx(interp, objc, objv);

    case 10022: // CertAddCertificateContextToStore
        if (TwapiGetArgs(interp, objc, objv,
                         GETVERIFIEDPTR(pv, HCERTSTORE, CertCloseStore),
                         GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                         GETINT(dw), ARGEND) != TCL_OK)
            return TCL_ERROR;
        if (!CertAddCertificateContextToStore(pv, certP, dw, &certP))
            result.type = TRT_GETLASTERROR;
        else {
            if (TwapiRegisterPointer(interp, certP, CertFreeCertificateContext) != TCL_OK)
                Tcl_Panic("Failed to register pointer: %s", Tcl_GetStringResult(interp));
            TwapiResult_SET_NONNULL_PTR(result, CERT_CONTEXT*, (void*)certP);
        }
        break;

    case 10023:  // CryptExportPublicKeyInfoEx
        if (TwapiGetArgs(interp, objc, objv,
                         GETHANDLET(pv, HCRYPTPROV),
                         GETINT(dw), // keyspec
                         GETINT(dw2), // enctype
                         GETASTR(cP), // publickeyobjid
                         GETINT(dw3), // flags
                         ARGEND) != TCL_OK)
            return TCL_ERROR;
        buf_sz = 0;
        if (!CryptExportPublicKeyInfoEx((HCRYPTPROV)pv, dw, dw2, cP, dw3, NULL, NULL, &buf_sz)) {
            result.type = TRT_GETLASTERROR;
            break;
        }
        bufP = TwapiAlloc(buf_sz);
        if (!CryptExportPublicKeyInfoEx((HCRYPTPROV)pv, dw, dw2, cP, dw3, NULL, bufP, &buf_sz)) {
            TwapiReturnSystemError(interp);
            TwapiFree(bufP);
            return TCL_ERROR;
        }
        if (TwapiRegisterPointer(interp, bufP, TwapiAlloc) != TCL_OK)
            Tcl_Panic("Failed to register pointer: %s", Tcl_GetStringResult(interp));
        TwapiResult_SET_NONNULL_PTR(result, CERT_PUBLIC_KEY_INFO*, bufP);
        break;

    case 10024:
        CHECK_NARGS(interp, objc, 2);
        CHECK_INTEGER_OBJ(interp, dw, objv[0]);
        if (dw & CERT_SYSTEM_STORE_RELOCATE_FLAG)
            return TwapiReturnErrorMsg(interp, TWAPI_INVALID_ARGS, "RELOCATE flag not supported.");
        result.value.obj = ObjNewList(0, NULL);
        if (CertEnumSystemStore(dw, ObjToLPWSTR_NULL_IF_EMPTY(objv[1]),
                                result.value.obj,
                                TwapiCertEnumSystemStoreCallback)) {
            result.type = TRT_OBJ;
        } else {
            TwapiReturnSystemError(interp);
            Tcl_DecrRefCount(result.value.obj);
            return TCL_ERROR;
        }
        break;

    case 10025:
        CHECK_NARGS(interp, objc, 2);
        CHECK_INTEGER_OBJ(interp, dw, objv[1]);
        if (dw & CERT_SYSTEM_STORE_RELOCATE_FLAG)
            return TwapiReturnErrorMsg(interp, TWAPI_INVALID_ARGS, "RELOCATE flag not supported.");
        result.value.obj = ObjNewList(0, NULL);
        if (CertEnumPhysicalStore(ObjToUnicode(objv[0]), dw,
                                result.value.obj,
                                TwapiCertEnumPhysicalStoreCallback)) {
            result.type = TRT_OBJ;
        } else {
            TwapiReturnSystemError(interp);
            Tcl_DecrRefCount(result.value.obj);
            return TCL_ERROR;
        }
        break;

    case 10026:
        CHECK_NARGS(interp, objc, 1);
        CHECK_INTEGER_OBJ(interp, dw, objv[0]);
        if (dw & CERT_SYSTEM_STORE_RELOCATE_FLAG)
            return TwapiReturnErrorMsg(interp, TWAPI_INVALID_ARGS, "RELOCATE flag not supported.");
        result.value.obj = ObjNewList(0, NULL);
        if (CertEnumSystemStoreLocation(dw, result.value.obj,
                                        TwapiCertEnumSystemStoreLocationCallback)) {
            result.type = TRT_OBJ;
        } else {
            TwapiReturnSystemError(interp);
            Tcl_DecrRefCount(result.value.obj);
            return TCL_ERROR;
        }
        break;

    case 10027:
        if (TwapiGetArgs(interp, objc, objv,
                         GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                         GETINT(dw), ARGSKIP, ARGEND) != TCL_OK)
            return TCL_ERROR;
        /* We only allow the following flags */
        if (dw & ~(CRYPT_ACQUIRE_CACHE_FLAG|CRYPT_ACQUIRE_COMPARE_KEY_FLAG|CRYPT_ACQUIRE_SILENT_FLAG|CRYPT_ACQUIRE_USE_PROV_INFO_FLAG)) {
            return TwapiReturnErrorMsg(interp, TWAPI_INVALID_ARGS, "Invalid flags");
        }
        if (CryptAcquireCertificatePrivateKey(certP,dw,NULL,&dwp,&dw2,&bval)) {
            objs[0] = ObjFromOpaque((void*)dwp, "HCRYPTPROV");
            objs[1] = ObjFromLong(dw2);
            objs[2] = ObjFromBoolean(bval);
            result.value.objv.objPP = objs;
            result.value.objv.nobj = 3;
            result.type = TRT_OBJV;
        } else
            result.type = TRT_GETLASTERROR;
        break;

    case 10028: // CertGetEnhancedKeyUsage
        if (TwapiGetArgs(interp, objc, objv,
                         GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                         GETINT(dw), ARGEND) != TCL_OK)
            return TCL_ERROR;
        dw2 = 0;
        result.type = TRT_GETLASTERROR;
        if (CertGetEnhancedKeyUsage(certP, dw, NULL, &dw2)) {
            pv = TwapiAlloc(dw2);
            if (CertGetEnhancedKeyUsage(certP, dw, pv, &dw2)) {
                CERT_ENHKEY_USAGE *ceuP = pv;
                result.type = TRT_OBJ;
                if (ceuP->cUsageIdentifier)
                    result.value.obj = ObjFromArgvA(ceuP->cUsageIdentifier,
                                                    ceuP->rgpszUsageIdentifier);
                else {
                    if (GetLastError() == CRYPT_E_NOT_FOUND) {
                        /* Extension not present -> all uses valid */
                        result.value.obj = STRING_LITERAL_OBJ("*");
                    } else /* No valid uses */
                        result.type = TRT_EMPTY;
                }
            }
            TwapiFree(pv);
        }
        break;

    case 10029: // Twapi_CertStoreCommit
        if (TwapiGetArgs(interp, objc, objv,
                         GETVERIFIEDPTR(pv, HCERTSTORE, CertCloseStore),
                         GETINT(dw), ARGEND) != TCL_OK)
            return TCL_ERROR;
        if (CertControlStore(pv, dw ? CERT_STORE_CTRL_COMMIT_FORCE_FLAG : 0,
                               CERT_STORE_CTRL_COMMIT, NULL))
            result.type = TRT_EMPTY;
        else
            result.type = TRT_GETLASTERROR;
        break;

    case 10030: // Twapi_CertGetIntendedKeyUsage
        if (TwapiGetArgs(interp, objc, objv, GETINT(dw),
                         GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                         ARGEND) != TCL_OK)
            return TCL_ERROR;
        else {
            BYTE buf[2];
            /* We do not currently support more than 2 bytes at Tcl level */
            dw = CertGetIntendedKeyUsage(dw, certP->pCertInfo, buf, ARRAYSIZE(buf));
            if (dw) {
                result.value.binary.p = buf;
                result.value.binary.len = ARRAYSIZE(buf);
                result.type = TRT_BINARY;
            } else {
                if (GetLastError() == 0)
                    result.type = TRT_EMPTY;
                else
                    result.type = TRT_GETLASTERROR;
            }
        }
        break;

    case 10031: // CertGetIssuerCertificateFromStore
        cert2P = NULL;
        res = TwapiGetArgs(interp, objc, objv,
                           GETVERIFIEDPTR(pv, HCERTSTORE, CertCloseStore),
                           GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                           GETVERIFIEDORNULL(cert2P, CERT_CONTEXT*, CertFreeCertificateContext),
                           GETINT(dw), ARGEND);
        
        if (cert2P) {
            /* CertGetIssuerCertificateFromStore frees cert2P */
            if (TwapiUnregisterCertPointer(interp, cert2P) != TCL_OK)
                return TCL_ERROR; /* Bad pointer, don't do anything more */
        }
        if (res != TCL_OK) {
            if (cert2P)
                CertFreeCertificateContext(cert2P); /* That's what we have
                                                       guaranteed caller */
            return res;
        }
        cert2P = CertGetIssuerCertificateFromStore(pv, certP, cert2P, &dw);
        if (cert2P) {
            TwapiRegisterCertPointer(interp, cert2P);
            objs[0] = ObjFromOpaque((void*)cert2P, "CERT_CONTEXT*");
            objs[1] = ObjFromDWORD(dw);
            result.type= TRT_OBJV;
            result.value.objv.objPP = objs;
            result.value.objv.nobj = 2;
        } else {
            result.type =
                GetLastError() == CRYPT_E_NOT_FOUND ?
                TRT_EMPTY : TRT_GETLASTERROR;
        }
        break;

    case 10032:
        if (TwapiGetArgs(interp, objc, objv,
                         GETHANDLET(pv, CERT_CHAIN_CONTEXT*),
                         ARGEND) != 0)
            return TCL_ERROR;
        CertFreeCertificateChain(pv);
        result.type = TRT_EMPTY;
        break;
    case 10033: // CertFindExtension
        res = TwapiGetArgs(interp, objc, objv,
                           GETVERIFIEDPTR(certP, CERT_CONTEXT*, CertFreeCertificateContext),
                           GETASTR(cP), ARGEND);
        if (res != TCL_OK)
            return res;
        if (certP->pCertInfo->cExtension && certP->pCertInfo->rgExtension) {
            CERT_EXTENSION *extP =
                CertFindExtension(cP,
                                  certP->pCertInfo->cExtension,
                                  certP->pCertInfo->rgExtension);
            if (extP == NULL)
                result.type = TRT_EMPTY;
            else {
                if (TwapiCryptDecodeObject(interp, extP->pszObjId,
                                           extP->Value.pbData,
                                           extP->Value.cbData, &objs[2])
                    != TCL_OK)
                    return TCL_ERROR;
                objs[0] = ObjFromString(extP->pszObjId);
                objs[1] = ObjFromBoolean(extP->fCritical);
                result.value.objv.objPP = objs;
                result.value.objv.nobj = 3;
                result.type = TRT_OBJV;
            }
        }
        break;
    }

    return TwapiSetResult(interp, &result);
}

static int TwapiCryptoInitCalls(Tcl_Interp *interp, TwapiInterpContext *ticP)
{
    static struct fncode_dispatch_s CryptoDispatch[] = {
        DEFINE_FNCODE_CMD(CryptAcquireContext, 10000),
        DEFINE_FNCODE_CMD(CryptReleaseContext, 10001),
        DEFINE_FNCODE_CMD(CryptGetProvParam, 10002),
        DEFINE_FNCODE_CMD(CertOpenSystemStore, 10003),
        DEFINE_FNCODE_CMD(cert_store_delete_certificate, 10004), // Doc TBD
        DEFINE_FNCODE_CMD(Twapi_SetCertContextKeyProvInfo, 10005),
        DEFINE_FNCODE_CMD(CertEnumCertificatesInStore, 10006),
        DEFINE_FNCODE_CMD(CertEnumCertificateContextProperties, 10007),
        DEFINE_FNCODE_CMD(CertGetCertificateContextProperty, 10008),
        DEFINE_FNCODE_CMD(crypt_key_free, 10009), // Doc TBD
        DEFINE_FNCODE_CMD(CryptGenKey, 10010),
        DEFINE_FNCODE_CMD(CertStrToName, 10011),
        DEFINE_FNCODE_CMD(CertNameToStr, 10012),
        DEFINE_FNCODE_CMD(CertGetNameString, 10013),
        DEFINE_FNCODE_CMD(cert_free, 10014),
        DEFINE_FNCODE_CMD(Twapi_CertGetEncoded, 10015),
        DEFINE_FNCODE_CMD(CertUnregisterSystemStore, 10016),
        DEFINE_FNCODE_CMD(CertCloseStore, 10017),
        DEFINE_FNCODE_CMD(CryptGetUserKey, 10018),
        DEFINE_FNCODE_CMD(CryptSetProvParam, 10019),
        DEFINE_FNCODE_CMD(CertOpenStore, 10020),
        DEFINE_FNCODE_CMD(PFXExportCertStoreEx, 10021),
        DEFINE_FNCODE_CMD(CertAddCertificateContextToStore, 10022),
        DEFINE_FNCODE_CMD(CryptExportPublicKeyInfoEx, 10023),
        DEFINE_FNCODE_CMD(CertEnumSystemStore, 10024),
        DEFINE_FNCODE_CMD(CertEnumPhysicalStore, 10025),
        DEFINE_FNCODE_CMD(CertEnumSystemStoreLocation, 10026),
        DEFINE_FNCODE_CMD(CryptAcquireCertificatePrivateKey, 10027),
        DEFINE_FNCODE_CMD(CertGetEnhancedKeyUsage, 10028),
        DEFINE_FNCODE_CMD(Twapi_CertStoreCommit, 10029),
        DEFINE_FNCODE_CMD(Twapi_CertGetIntendedKeyUsage, 10030),
        DEFINE_FNCODE_CMD(CertGetIssuerCertificateFromStore, 10031),
        DEFINE_FNCODE_CMD(CertFreeCertificateChain, 10032),
        DEFINE_FNCODE_CMD(CertFindExtension, 10033),
    };

    static struct tcl_dispatch_s TclDispatch[] = {
        DEFINE_TCL_CMD(CertCreateSelfSignCertificate, Twapi_CertCreateSelfSignCertificate),
        DEFINE_TCL_CMD(CryptSignAndEncodeCertificate, Twapi_CryptSignAndEncodeCertObjCmd),
        DEFINE_TCL_CMD(CertFindCertificateInStore, Twapi_CertFindCertificateInStoreObjCmd),
        DEFINE_TCL_CMD(CertGetCertificateChain, Twapi_CertGetCertificateChainObjCmd),
        DEFINE_TCL_CMD(Twapi_CertVerifyChainPolicySSL, Twapi_CertVerifyChainPolicySSLObjCmd),
    };

    TwapiDefineFncodeCmds(interp, ARRAYSIZE(CryptoDispatch), CryptoDispatch, Twapi_CryptoCallObjCmd);
    TwapiDefineTclCmds(interp, ARRAYSIZE(TclDispatch), TclDispatch, ticP);

    return TwapiSspiInitCalls(interp, ticP);
}


#ifndef TWAPI_SINGLE_MODULE
BOOL WINAPI DllMain(HINSTANCE hmod, DWORD reason, PVOID unused)
{
    if (reason == DLL_PROCESS_ATTACH)
        gModuleHandle = hmod;
    return TRUE;
}
#endif

/* Main entry point */
#ifndef TWAPI_SINGLE_MODULE
__declspec(dllexport) 
#endif
int Twapi_crypto_Init(Tcl_Interp *interp)
{
    static TwapiModuleDef gModuleDef = {
        MODULENAME,
        TwapiCryptoInitCalls,
        NULL
    };

    /* IMPORTANT */
    /* MUST BE FIRST CALL as it initializes Tcl stubs */
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
        return TCL_ERROR;
    }

    return TwapiRegisterModule(interp, MODULE_HANDLE, &gModuleDef, DEFAULT_TIC) ? TCL_OK : TCL_ERROR;
}

