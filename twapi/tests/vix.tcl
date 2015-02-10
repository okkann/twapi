package require TclOO
package require twapi_com

namespace eval vix {
    proc initialize {} {
        variable Vix
        
        if {![info exists Vix]} {
            set Vix [twapi::comobj VixCom.VixLib]
        }
        return
    }

    proc finalize {} {
        variable Vix
        if {[info exists Vix]} {
            $Vix destroy
            unset Vix
        }
    }

    proc vix {} {
        variable Vix
        return $Vix
    }

    proc check_error {err} {
        variable Vix
        if {$err && [$Vix ErrorIndicatesFailure $err]} {
            set msg [$Vix GetErrorText $err [twapi::vt_empty]]
            return -level 1 -code error -errorcode [list VIX $err $msg] $msg
        }
    }

    proc check_path {path} {
        # Path must be absolute path. However, we allow volume-relative
        # since since guest may actually be *ix where volume-relative
        # really is absolute
        if {[file pathtype $path] eq "relative"} {
            return -level 1 -code error -errorcode [list VIX FILE NOTABSOLUTE] "File path $path is a relative file path."
        }
    }

    proc wait_for_result {job {propname VIX_PROPERTY_JOB_RESULT_HANDLE}} {
        twapi::trap {
            set results [twapi::vt_null]; # Must be init'ed
            set err [$job Wait [twapi::safearray i4 [$propname]] results]
            check_error $err

            # $results is a safearray the first element of which
            # will contain the awaited result
            return [lindex [twapi::variant_value $results 0 0 0] 0]
        } finally {
            $job -destroy
        }
    }

    proc wait_for_results {job args} {
        twapi::trap {
            set values {}
            foreach propname $args {
                set results [twapi::vt_null]; # Must be init'ed
                set err [$job Wait [twapi::safearray i4 [$propname]] results]
                check_error $err
                # $results is a safearray the first element of which
                # will contain the awaited result
                lappend values [lindex [twapi::variant_value $results 0 0 0] 0]
            }
        } finally {
            $job -destroy
        }
        return $values
    }

    proc wait_for_completion {job {preserve 0}} {
        twapi::trap {
            set err [$job WaitWithoutResults]
            check_error $err
        } finally {
            if {! $preserve} {
                $job -destroy
            }
        }
        return
    }

    proc wait_for_properties {job args} {
        # args is list of property ids (integer)
        # Returns nested list if more than one arg
        set values {}
        twapi::trap {
            wait_for_completion $job 1
            set count [$job GetNumProperties [lindex $args 0]]
            set proparr [twapi::safearray i4 $args]
            for {set i 0} {$i < $count} {incr i} {
                set results [twapi::vt_null]
                set err [$job GetNthProperties $i $proparr results]
                check_error $err
                if {[llength $args] == 1} {
                    lappend values [lindex [twapi::variant_value $results 0 0 0] 0]
                } else {
                    lappend values [twapi::variant_value $results 0 0 0]
                }
            }
        } finally {
            $job -destroy
        }

        return $values
    }

    proc pick {cond {a 1} {b 0}} {
        return [expr {$cond ? $a : $b}]
    }
}

oo::class create vix::Host {
    variable Opts Host VMs NameCounter
    constructor args {
        # Class representing a host system running VMware software
        #
        # -hosttype HOSTTYPE - specifies the type of VMware host software.
        #   HOSTTYPE must be one of 'workstation' for VMware Workstation
        #   (default), 'workstation_server' for VMware Workstation in shared
        #   mode, 'server' for VMware Server 1.0, 'player' for VMware Player
        #   and 'vi_server' for VMware Server 2.0, vCenter and ESX/ESXi.
        # -hostname HOSTNAME - specifies the name of the host system. Defaults
        #   to the local system. Cannot be specified if HOSTTYPE is 
        #   'workstation' or 'player'. Must be specified in other cases.
        # -port PORTNUMBER - specifies the port number to connect to on the
        #   host. Ignored if -hostname is not specified.
        # -username USER - name of the account to use to connect to the host
        #   system. Ignored if -hostname is not specified.
        # -password PASSWORD - the password associated with the account.
        #   Ignored if -hostname is not specified.
        #
        # The specified host is not contacted until the connect method
        # is invoked.

        namespace path [linsert [namespace path] end [namespace qualifiers [self class]]]
        array set Opts [twapi::parseargs args {
            {hosttype.arg workstation {workstation workstation_shared server vi_server player}}
            hostname.arg
            {port.int 0}
            username.arg
            password.arg
        } -maxleftover 0]

        #ruff

        if {[info exists Opts(hostname)]} {
            if {$Opts(hosttype) in {workstation player}} {
                twapi::badargs! "Option -hostname cannot be specified if -hosttype is 'workstation' or 'player'"
            }
            foreach opt {port username password} {
                if {![info exists Opts($opt)]} {
                    twapi::badargs! "If -hostname is specified,  -port, -username and -password must also be specified."
                }
            }
        } else {
            if {$Opts(hosttype) ni {workstation player}} {
                twapi::badargs! "Option -hostname must be specified if -hosttype is not 'workstation' or 'player'"
            }
            set Opts(hostname) [twapi::vt_empty]
            set Opts(username) [twapi::vt_empty]
            set Opts(password) [twapi::vt_empty]
        }

        set Opts(VIX_SERVICE_PROVIDER) [VIX_SERVICEPROVIDER_VMWARE_[string toupper $Opts(hosttype)]]
    }

    destructor {
        # Disconnects from the VMware host
        #
        # After destroying a Host object, the application should not
        # attempt to access related VM objects.

        my disconnect 1
        if {[info exists Host]} {
            $Host Disconnect
            $Host -destroy
        }
    }

    method connect {} {
        # Establishes a connection to the host system represented by this
        # object.
        #
        # This method must be called before any virtual machines
        # can be opened on the host system. The method may be called
        # multiple times with calls being no-ops if the connection is
        # already established.

        if {[info exists Host]} {
            return
        }
        set Host [wait_for_result    \
                      [[vix] Connect \
                           [VIX_API_VERSION] \
                           $Opts(VIX_SERVICE_PROVIDER) \
                           $Opts(hostname) $Opts(port) \
                           $Opts(username) $Opts(password) \
                           0 0 0]]
        return
    }

    method disconnect {{force 0}} {
        # Disconnects the object from the associated VMware host.
        #
        # force - if 0 (default), an error is raised if any associated
        #   VM objects exist. If 1, the associated VM objects
        #   are forcibly destroyed before disconnecting.
        #
        # The application should normally ensure that all VM objects 
        # associated with this host have been closed before calling disconnect.
        #
        # The connect method may be called to reestablish the connection.

        if {![info exists Host]} {
            return
        }

        if {[array size VMs]} {
            if {! $force} {
                error "Cannot disconnect until all guest system objects are closed."
            }
            foreach guest [array names VMs] {
                catch {$guest destroy}
            }
        }

        $Host Disconnect
        $Host -destroy
        unset Host
    }

    method open {vm_path} {
        # Returns a VM object representing a virtual machine
        # on this VMware host
        #
        # vm_path - absolute path to the VMX file for the virtual machine
        #  on the VMware host. The path must be in the syntax expected
        #  by the VMware host operating system.
        #
        # The methods of the returned VM object may be used to interact with
        # the corresponding virtual machine.
        #
        # For VMware server and ESX/ESXi hosts, the virtual machine
        # must have been registered.

        set guest [VM create guest#[incr NameCounter] [wait_for_result [$Host OpenVM $vm_path 0]]]
        set VMs($guest) $vm_path
        trace add command $guest {rename delete} [list [self] _trace_VM]
        return $guest
    }

    method register {vm_path} {
        # Registers a virtual machine with a VMware host
        #
        # vm_path - absolute path to the VMX file for the virtual machine
        #  on the VMware host. The path must be in the syntax expected
        #  by the VMware host operating system.
        #
        # For VMware Server and ESX/ESXi hosts, a virtual machine must be 
        # registered before it can be accessed with the open call.
        # For other VMware host types, this call is ignored.
        # Registration is a one-time operation and may also be done
        # through the VMware command line or user interface.

        wait_for_completion [$Host RegisterVM $vm_path 0]
    }

    method unregister {vm_path} {
        # Unregisters a virtual machine with a VMware host.
        #
        # vm_path - absolute path to the VMX file for the virtual machine
        #  on the VMware host. The path must be in the syntax expected
        #  by the VMware host operating system.
        #
        # For VMware Server and ESX/ESXi hosts, a virtual machine must be 
        # registered before it can be accessed with the open call. This
        # method removes a registered VM from the host inventory.

        wait_for_completion [$Host UnregisterVM $vm_path 0]
    }

    method running_vms {} {
        return [wait_for_properties [$Host FindItems [VIX_FIND_RUNNING_VMS] 0 -1 0] [VIX_PROPERTY_FOUND_ITEM_LOCATION]]
    }

    method _trace_VM {oldname newname op} {
        if {$oldname eq $newname || ![info exists VMs($oldname)]} {
            return
        }
        if {$op eq "rename"} {
            set VMs($newname) $VMs($oldname)
        }
        unset VMs($oldname)
    }
    export _trace_guest

}

oo::class create vix::VM {
    variable Guest

    constructor {comobj} {
        # Represents a virtual machine on a VMware host.
        #
        # Objects of this class should not be created directly.
        # They are returned by the open method of an Host object.
        #
        # The methods of this class allow invoking of various operations
        # on the associated virtual machine.
        #
        # The associated VM may or may not be running.
        
        namespace path [linsert [namespace path] end [namespace qualifiers [self class]]]
        set Guest $comobj
    }

    destructor {
        $Guest -destroy
    }

    method power_on {args} {
        twapi::parseargs args {
            {showui.bool 1}
        } -setvars -maxleftover 0
        wait_for_completion [$Guest PowerOn \
                                 [pick $showui [VIX_VMPOWEROP_LAUNCH_GUI] \
                                      [VIX_VMPOWEROP_NORMAL]] \
                                 0 0]
    }
    method shutdown {} {
        wait_for_completion [$Guest PowerOff [VIX_VMPOWEROP_FROM_GUEST] 0]
    }

    method power_off {} {
        wait_for_completion [$Guest PowerOff [VIX_VMPOWEROP_NORMAL] 0]
    }

    method wait_for_tools {{timeout 0}} {
        wait_for_completion [$Guest WaitForToolsInGuest $timeout 0]
    }

    method login {username password args} {
        twapi::parseargs args {
            {interactive.bool 1}
        } -setvars -maxleftover 0
        wait_for_completion \
            [$Guest LoginInGuest $username $password \
                 [pick $interactive [VIX_LOGIN_IN_GUEST_REQUIRE_INTERACTIVE_ENVIRONMENT] 0] \
                 0]
    }

    method logout {} {
        wait_for_completion [$Guest LogoutFromGuest 0]
    }

    method getenv {envvar} {
        return [wait_for_result \
                    [$Guest ReadVariable [VIX_GUEST_ENVIRONMENT_VARIABLE] $envvar 0 0] \
                    VIX_PROPERTY_JOB_RESULT_VM_VARIABLE_STRING]
    }

    method setenv {envvar val} {
        wait_for_completion [$Guest WriteVariable [VIX_GUEST_ENVIRONMENT_VARIABLE] $envvar $val 0 0]
    }

    method copy_to_host {guest_path host_path} {
        check_path $guest_path
        wait_for_completion [$Guest CopyFileFromGuestToHost $guest_path $host_path 0 0 0]
    }

    method copy_to_vm {host_path guest_path} {
        check_path $guest_path
        wait_for_completion [$Guest CopyFileFromHostToGuest $host_path $guest_path 0 0 0]
    }

    method mkdir {path} {
        check_path $path
        twapi::trap {
            wait_for_completion [$Guest CreateDirectoryInGuest $path 0 0]
        } onerror [list VIX [VIX_E_FILE_ALREADY_EXISTS]] {
            # Ignore dir already exists errors
        }
    }

    method rmdir {path} {
        check_path $path
        twapi::trap {
            wait_for_completion [$Guest DeleteDirectoryInGuest $path 0 0]
        } onerror [list VIX [VIX_E_FILE_NOT_FOUND]] {
            # Ignore dir does not exist errors
        }
    }

    method delete_file {path} {
        check_path $path
        twapi::trap {
            wait_for_completion [$Guest DeleteFileInGuest $path 0]
        } onerror [list VIX [VIX_E_FILE_NOT_FOUND]] {
            # Ignore file does not exist errors
        }
    }

    method isdir {path} {
        check_path $path
        return [wait_for_result \
                    [$Guest DirectoryExistsInGuest $path 0] \
                    VIX_PROPERTY_JOB_RESULT_GUEST_OBJECT_EXISTS]
    }        

    method isfile {path} {
        check_path $path
        return [wait_for_result \
                    [$Guest FileExistsInGuest $path 0] \
                    VIX_PROPERTY_JOB_RESULT_GUEST_OBJECT_EXISTS]
    }        

    method file_info {path} {
        check_path $path
        lassign [wait_for_results \
                     [$Guest GetFileInfoInGuest $path 0] \
                     VIX_PROPERTY_JOB_RESULT_FILE_SIZE \
                     VIX_PROPERTY_JOB_RESULT_FILE_FLAGS \
                     VIX_PROPERTY_JOB_RESULT_FILE_MOD_TIME] \
            size flags time
        return [list size $size time $time \
                    directory [expr {($flags & [VIX_FILE_ATTRIBUTES_DIRECTORY]) != 0}] \
                    symlink [expr {($flags & [VIX_FILE_ATTRIBUTES_SYMLINK]) != 0}]]
    }

    method temp_file {} {
        return [wait_for_result \
                    [$Guest CreateTempFileInGuest 0 0 0] \
                    VIX_PROPERTY_JOB_RESULT_ITEM_NAME]
    }

    method rename {from to} {
        check_path $from
        check_path $to
        wait_for_completion [$Guest RenameFileInGuest $from $to 0 0 0]
    }

    method dir {path args} {
        twapi::parseargs args {{details.bool 0}} -setvars -maxleftover 0
        check_path $path
        set values {}
        set job [$Guest ListDirectoryInGuest $path 0 0]
        twapi::trap {
            wait_for_completion $job 1
            set count [$job GetNumProperties [VIX_PROPERTY_JOB_RESULT_ITEM_NAME]]
            if {$details} {
                set proparr [twapi::safearray i4 \
                                 [list \
                                      [VIX_PROPERTY_JOB_RESULT_ITEM_NAME] \
                                      [VIX_PROPERTY_JOB_RESULT_FILE_SIZE] \
                                      [VIX_PROPERTY_JOB_RESULT_FILE_FLAGS] \
                                      [VIX_PROPERTY_JOB_RESULT_FILE_MOD_TIME]]]
            } else {
                set proparr [twapi::safearray i4 [list [VIX_PROPERTY_JOB_RESULT_ITEM_NAME]]]
            }
            for {set i 0} {$i < $count} {incr i} {
                set results [twapi::vt_null]
                set err [$job GetNthProperties $i $proparr results]
                check_error $err
                if {$details} {
                    lassign [twapi::variant_value $results 0 0 0] name size flags time
                    lappend values \
                        [list name $name size $size time $time \
                             directory [expr {($flags & [VIX_FILE_ATTRIBUTES_DIRECTORY]) != 0}] \
                             symlink [expr {($flags & [VIX_FILE_ATTRIBUTES_SYMLINK]) != 0}]]
                } else {
                    lappend values [lindex [twapi::variant_value $results 0 0 0] 0]
                }
            }
        } finally {
            $job -destroy
        }

        return $values
    }

    method processes {args} {
        twapi::parseargs args {{details.bool 0}} -setvars -maxleftover 0
        set values {}
        set job [$Guest ListProcessesInGuest 0 0]

        twapi::trap {
            wait_for_completion $job 1
            set count [$job GetNumProperties [VIX_PROPERTY_JOB_RESULT_ITEM_NAME]]
            if {$details} {
                set proparr [twapi::safearray i4 \
                                 [list \
                                      [VIX_PROPERTY_JOB_RESULT_ITEM_NAME] \
                                      [VIX_PROPERTY_JOB_RESULT_PROCESS_ID] \
                                      [VIX_PROPERTY_JOB_RESULT_PROCESS_OWNER] \
                                      [VIX_PROPERTY_JOB_RESULT_PROCESS_COMMAND] \
                                      [VIX_PROPERTY_JOB_RESULT_PROCESS_BEING_DEBUGGED] \
                                      [VIX_PROPERTY_JOB_RESULT_PROCESS_START_TIME]]]
            } else {
                set proparr [twapi::safearray i4 [list [VIX_PROPERTY_JOB_RESULT_PROCESS_ID]]]
            }
            for {set i 0} {$i < $count} {incr i} {
                set results [twapi::vt_null]
                set err [$job GetNthProperties $i $proparr results]
                check_error $err
                if {$details} {
                    lappend values [twapi::twine {name pid owner command debugged start_time} [twapi::variant_value $results 0 0 0]]
                } else {
                    lappend values [lindex [twapi::variant_value $results 0 0 0] 0]
                }
            }
        } finally {
            $job -destroy
        }

        return $values
    }

    method kill {pid} {
        wait_for_completion [$Guest KillProcessInGuest $pid 0 0]
    }
}

namespace eval vix {
    # Syntactically, easier to access VIX #defines as commands than as variables
    foreach {_vixdefine _vixvalue} {
        VIX_INVALID_HANDLE 0
        VIX_HANDLETYPE_NONE 0
        VIX_HANDLETYPE_HOST 2
        VIX_HANDLETYPE_VM 3
        VIX_HANDLETYPE_NETWORK 5
        VIX_HANDLETYPE_JOB 6
        VIX_HANDLETYPE_SNAPSHOT 7
        VIX_HANDLETYPE_PROPERTY_LIST 9
        VIX_HANDLETYPE_METADATA_CONTAINER 11
        VIX_OK 0
        VIX_E_FAIL 1
        VIX_E_OUT_OF_MEMORY 2
        VIX_E_INVALID_ARG 3
        VIX_E_FILE_NOT_FOUND 4
        VIX_E_OBJECT_IS_BUSY 5
        VIX_E_NOT_SUPPORTED 6
        VIX_E_FILE_ERROR 7
        VIX_E_DISK_FULL 8
        VIX_E_INCORRECT_FILE_TYPE 9
        VIX_E_CANCELLED 10
        VIX_E_FILE_READ_ONLY 11
        VIX_E_FILE_ALREADY_EXISTS 12
        VIX_E_FILE_ACCESS_ERROR 13
        VIX_E_REQUIRES_LARGE_FILES 14
        VIX_E_FILE_ALREADY_LOCKED 15
        VIX_E_VMDB 16
        VIX_E_NOT_SUPPORTED_ON_REMOTE_OBJECT 20
        VIX_E_FILE_TOO_BIG 21
        VIX_E_FILE_NAME_INVALID 22
        VIX_E_ALREADY_EXISTS 23
        VIX_E_BUFFER_TOOSMALL 24
        VIX_E_OBJECT_NOT_FOUND 25
        VIX_E_HOST_NOT_CONNECTED 26
        VIX_E_INVALID_UTF8_STRING 27
        VIX_E_OPERATION_ALREADY_IN_PROGRESS 31
        VIX_E_UNFINISHED_JOB 29
        VIX_E_NEED_KEY 30
        VIX_E_LICENSE 32
        VIX_E_VM_HOST_DISCONNECTED 34
        VIX_E_AUTHENTICATION_FAIL 35
        VIX_E_HOST_CONNECTION_LOST 36
        VIX_E_DUPLICATE_NAME 41
        VIX_E_ARGUMENT_TOO_BIG 44
        VIX_E_INVALID_HANDLE 1000
        VIX_E_NOT_SUPPORTED_ON_HANDLE_TYPE 1001
        VIX_E_TOO_MANY_HANDLES 1002
        VIX_E_NOT_FOUND 2000
        VIX_E_TYPE_MISMATCH 2001
        VIX_E_INVALID_XML 2002
        VIX_E_TIMEOUT_WAITING_FOR_TOOLS 3000
        VIX_E_UNRECOGNIZED_COMMAND 3001
        VIX_E_OP_NOT_SUPPORTED_ON_GUEST 3003
        VIX_E_PROGRAM_NOT_STARTED 3004
        VIX_E_CANNOT_START_READ_ONLY_VM 3005
        VIX_E_VM_NOT_RUNNING 3006
        VIX_E_VM_IS_RUNNING 3007
        VIX_E_CANNOT_CONNECT_TO_VM 3008
        VIX_E_POWEROP_SCRIPTS_NOT_AVAILABLE 3009
        VIX_E_NO_GUEST_OS_INSTALLED 3010
        VIX_E_VM_INSUFFICIENT_HOST_MEMORY 3011
        VIX_E_SUSPEND_ERROR 3012
        VIX_E_VM_NOT_ENOUGH_CPUS 3013
        VIX_E_HOST_USER_PERMISSIONS 3014
        VIX_E_GUEST_USER_PERMISSIONS 3015
        VIX_E_TOOLS_NOT_RUNNING 3016
        VIX_E_GUEST_OPERATIONS_PROHIBITED 3017
        VIX_E_ANON_GUEST_OPERATIONS_PROHIBITED 3018
        VIX_E_ROOT_GUEST_OPERATIONS_PROHIBITED 3019
        VIX_E_MISSING_ANON_GUEST_ACCOUNT 3023
        VIX_E_CANNOT_AUTHENTICATE_WITH_GUEST 3024
        VIX_E_UNRECOGNIZED_COMMAND_IN_GUEST 3025
        VIX_E_CONSOLE_GUEST_OPERATIONS_PROHIBITED 3026
        VIX_E_MUST_BE_CONSOLE_USER 3027
        VIX_E_VMX_MSG_DIALOG_AND_NO_UI 3028
        VIX_E_OPERATION_NOT_ALLOWED_FOR_LOGIN_TYPE 3031
        VIX_E_LOGIN_TYPE_NOT_SUPPORTED 3032
        VIX_E_EMPTY_PASSWORD_NOT_ALLOWED_IN_GUEST 3033
        VIX_E_INTERACTIVE_SESSION_NOT_PRESENT 3034
        VIX_E_INTERACTIVE_SESSION_USER_MISMATCH 3035
        VIX_E_CANNOT_POWER_ON_VM 3041
        VIX_E_NO_DISPLAY_SERVER 3043
        VIX_E_TOO_MANY_LOGONS 3046
        VIX_E_INVALID_AUTHENTICATION_SESSION 3047
        VIX_E_VM_NOT_FOUND 4000
        VIX_E_NOT_SUPPORTED_FOR_VM_VERSION 4001
        VIX_E_CANNOT_READ_VM_CONFIG 4002
        VIX_E_TEMPLATE_VM 4003
        VIX_E_VM_ALREADY_LOADED 4004
        VIX_E_VM_ALREADY_UP_TO_DATE 4006
        VIX_E_VM_UNSUPPORTED_GUEST 4011
        VIX_E_UNRECOGNIZED_PROPERTY 6000
        VIX_E_INVALID_PROPERTY_VALUE 6001
        VIX_E_READ_ONLY_PROPERTY 6002
        VIX_E_MISSING_REQUIRED_PROPERTY 6003
        VIX_E_INVALID_SERIALIZED_DATA 6004
        VIX_E_PROPERTY_TYPE_MISMATCH 6005
        VIX_E_BAD_VM_INDEX 8000
        VIX_E_INVALID_MESSAGE_HEADER 10000
        VIX_E_INVALID_MESSAGE_BODY 10001
        VIX_E_SNAPSHOT_INVAL 13000
        VIX_E_SNAPSHOT_DUMPER 13001
        VIX_E_SNAPSHOT_DISKLIB 13002
        VIX_E_SNAPSHOT_NOTFOUND 13003
        VIX_E_SNAPSHOT_EXISTS 13004
        VIX_E_SNAPSHOT_VERSION 13005
        VIX_E_SNAPSHOT_NOPERM 13006
        VIX_E_SNAPSHOT_CONFIG 13007
        VIX_E_SNAPSHOT_NOCHANGE 13008
        VIX_E_SNAPSHOT_CHECKPOINT 13009
        VIX_E_SNAPSHOT_LOCKED 13010
        VIX_E_SNAPSHOT_INCONSISTENT 13011
        VIX_E_SNAPSHOT_NAMETOOLONG 13012
        VIX_E_SNAPSHOT_VIXFILE 13013
        VIX_E_SNAPSHOT_DISKLOCKED 13014
        VIX_E_SNAPSHOT_DUPLICATEDDISK 13015
        VIX_E_SNAPSHOT_INDEPENDENTDISK 13016
        VIX_E_SNAPSHOT_NONUNIQUE_NAME 13017
        VIX_E_SNAPSHOT_MEMORY_ON_INDEPENDENT_DISK 13018
        VIX_E_SNAPSHOT_MAXSNAPSHOTS 13019
        VIX_E_SNAPSHOT_MIN_FREE_SPACE 13020
        VIX_E_SNAPSHOT_HIERARCHY_TOODEEP 13021
        VIX_E_SNAPSHOT_RRSUSPEND 13022
        VIX_E_SNAPSHOT_NOT_REVERTABLE 13024
        VIX_E_HOST_DISK_INVALID_VALUE 14003
        VIX_E_HOST_DISK_SECTORSIZE 14004
        VIX_E_HOST_FILE_ERROR_EOF 14005
        VIX_E_HOST_NETBLKDEV_HANDSHAKE 14006
        VIX_E_HOST_SOCKET_CREATION_ERROR 14007
        VIX_E_HOST_SERVER_NOT_FOUND 14008
        VIX_E_HOST_NETWORK_CONN_REFUSED 14009
        VIX_E_HOST_TCP_SOCKET_ERROR 14010
        VIX_E_HOST_TCP_CONN_LOST 14011
        VIX_E_HOST_NBD_HASHFILE_VOLUME 14012
        VIX_E_HOST_NBD_HASHFILE_INIT 14013
        VIX_E_DISK_INVAL 16000
        VIX_E_DISK_NOINIT 16001
        VIX_E_DISK_NOIO 16002
        VIX_E_DISK_PARTIALCHAIN 16003
        VIX_E_DISK_NEEDSREPAIR 16006
        VIX_E_DISK_OUTOFRANGE 16007
        VIX_E_DISK_CID_MISMATCH 16008
        VIX_E_DISK_CANTSHRINK 16009
        VIX_E_DISK_PARTMISMATCH 16010
        VIX_E_DISK_UNSUPPORTEDDISKVERSION 16011
        VIX_E_DISK_OPENPARENT 16012
        VIX_E_DISK_NOTSUPPORTED 16013
        VIX_E_DISK_NEEDKEY 16014
        VIX_E_DISK_NOKEYOVERRIDE 16015
        VIX_E_DISK_NOTENCRYPTED 16016
        VIX_E_DISK_NOKEY 16017
        VIX_E_DISK_INVALIDPARTITIONTABLE 16018
        VIX_E_DISK_NOTNORMAL 16019
        VIX_E_DISK_NOTENCDESC 16020
        VIX_E_DISK_NEEDVMFS 16022
        VIX_E_DISK_RAWTOOBIG 16024
        VIX_E_DISK_TOOMANYOPENFILES 16027
        VIX_E_DISK_TOOMANYREDO 16028
        VIX_E_DISK_RAWTOOSMALL 16029
        VIX_E_DISK_INVALIDCHAIN 16030
        VIX_E_DISK_KEY_NOTFOUND 16052
        VIX_E_DISK_SUBSYSTEM_INIT_FAIL 16053
        VIX_E_DISK_INVALID_CONNECTION 16054
        VIX_E_DISK_ENCODING 16061
        VIX_E_DISK_CANTREPAIR 16062
        VIX_E_DISK_INVALIDDISK 16063
        VIX_E_DISK_NOLICENSE 16064
        VIX_E_DISK_NODEVICE 16065
        VIX_E_DISK_UNSUPPORTEDDEVICE 16066
        VIX_E_DISK_CAPACITY_MISMATCH 16067
        VIX_E_DISK_PARENT_NOTALLOWED 16068
        VIX_E_DISK_ATTACH_ROOTLINK 16069
        VIX_E_CRYPTO_UNKNOWN_ALGORITHM 17000
        VIX_E_CRYPTO_BAD_BUFFER_SIZE 17001
        VIX_E_CRYPTO_INVALID_OPERATION 17002
        VIX_E_CRYPTO_RANDOM_DEVICE 17003
        VIX_E_CRYPTO_NEED_PASSWORD 17004
        VIX_E_CRYPTO_BAD_PASSWORD 17005
        VIX_E_CRYPTO_NOT_IN_DICTIONARY 17006
        VIX_E_CRYPTO_NO_CRYPTO 17007
        VIX_E_CRYPTO_ERROR 17008
        VIX_E_CRYPTO_BAD_FORMAT 17009
        VIX_E_CRYPTO_LOCKED 17010
        VIX_E_CRYPTO_EMPTY 17011
        VIX_E_CRYPTO_KEYSAFE_LOCATOR 17012
        VIX_E_CANNOT_CONNECT_TO_HOST 18000
        VIX_E_NOT_FOR_REMOTE_HOST 18001
        VIX_E_INVALID_HOSTNAME_SPECIFICATION 18002
        VIX_E_SCREEN_CAPTURE_ERROR 19000
        VIX_E_SCREEN_CAPTURE_BAD_FORMAT 19001
        VIX_E_SCREEN_CAPTURE_COMPRESSION_FAIL 19002
        VIX_E_SCREEN_CAPTURE_LARGE_DATA 19003
        VIX_E_GUEST_VOLUMES_NOT_FROZEN 20000
        VIX_E_NOT_A_FILE 20001
        VIX_E_NOT_A_DIRECTORY 20002
        VIX_E_NO_SUCH_PROCESS 20003
        VIX_E_FILE_NAME_TOO_LONG 20004
        VIX_E_OPERATION_DISABLED 20005
        VIX_E_TOOLS_INSTALL_NO_IMAGE 21000
        VIX_E_TOOLS_INSTALL_IMAGE_INACCESIBLE 21001
        VIX_E_TOOLS_INSTALL_NO_DEVICE 21002
        VIX_E_TOOLS_INSTALL_DEVICE_NOT_CONNECTED 21003
        VIX_E_TOOLS_INSTALL_CANCELLED 21004
        VIX_E_TOOLS_INSTALL_INIT_FAILED 21005
        VIX_E_TOOLS_INSTALL_AUTO_NOT_SUPPORTED 21006
        VIX_E_TOOLS_INSTALL_GUEST_NOT_READY 21007
        VIX_E_TOOLS_INSTALL_SIG_CHECK_FAILED 21008
        VIX_E_TOOLS_INSTALL_ERROR 21009
        VIX_E_TOOLS_INSTALL_ALREADY_UP_TO_DATE 21010
        VIX_E_TOOLS_INSTALL_IN_PROGRESS 21011
        VIX_E_TOOLS_INSTALL_IMAGE_COPY_FAILED 21012
        VIX_E_WRAPPER_WORKSTATION_NOT_INSTALLED 22001
        VIX_E_WRAPPER_VERSION_NOT_FOUND 22002
        VIX_E_WRAPPER_SERVICEPROVIDER_NOT_FOUND 22003
        VIX_E_WRAPPER_PLAYER_NOT_INSTALLED 22004
        VIX_E_WRAPPER_RUNTIME_NOT_INSTALLED 22005
        VIX_E_WRAPPER_MULTIPLE_SERVICEPROVIDERS 22006
        VIX_E_MNTAPI_MOUNTPT_NOT_FOUND 24000
        VIX_E_MNTAPI_MOUNTPT_IN_USE 24001
        VIX_E_MNTAPI_DISK_NOT_FOUND 24002
        VIX_E_MNTAPI_DISK_NOT_MOUNTED 24003
        VIX_E_MNTAPI_DISK_IS_MOUNTED 24004
        VIX_E_MNTAPI_DISK_NOT_SAFE 24005
        VIX_E_MNTAPI_DISK_CANT_OPEN 24006
        VIX_E_MNTAPI_CANT_READ_PARTS 24007
        VIX_E_MNTAPI_UMOUNT_APP_NOT_FOUND 24008
        VIX_E_MNTAPI_UMOUNT 24009
        VIX_E_MNTAPI_NO_MOUNTABLE_PARTITONS 24010
        VIX_E_MNTAPI_PARTITION_RANGE 24011
        VIX_E_MNTAPI_PERM 24012
        VIX_E_MNTAPI_DICT 24013
        VIX_E_MNTAPI_DICT_LOCKED 24014
        VIX_E_MNTAPI_OPEN_HANDLES 24015
        VIX_E_MNTAPI_CANT_MAKE_VAR_DIR 24016
        VIX_E_MNTAPI_NO_ROOT 24017
        VIX_E_MNTAPI_LOOP_FAILED 24018
        VIX_E_MNTAPI_DAEMON 24019
        VIX_E_MNTAPI_INTERNAL 24020
        VIX_E_MNTAPI_SYSTEM 24021
        VIX_E_MNTAPI_NO_CONNECTION_DETAILS 24022
        VIX_E_MNTAPI_INCOMPATIBLE_VERSION 24300
        VIX_E_MNTAPI_OS_ERROR 24301
        VIX_E_MNTAPI_DRIVE_LETTER_IN_USE 24302
        VIX_E_MNTAPI_DRIVE_LETTER_ALREADY_ASSIGNED 24303
        VIX_E_MNTAPI_VOLUME_NOT_MOUNTED 24304
        VIX_E_MNTAPI_VOLUME_ALREADY_MOUNTED 24305
        VIX_E_MNTAPI_FORMAT_FAILURE 24306
        VIX_E_MNTAPI_NO_DRIVER 24307
        VIX_E_MNTAPI_ALREADY_OPENED 24308
        VIX_E_MNTAPI_ITEM_NOT_FOUND 24309
        VIX_E_MNTAPI_UNSUPPROTED_BOOT_LOADER 24310
        VIX_E_MNTAPI_UNSUPPROTED_OS 24311
        VIX_E_MNTAPI_CODECONVERSION 24312
        VIX_E_MNTAPI_REGWRITE_ERROR 24313
        VIX_E_MNTAPI_UNSUPPORTED_FT_VOLUME 24314
        VIX_E_MNTAPI_PARTITION_NOT_FOUND 24315
        VIX_E_MNTAPI_PUTFILE_ERROR 24316
        VIX_E_MNTAPI_GETFILE_ERROR 24317
        VIX_E_MNTAPI_REG_NOT_OPENED 24318
        VIX_E_MNTAPI_REGDELKEY_ERROR 24319
        VIX_E_MNTAPI_CREATE_PARTITIONTABLE_ERROR 24320
        VIX_E_MNTAPI_OPEN_FAILURE 24321
        VIX_E_MNTAPI_VOLUME_NOT_WRITABLE 24322
        VIX_ASYNC 25000
        VIX_E_ASYNC_MIXEDMODE_UNSUPPORTED 26000
        VIX_E_NET_HTTP_UNSUPPORTED_PROTOCOL 30001
        VIX_E_NET_HTTP_URL_MALFORMAT 30003
        VIX_E_NET_HTTP_COULDNT_RESOLVE_PROXY 30005
        VIX_E_NET_HTTP_COULDNT_RESOLVE_HOST 30006
        VIX_E_NET_HTTP_COULDNT_CONNECT 30007
        VIX_E_NET_HTTP_HTTP_RETURNED_ERROR 30022
        VIX_E_NET_HTTP_OPERATION_TIMEDOUT 30028
        VIX_E_NET_HTTP_SSL_CONNECT_ERROR 30035
        VIX_E_NET_HTTP_TOO_MANY_REDIRECTS 30047
        VIX_E_NET_HTTP_TRANSFER 30200
        VIX_E_NET_HTTP_SSL_SECURITY 30201
        VIX_E_NET_HTTP_GENERIC 30202
        VIX_PROPERTYTYPE_ANY 0
        VIX_PROPERTYTYPE_INTEGER 1
        VIX_PROPERTYTYPE_STRING 2
        VIX_PROPERTYTYPE_BOOL 3
        VIX_PROPERTYTYPE_HANDLE 4
        VIX_PROPERTYTYPE_INT64 5
        VIX_PROPERTYTYPE_BLOB 6
        VIX_PROPERTY_NONE 0
        VIX_PROPERTY_META_DATA_CONTAINER 2
        VIX_PROPERTY_HOST_HOSTTYPE 50
        VIX_PROPERTY_HOST_API_VERSION 51
        VIX_PROPERTY_HOST_SOFTWARE_VERSION 52
        VIX_PROPERTY_VM_NUM_VCPUS 101
        VIX_PROPERTY_VM_VMX_PATHNAME 103
        VIX_PROPERTY_VM_VMTEAM_PATHNAME 105
        VIX_PROPERTY_VM_MEMORY_SIZE 106
        VIX_PROPERTY_VM_READ_ONLY 107
        VIX_PROPERTY_VM_NAME 108
        VIX_PROPERTY_VM_GUESTOS 109
        VIX_PROPERTY_VM_IN_VMTEAM 128
        VIX_PROPERTY_VM_POWER_STATE 129
        VIX_PROPERTY_VM_TOOLS_STATE 152
        VIX_PROPERTY_VM_IS_RUNNING 196
        VIX_PROPERTY_VM_SUPPORTED_FEATURES 197
        VIX_PROPERTY_VM_SSL_ERROR 293
        VIX_PROPERTY_JOB_RESULT_ERROR_CODE 3000
        VIX_PROPERTY_JOB_RESULT_VM_IN_GROUP 3001
        VIX_PROPERTY_JOB_RESULT_USER_MESSAGE 3002
        VIX_PROPERTY_JOB_RESULT_EXIT_CODE 3004
        VIX_PROPERTY_JOB_RESULT_COMMAND_OUTPUT 3005
        VIX_PROPERTY_JOB_RESULT_HANDLE 3010
        VIX_PROPERTY_JOB_RESULT_GUEST_OBJECT_EXISTS 3011
        VIX_PROPERTY_JOB_RESULT_GUEST_PROGRAM_ELAPSED_TIME 3017
        VIX_PROPERTY_JOB_RESULT_GUEST_PROGRAM_EXIT_CODE 3018
        VIX_PROPERTY_JOB_RESULT_ITEM_NAME 3035
        VIX_PROPERTY_JOB_RESULT_FOUND_ITEM_DESCRIPTION 3036
        VIX_PROPERTY_JOB_RESULT_SHARED_FOLDER_COUNT 3046
        VIX_PROPERTY_JOB_RESULT_SHARED_FOLDER_HOST 3048
        VIX_PROPERTY_JOB_RESULT_SHARED_FOLDER_FLAGS 3049
        VIX_PROPERTY_JOB_RESULT_PROCESS_ID 3051
        VIX_PROPERTY_JOB_RESULT_PROCESS_OWNER 3052
        VIX_PROPERTY_JOB_RESULT_PROCESS_COMMAND 3053
        VIX_PROPERTY_JOB_RESULT_FILE_FLAGS 3054
        VIX_PROPERTY_JOB_RESULT_PROCESS_START_TIME 3055
        VIX_PROPERTY_JOB_RESULT_VM_VARIABLE_STRING 3056
        VIX_PROPERTY_JOB_RESULT_PROCESS_BEING_DEBUGGED 3057
        VIX_PROPERTY_JOB_RESULT_SCREEN_IMAGE_SIZE 3058
        VIX_PROPERTY_JOB_RESULT_SCREEN_IMAGE_DATA 3059
        VIX_PROPERTY_JOB_RESULT_FILE_SIZE 3061
        VIX_PROPERTY_JOB_RESULT_FILE_MOD_TIME 3062
        VIX_PROPERTY_JOB_RESULT_EXTRA_ERROR_INFO 3084
        VIX_PROPERTY_FOUND_ITEM_LOCATION 4010
        VIX_PROPERTY_SNAPSHOT_DISPLAYNAME 4200
        VIX_PROPERTY_SNAPSHOT_DESCRIPTION 4201
        VIX_PROPERTY_SNAPSHOT_POWERSTATE 4205
        VIX_PROPERTY_GUEST_SHAREDFOLDERS_SHARES_PATH 4525
        VIX_PROPERTY_VM_ENCRYPTION_PASSWORD 7001
        VIX_EVENTTYPE_JOB_COMPLETED 2
        VIX_EVENTTYPE_JOB_PROGRESS 3
        VIX_EVENTTYPE_FIND_ITEM 8
        VIX_EVENTTYPE_CALLBACK_SIGNALLED 2
        VIX_FILE_ATTRIBUTES_DIRECTORY 1
        VIX_FILE_ATTRIBUTES_SYMLINK 2
        VIX_HOSTOPTION_VERIFY_SSL_CERT 16384
        VIX_SERVICEPROVIDER_DEFAULT 1
        VIX_SERVICEPROVIDER_VMWARE_SERVER 2
        VIX_SERVICEPROVIDER_VMWARE_WORKSTATION 3
        VIX_SERVICEPROVIDER_VMWARE_PLAYER 4
        VIX_SERVICEPROVIDER_VMWARE_VI_SERVER 10
        VIX_SERVICEPROVIDER_VMWARE_WORKSTATION_SHARED 11
        VIX_API_VERSION -1
        VIX_FIND_RUNNING_VMS 1
        VIX_FIND_REGISTERED_VMS 4
        VIX_VMOPEN_NORMAL 0
        VIX_VMPOWEROP_NORMAL 0
        VIX_VMPOWEROP_FROM_GUEST 4
        VIX_VMPOWEROP_SUPPRESS_SNAPSHOT_POWERON 128
        VIX_VMPOWEROP_LAUNCH_GUI 512
        VIX_VMPOWEROP_START_VM_PAUSED 4096
        VIX_VMDELETE_DISK_FILES 2
        VIX_POWERSTATE_POWERING_OFF 1
        VIX_POWERSTATE_POWERED_OFF 2
        VIX_POWERSTATE_POWERING_ON 4
        VIX_POWERSTATE_POWERED_ON 8
        VIX_POWERSTATE_SUSPENDING 16
        VIX_POWERSTATE_SUSPENDED 32
        VIX_POWERSTATE_TOOLS_RUNNING 64
        VIX_POWERSTATE_RESETTING 128
        VIX_POWERSTATE_BLOCKED_ON_MSG 256
        VIX_POWERSTATE_PAUSED 512
        VIX_POWERSTATE_RESUMING 2048
        VIX_TOOLSSTATE_UNKNOWN 1
        VIX_TOOLSSTATE_RUNNING 2
        VIX_TOOLSSTATE_NOT_INSTALLED 4
        VIX_VM_SUPPORT_SHARED_FOLDERS 1
        VIX_VM_SUPPORT_MULTIPLE_SNAPSHOTS 2
        VIX_VM_SUPPORT_TOOLS_INSTALL 4
        VIX_VM_SUPPORT_HARDWARE_UPGRADE 8
        VIX_LOGIN_IN_GUEST_REQUIRE_INTERACTIVE_ENVIRONMENT 8
        VIX_RUNPROGRAM_RETURN_IMMEDIATELY 1
        VIX_RUNPROGRAM_ACTIVATE_WINDOW 2
        VIX_VM_GUEST_VARIABLE 1
        VIX_VM_CONFIG_RUNTIME_ONLY 2
        VIX_GUEST_ENVIRONMENT_VARIABLE 3
        VIX_SNAPSHOT_REMOVE_CHILDREN 1
        VIX_SNAPSHOT_INCLUDE_MEMORY 2
        VIX_SHAREDFOLDER_WRITE_ACCESS 4
        VIX_CAPTURESCREENFORMAT_PNG 1
        VIX_CAPTURESCREENFORMAT_PNG_NOCOMPRESS 2
        VIX_CLONETYPE_FULL 0
        VIX_CLONETYPE_LINKED 1
        VIX_INSTALLTOOLS_MOUNT_TOOLS_INSTALLER 0
        VIX_INSTALLTOOLS_AUTO_UPGRADE 1
        VIX_INSTALLTOOLS_RETURN_IMMEDIATELY 2
    } {
        interp alias {} [namespace current]::$_vixdefine {} lindex $_vixvalue
    }
    unset _vixdefine
    unset _vixvalue
}