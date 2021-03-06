/*
 * Name:        wx/unix/chkconf.h
 * Purpose:     Unix-specific config settings consistency checks
 * Author:      Vadim Zeitlin
 * Created:     2007-07-14
 * Copyright:   (c) 2007 Vadim Zeitlin <vadim@wxwidgets.org>
 * Licence:     wxWindows licence
 */

/* THIS IS A C FILE, DON'T USE C++ FEATURES (IN PARTICULAR COMMENTS) IN IT */

#if wxUSE_CONSOLE_EVENTLOOP
#   if !wxUSE_SELECT_DISPATCHER && !wxUSE_EPOLL_DISPATCHER
#           undef wxUSE_SELECT_DISPATCHER
#           define wxUSE_SELECT_DISPATCHER 1
#   endif
#endif /* wxUSE_CONSOLE_EVENTLOOP */
