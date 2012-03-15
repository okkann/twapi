if {$::tcl_platform(os) ne "Windows NT" ||
    ($::tcl_platform(machine) ne "intel" &&
     $::tcl_platform(machine) ne "amd64")} {
    return
}

namespace eval twapi {}
proc twapi::package_setup {dir pkg version type {file {}} {commands {}}} {
    # Need the twapi base of the same version
    package require twapi_base $version

    global auto_index

    if {$file eq ""} {
        set file $pkg
    }
    if {$type eq "load"} {
        # Package could be statically linked or to be loaded
        if {[twapi::get_build_config single_module]} {
            # Modules are statically bound
            set fn {}
        } else {
            # Modules are external DLL's
            if {$::tcl_platform(pointerSize) == 8} {
                set fn [file join $dir "${file}64.dll"]
            } else {
                set fn [file join $dir "${file}.dll"]
            }
        }
        set loadcmd [list load $fn $pkg]
    } else {
        # A pure Tcl script package
        set loadcmd [list twapi::Twapi_SourceResource $file 1]
    }

    if {[llength $commands] == 0} {
        # No commands specified, load the package right away
        uplevel #0 $loadcmd
    } else {
        # Set up the load for when commands are actually accessed
        foreach command $commands {
            set auto_index($command) $loadcmd
        }
    }
    package provide $pkg $version
}

# The build process will append package ifneeded commands below
# to create an appropriate pkgIndex.tcl file for included modules