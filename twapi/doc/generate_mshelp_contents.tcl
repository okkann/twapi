# Generate a MS Help TOC file from the DTP meta file

# TBD - should do this in a safe interp

# Create a linkable name that matches the one created by the doctools
# htm formatter
proc make_linkable_cmd {name} {
    # Map starting - to x- as the former is not legal in XHTML "id" attribute
    if {[string index $name 0] eq "-"} {
        set name x$name
    }

    # If name is all upper case, attach another underscore
    if {[regexp {[[:alnum:]]+} $name] &&
        [string toupper $name] eq $name} {
        append name _uc
    }

    # Remove spaces
    set name [string map {{ } {}} $name]    

    # Hopefully all the above will not lead to name clashes
    return [string tolower $name]
}

proc process_meta {data} {
    set text ""
    foreach {__ item} $data {
        array set manpage $item
        set filename [file rootname $manpage(file)].html
        append text {<LI> <OBJECT type="text/sitemap">}
        if {$manpage(desc) != ""} {
            append text "\n\t<param name=\"Name\" value=\"$manpage(desc)\">"
        }
        append text "\n\t<param name=\"Name\" value=\"$manpage(title)\">"
        append text "\n\t<param name=\"Local\" value=\"$filename\">"
        append text "\n\t</OBJECT>\n"
        if {[info exists manpage(commands)] && [llength $manpage(commands)]} {
            append text "\n\t<UL>"
            foreach cmd $manpage(commands) {
                set cmd [eval concat $cmd]
                append text "\n\t\t<LI> <OBJECT type=\"text/sitemap\">"
                append text "\n\t\t<param name=\"Name\" value=\"$cmd\">"
                append text "\n\t\t<param name=\"Local\" value=\"$filename#[make_linkable_cmd $cmd]\">"
                append text "\n\t\t</OBJECT>\n"
            }
            append text "\n\t</UL>"
        }
        unset manpage
    }
    return $text
}

#
# Processing being here
#

# Output the standard header
puts {
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<HTML>
<HEAD>
<meta name="GENERATOR" content="Microsoft&reg; HTML Help Workshop 4.1">
<!-- Sitemap 1.0 -->
</HEAD><BODY>
</HEAD><BODY>
<OBJECT type="text/site properties">
        <param name="Auto Generated" value="Yes">
</OBJECT>
<UL>
}

# Open the first argument as a toc file generated by dtp
set fd [open [lindex $argv 0]]
set data [read $fd]
close $fd

# Eval the file - should really be in a safe interpreter! TBD
puts [process_meta $data]

# Write the trailer
puts {
</UL>
</BODY></HTML>
}
