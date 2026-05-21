if XRDP_PIXMAN
  PIXMAN_SOURCES =
else
  PIXMAN_SOURCES = pixman-region16.c pixman-region.h
endif

EXTRA_DIST = pixman-region.c

include_HEADERS = \
  ms-erref.h \
  ms-fscc.h \
  ms-rdpbcgr.h \
  ms-rdpeclip.h \
  ms-rdpefs.h \
  ms-rdpegdi.h \
  ms-rdpele.h \
  ms-rdperp.h \
  ms-rdpedisp.h \
  ms-smb2.h \
  xup_client_info.h \
  xrdp_client_info.h \
  xrdp_constants.h \
  xrdp_rail.h \
  xrdp_scancode_defs.h \
  xrdp_sockets.h

if XRDP_OHOS
XRDP_RUNTIME_CFG_PATH = /data/storage/el2/base/files/xrdp/config
XRDP_RUNTIME_SHARE_PATH = /data/storage/el2/base/files/xrdp/share
XRDP_RUNTIME_PID_PATH = /data/storage/el2/base/files/xrdp/run
XRDP_RUNTIME_LOG_PATH = /data/storage/el2/base/files/xrdp/log
else
XRDP_RUNTIME_CFG_PATH = ${sysconfdir}/${sysconfsubdir}
XRDP_RUNTIME_SHARE_PATH = ${datadir}/xrdp
XRDP_RUNTIME_PID_PATH = ${localstatedir}/run
XRDP_RUNTIME_LOG_PATH = ${localstatedir}/log
endif

AM_CPPFLAGS = \
  -DXRDP_CFG_PATH=\"$(XRDP_RUNTIME_CFG_PATH)\" \
  -DXRDP_SBIN_PATH=\"${sbindir}\" \
  -DXRDP_SHARE_PATH=\"$(XRDP_RUNTIME_SHARE_PATH)\" \
  -DXRDP_PID_PATH=\"$(XRDP_RUNTIME_PID_PATH)\" \
  -DXRDP_LOG_PATH=\"$(XRDP_RUNTIME_LOG_PATH)\"

if XRDP_NVENC
AM_CPPFLAGS += -DXRDP_NVENC
endif

# -no-suppress is an automake-specific flag which is needed
# to prevent us missing compiler errors in some circumstances
# (see https://github.com/neutrinolabs/xrdp/pull/1843 )
AM_CFLAGS = -no-suppress $(OPENSSL_CFLAGS)

module_LTLIBRARIES = \
  libcommon.la

libcommon_la_SOURCES = \
  arch.h \
  base64.h \
  base64.c \
  channel_defs.h \
  defines.h \
  fifo.c \
  fifo.h \
  file.c \
  file.h \
  guid.c \
  guid.h \
  list.c \
  list.h \
  list16.c \
  list16.h \
  log.c \
  log.h \
  os_calls.c \
  os_calls.h \
  parse.c \
  parse.h \
  rail.h \
  scancode.c \
  scancode.h \
  set_int.c \
  set_int.h \
  ssl_calls.c \
  ssl_calls.h \
  string_calls.c \
  string_calls.h \
  thread_calls.c \
  thread_calls.h \
  timers.c \
  timers.h \
  trans.c \
  trans.h \
  unicode_defines.h \
  $(PIXMAN_SOURCES)

libcommon_la_LIBADD = \
  -lpthread \
  $(OPENSSL_LIBS) \
  $(XRDP_OHOS_LIBS) \
  $(DLOPEN_LIBS)
