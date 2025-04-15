handle SIGPIPE nostop
set breakpoint pending on
b __asan::ReportGenericError