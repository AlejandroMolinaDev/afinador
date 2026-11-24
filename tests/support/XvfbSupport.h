#pragma once

// Test-tool support for a bare Xvfb display (CI and local headless runs).
//
// A bare Xvfb has no window manager, so the EWMH / ICCCM atoms JUCE looks up
// "only if they exist" are missing and creating a desktop window fails with
// BadAtom. Every real desktop session has them: create them once, as a window
// manager would. The connection stays open on purpose: an X server resets (and
// forgets the atoms) when its last client disconnects. No-op elsewhere.

#include <juce_core/juce_core.h>

#if JUCE_LINUX || JUCE_BSD
 #include <cstdlib>
 #include <dlfcn.h>
#endif

namespace novapitch::test
{

inline void prepareX11Display()
{
   #if JUCE_LINUX || JUCE_BSD
    if (std::getenv ("DISPLAY") == nullptr)
        return;
    auto* x11 = dlopen ("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (x11 == nullptr)
        return;
    using OpenFn = void* (*) (const char*);
    using InternFn = unsigned long (*) (void*, const char*, int);
    auto open = reinterpret_cast<OpenFn> (dlsym (x11, "XOpenDisplay"));
    auto intern = reinterpret_cast<InternFn> (dlsym (x11, "XInternAtom"));
    if (open == nullptr || intern == nullptr)
        return;
    if (auto* display = open (nullptr))
        for (const char* name : { "CLIPBOARD", "KWM_WIN_DECORATION", "TARGETS", "UTF8_STRING", "WM_CHANGE_STATE",
                                  "WM_DELETE_WINDOW", "WM_PROTOCOLS", "WM_STATE", "WM_TAKE_FOCUS", "XdndActionAsk",
                                  "XdndActionCopy", "XdndActionDescription", "XdndActionLink", "XdndActionList",
                                  "XdndActionMove", "XdndActionPrivate", "XdndAware", "XdndDrop", "XdndEnter",
                                  "XdndFinished", "XdndLeave", "XdndPosition", "XdndSelection", "XdndStatus",
                                  "XdndTypeList", "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE", "_MOTIF_WM_HINTS",
                                  "_NET_ACTIVE_WINDOW", "_NET_FRAME_EXTENTS", "_NET_WM_ALLOWED_ACTIONS",
                                  "_NET_WM_ICON", "_NET_WM_MOVERESIZE", "_NET_WM_PID", "_NET_WM_PING", "_NET_WM_STATE",
                                  "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_MAXIMIZED_HORZ",
                                  "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_USER_TIME", "_NET_WM_WINDOW_TYPE",
                                  "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WORKAREA",
                                  "_WIN_HINTS", "_XEMBED", "_XEMBED_INFO", "text/plain", "text/uri-list",
                                  "text/plain;charset=utf-8" })
            intern (display, name, 0);
   #endif
}

} // namespace novapitch::test
