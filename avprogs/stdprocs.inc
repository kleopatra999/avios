#
# Some usefull procedures
#

# Write to a running processes message queue. Keep trying if we
# can't do it at the moment.
proc _loop_interrupt pid mesg
var status

if [set status [elements [pcsinfo pid] 8]]!="IMAGE" and \
   status!="EXITING" and \
   status!="HALTED"
     while [not [interrupt pid with mesg]]; wend
fi
endproc


var major_version minor_version patch_level

proc version_info
set major_version [midstr $version 1 1]
set minor_version [midstr $version 3 3]
set patch_level [midstr $version 5 5]
endproc
