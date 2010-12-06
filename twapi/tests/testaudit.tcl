# Report which commands do not have a test suite

#package require twapi

package require tcltest

set implemented 0
set placeholders {}
set missing {}

proc tcltest::test args {
    set test_id [lindex $args 0]
    lappend ::tests $test_id

    # If a constraint is TBD, mark as a placeholder
    set i [lsearch -exact $args "-constraints"]
    if {$i >= 0} {
        if {[lsearch -exact [lindex $args [incr i]] "TBD"] >= 0} {
            lappend ::placeholders $test_id
        } else {
            incr ::implemented
        }
    } else {
        incr ::implemented
    }
}
proc tcltest::cleanupTests {} {}

foreach fn [glob *.test] {
    source $fn
}

set missing {}
foreach cmd [twapi::_get_public_procs] {
    # Skip internal commands
    if {[regexp {^(_|[A-Z]).*} $cmd]} continue

    if {[lsearch -glob $::tests ${cmd}-*] < 0} {
        lappend missing $cmd
    }
}

puts "Implemented tests: $implemented"
puts "\nPlaceholders: [llength $placeholders]"
puts [join [lsort $placeholders] \n]
puts "\nMissing tests:[llength $::missing]"
puts [join [lsort $missing] \n]


