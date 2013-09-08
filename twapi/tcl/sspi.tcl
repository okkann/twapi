#
# Copyright (c) 2007-2013, Ashok P. Nadkarni
# All rights reserved.
#
# See the file LICENSE for license

namespace eval twapi {


    # Holds SSPI security contexts indexed by a handle
    # Each element is a dict with the following keys:
    #   State - state of the security context - see sspi_step
    #   Handle - the Win32 SecHandle for the context
    #   Input - Pending input from remote end to be passed in to
    #    SSPI provider (only valid for streams)
    #   Output - list of SecBuffers that contain data to be sent
    #    to remote end during a SSPI negotiation
    #   Inattr - requested context attributes
    #   Outattr - context attributes returned from service provider
    #    (currently not used)
    #   Expiration - time when context will expire
    #   Ctxtype - client, server
    #   Target -
    #   Datarep - data representation format
    #   Credentials - handle for credentials to pass to sspi provider
    variable _sspi_state
    array set _sspi_state {}

    proc* _init_security_context_syms {} {
        variable _server_security_context_syms
        variable _client_security_context_syms
        variable _secpkg_capability_syms


        # Symbols used for mapping server security context flags
        array set _server_security_context_syms {
            confidentiality      0x10
            connection           0x800
            delegate             0x1
            extendederror        0x8000
            identify             0x80000
            integrity            0x20000
            mutualauth           0x2
            replaydetect         0x4
            sequencedetect       0x8
            stream               0x10000
        }

        # Symbols used for mapping client security context flags
        array set _client_security_context_syms {
            confidentiality      0x10
            connection           0x800
            delegate             0x1
            extendederror        0x4000
            identify             0x20000
            integrity            0x10000
            manualvalidation     0x80000
            mutualauth           0x2
            replaydetect         0x4
            sequencedetect       0x8
            stream               0x8000
            usesessionkey        0x20
            usesuppliedcreds     0x80
        }

        # Symbols used for mapping security package capabilities
        array set _secpkg_capability_syms {
            integrity                   0x00000001
            privacy                     0x00000002
            tokenonly                  0x00000004
            datagram                    0x00000008
            connection                  0x00000010
            multirequired              0x00000020
            clientonly                 0x00000040
            extendederror              0x00000080
            impersonation               0x00000100
            acceptwin32name           0x00000200
            stream                      0x00000400
            negotiable                  0x00000800
            gsscompatible              0x00001000
            logon                       0x00002000
            asciibuffers               0x00004000
            fragment                    0x00008000
            mutualauth                 0x00010000
            delegation                  0x00020000
            readonlywithchecksum      0x00040000
            restrictedtokens           0x00080000
            negoextender               0x00100000
            negotiable2                 0x00200000
            appcontainerpassthrough  0x00400000
            appcontainerchecks  0x00800000
        }
    } {}
}

# Return list of security packages
proc twapi::sspi_enumerate_packages {args} {
    set pkgs [EnumerateSecurityPackages]
    if {[llength $args] == 0} {
        set names [list ]
        foreach pkg $pkgs {
            lappend names [kl_get $pkg Name]
        }
        return $names
    }

    array set opts [parseargs args {
        all capabilities version rpcid maxtokensize name comment
    } -maxleftover 0 -hyphenated]

    _init_security_context_syms
    variable _secpkg_capability_syms
    set retdata {}
    foreach pkg $pkgs {
        set rec {}
        if {$opts(-all) || $opts(-capabilities)} {
            lappend rec -capabilities [_make_symbolic_bitmask [kl_get $pkg fCapabilities] _secpkg_capability_syms]
        }
        foreach {opt field} {-version wVersion -rpcid wRPCID -maxtokensize cbMaxToken -name Name -comment Comment} {
            if {$opts(-all) || $opts($opt)} {
                lappend rec $opt [kl_get $pkg $field]
            }
        }
        dict set recdata [kl_get $pkg Name] $rec
    }
    return $recdata
}


# Return sspi credentials
interp alias {} twapi::sspi_new_credentials {} twapi::sspi_credentials
proc twapi::sspi_credentials {args} {
    array set opts [parseargs args {
        {principal.arg ""}
        {package.arg NTLM}
        {usage.arg both {inbound outbound both}}
        getexpiration
    } -maxleftover 0]

    set creds [AcquireCredentialsHandle $opts(principal) \
                   [dictmap {
                       unisp {Microsoft Unified Security Protocol Provider}
                       ssl {Microsoft Unified Security Protocol Provider}
                       tls {Microsoft Unified Security Protocol Provider}
                   } $opts(package)] \
                   [kl_get {inbound 1 outbound 2 both 3} $opts(usage)] \
                   "" {}]

    if {$opts(getexpiration)} {
        return [kl_create2 {-handle -expiration} $creds]
    } else {
        return [lindex $creds 0]
    }
}

# Frees credentials
proc twapi::sspi_free_credentials {cred} {
    FreeCredentialsHandle $cred
}

# Return a client context
interp alias {} twapi::sspi_client_new_context {} twapi::sspi_client_context
proc twapi::sspi_client_context {cred args} {
    _init_security_context_syms
    variable _client_security_context_syms

    array set opts [parseargs args {
        target.arg
        {datarep.arg network {native network}}
        confidentiality.bool
        connection.bool
        delegate.bool
        extendederror.bool
        identify.bool
        integrity.bool
        manualvalidation.bool
        mutualauth.bool
        replaydetect.bool
        sequencedetect.bool
        stream.bool
        usesessionkey.bool
        usesuppliedcreds.bool
    } -maxleftover 0 -nulldefault]

    set context_flags 0
    foreach {opt flag} [array get _client_security_context_syms] {
        if {$opts($opt)} {
            set context_flags [expr {$context_flags | $flag}]
        }
    }

    set drep [kl_get {native 0x10 network 0} $opts(datarep)]
    return [_construct_sspi_security_context \
                sspiclient#[TwapiId] \
                [InitializeSecurityContext \
                     $cred \
                     "" \
                     $opts(target) \
                     $context_flags \
                     0 \
                     $drep \
                     [list ] \
                     0] \
                client \
                $context_flags \
                $opts(target) \
                $cred \
                $drep \
               ]
}

# Delete a security context
proc twapi::sspi_close_context {ctx} {
    variable _sspi_state
    DeleteSecurityContext [_sspi_context_handle $ctx]
    unset _sspi_state($ctx)
}

# Take the next step in client side authentication
# Returns
#   {done data newctx extradata}
#   {continue data newctx extradata}
interp alias {} twapi::sspi_security_context_next {} twapi::sspi_step
proc twapi::sspi_step {ctx {received ""}} {
    variable _sspi_state

    _sspi_validate_handle $ctx

    dict with _sspi_state($ctx) {
        # Note the dictionary content variables are
        #   State, Handle, Output, Outattr, Expiration,
        #   Ctxtype, Inattr, Target, Datarep, Credentials

        # Append new input to existing input
        append Input $received
        switch -exact -- $State {
            ok {
                # See if there is any data to send.
                set data ""
                foreach buf $Output {
                    # First element is buffer type, which we do not care
                    # Second element is actual data
                    append data [lindex $buf 1]
                }

                set Output {}
                # We return the context handle as third element for backwards
                # compatibility reasons - TBD
                # $Input at this point contains left over input that is
                # actually application data (streaming case).
                # Application should pass this to decrypt commands
                return [list done $data $ctx $Input[set Input ""]]
            }
            continue {
                # Continue with the negotiation
                if {[string length $Input] != 0} {
                    # Pass in received data to SSPI.
                    # Most providers take only the first buffer
                    # but SChannel/UNISP need the second. Since
                    # others don't seem to mind the second buffer
                    # we always include it
                    # 2 -> SECBUFFER_TOKEN, 0 -> SECBUFFER_EMPTY
                    set inbuflist [list [list 2 $Input] [list 0]]
                    if {$Ctxtype eq "client"} {
                        set rawctx [InitializeSecurityContext \
                                        $Credentials \
                                        $Handle \
                                        $Target \
                                        $Inattr \
                                        0 \
                                        $Datarep \
                                        $inbuflist \
                                        0]
                    } else {
                        set rawctx [AcceptSecurityContext \
                                        $Credentials \
                                        $Handle \
                                        $inbuflist \
                                        $Inattr \
                                        $Datarep]
                    }
                    lassign $rawctx State Handle out Outattr Expiration extra
                    lappend Output {*}$out
                    set Input $extra
                    # Will recurse at proc end
                } else {
                    # There was no received data. Return any data
                    # to be sent to remote end
                    set data ""
                    foreach buf $Output {
                        append data [lindex $buf 1]
                    }
                    set Output {}
                    return [list continue $data $ctx ""]
                }
            }
            incomplete_message {
                # Caller has to get more data from remote end
                return [list continue "" $ctx ""]
            }
            incomplete_credentials -
            complete -
            complete_and_continue {
                # TBD
                error "State $State handling not implemented."
            }
        }
    }

    # Recurse to return next state. $extra contains data that has not
    # been processed.
    # This has to be OUTSIDE the [dict with] else it will not
    # see the updated values
    return [sspi_step $ctx]
}

# Return a server context
interp alias {} twapi::sspi_server_new_context {} twapi::sspi_server_context
proc twapi::sspi_server_context {cred clientdata args} {
    _init_security_context_syms
    variable _server_security_context_syms

    array set opts [parseargs args {
        {datarep.arg network {native network}}
        confidentiality.bool
        connection.bool
        delegate.bool
        extendederror.bool
        identify.bool
        integrity.bool
        mutualauth.bool
        replaydetect.bool
        sequencedetect.bool
        stream.bool
    } -maxleftover 0 -nulldefault]

    set context_flags 0
    foreach {opt flag} [array get _server_security_context_syms] {
        if {$opts($opt)} {
            set context_flags [expr {$context_flags | $flag}]
        }
    }

    set drep [kl_get {native 0x10 network 0} $opts(datarep)]
    return [_construct_sspi_security_context \
                sspiserver#[TwapiId] \
                [AcceptSecurityContext \
                     $cred \
                     "" \
                     [list [list 2 $clientdata]] \
                     $context_flags \
                     $drep] \
                server \
                $context_flags \
                "" \
                $cred \
                $drep \
               ]
}


# Get the security context flags after completion of request
proc ::twapi::sspi_get_context_features {ctx} {
    variable _sspi_state

    set ctxh [_sspi_context_handle $ctx]

    _init_security_context_syms

    # We could directly look in the context itself but intead we make
    # an explicit call, just in case they change after initial setup
    set flags [QueryContextAttributes $ctxh 14]

        # Mapping of symbols depends on whether it is a client or server
        # context
    if {[dict get $_sspi_state($ctx) Ctxtype] eq "client"} {
        upvar 0 [namespace current]::_client_security_context_syms syms
    } else {
        upvar 0 [namespace current]::_server_security_context_syms syms
    }

    set result [list -raw $flags]
    foreach {sym flag} [array get syms] {
        lappend result -$sym [expr {($flag & $flags) != 0}]
    }

    return $result
}

# Get the user name for a security context
proc twapi::sspi_get_context_username {ctx} {
    return [QueryContextAttributes [_sspi_context_handle $ctx] 1]
}

# Get the field size information for a security context
# TBD - update for SSL
proc twapi::sspi_get_context_sizes {ctx} {
    set sizes [QueryContextAttributes [_sspi_context_handle $ctx] 0]
    return [twine {-maxtoken -maxsig -blocksize -trailersize} $sizes]
}

# Returns a signature
proc twapi::sspi_sign {ctx data args} {
    array set opts [parseargs args {
        {seqnum.int 0}
        {qop.int 0}
    } -maxleftover 0]

    return [MakeSignature \
                [_sspi_context_handle $ctx] \
                $opts(qop) \
                $data \
                $opts(seqnum)]
}

# Verify signature
proc twapi::sspi_verify {ctx sig data args} {
    array set opts [parseargs args {
        {seqnum.int 0}
    } -maxleftover 0]

    # Buffer type 2 - Token, 1- Data
    return [VerifySignature \
                [_sspi_context_handle $ctx] \
                [list [list 2 $sig] [list 1 $data]] \
                $opts(seqnum)]
}

# Encrypts a data as per a context
# Returns {securitytrailer encrypteddata padding}
proc twapi::sspi_encrypt {ctx data args} {
    array set opts [parseargs args {
        {seqnum.int 0}
        {qop.int 0}
    } -maxleftover 0]

    return [EncryptMessage \
                [_sspi_context_handle $ctx] \
                $opts(qop) \
                $data \
                $opts(seqnum)]
}

proc twapi::sspi_encrypt_stream {ctx data args} {
    variable _sspi_state
    
    set h [_sspi_context_handle $ctx]

    array set opts [parseargs args {
        {qop.int 0}
    } -maxleftover 0]

    set enc ""
    while {[string length $data]} {
        lassign [EncryptStream $h $opts(qop) $data] fragment data
        append enc $fragment
    }

    return $enc
}


# Decrypts a message
proc twapi::sspi_decrypt {ctx sig data padding args} {
    variable _sspi_state
    _sspi_validate_handle $ctx

    array set opts [parseargs args {
        {seqnum.int 0}
    } -maxleftover 0]

    # Buffer type 2 - Token, 1- Data, 9 - padding
    set decrypted [DecryptMessage \
                       [dict get $_sspi_state($ctx) Handle] \
                       [list [list 2 $sig] [list 1 $data] [list 9 $padding]] \
                       $opts(seqnum)]
    set plaintext {}
    # Pick out only the data buffers, ignoring pad buffers and signature
    # Optimize copies by keeping as a list so in the common case of a 
    # single buffer can return it as is. Multiple buffers are expensive
    # because Tcl will shimmer each byte array into a list and then
    # incur additional copies during joining
    foreach buf $decrypted {
        # SECBUFFER_DATA -> 1
        if {[lindex $buf 0] == 1} {
            lappend plaintext [lindex $buf 1]
        }
    }

    if {[llength $plaintext] < 2} {
        return [lindex $plaintext 0]
    } else {
        return [join $plaintext ""]
    }
}

# Decrypts a stream
proc twapi::sspi_decrypt_stream {ctx data} {
    variable _sspi_state
    _sspi_validate_handle $ctx

    return [DecryptStream [_sspi_context_handle $ctx] $data]
}

################################################################
# Utility procs


# Construct a high level SSPI security context structure
# rawctx is context as returned from C level code
proc twapi::_construct_sspi_security_context {id rawctx ctxtype inattr target credentials datarep} {
    variable _sspi_state
    
    set _sspi_state($id) [dict merge [dict create Ctxtype $ctxtype \
                                          Inattr $inattr \
                                          Target $target \
                                          Datarep $datarep \
                                          Credentials $credentials] \
                              [twine \
                                   {State Handle Output Outattr Expiration Input} \
                                   $rawctx]]

    return $id
}

proc twapi::_sspi_validate_handle {ctx} {
    variable _sspi_state

    if {![info exists _sspi_state($ctx)]} {
        badargs! "Invalid SSPI security context handle $ctx" 3
    }
}

proc twapi::_sspi_context_handle {ctx} {
    variable _sspi_state

    if {![info exists _sspi_state($ctx)]} {
        badargs! "Invalid SSPI security context handle $ctx" 3
    }

    return [dict get $_sspi_state($ctx) Handle]
}
