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


# Reset $break
proc _res_brk
do; break ""; until 0
endproc


# Reset $cont
proc _res_cont
do; continue ""; until 1
endproc


# Reset both
proc _res_bc
call _res_brk
call _res_cont
endproc

