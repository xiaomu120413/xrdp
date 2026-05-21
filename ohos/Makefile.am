if XRDP_OHOS
XRDP_RUNTIME_CFG_PATH = /data/storage/el2/base/files/xrdp/config
XRDP_RUNTIME_SHARE_PATH = /data/storage/el2/base/files/xrdp/share
XRDP_RUNTIME_PID_PATH = /data/storage/el2/base/files/xrdp/run
else
XRDP_RUNTIME_CFG_PATH = ${sysconfdir}/${sysconfsubdir}
XRDP_RUNTIME_SHARE_PATH = ${datadir}/xrdp
XRDP_RUNTIME_PID_PATH = ${localstatedir}/run
endif

AM_CPPFLAGS = \
  -DXRDP_CFG_PATH=\"$(XRDP_RUNTIME_CFG_PATH)\" \
  -DXRDP_SBIN_PATH=\"${sbindir}\" \
  -DXRDP_SHARE_PATH=\"$(XRDP_RUNTIME_SHARE_PATH)\" \
  -DXRDP_PID_PATH=\"$(XRDP_RUNTIME_PID_PATH)\" \
  -I$(top_builddir) \
  -I$(top_srcdir)/common \
  -I$(top_srcdir)/libxrdp \
  -I$(top_srcdir)/xup \
  -I$(top_srcdir)/xrdp

module_LTLIBRARIES = \
  libxrdpohos.la

libxrdpohos_la_SOURCES = \
  ohos.c \
  ohos_cliprdr_channel.c \
  ohos_cliprdr.c \
  ohos_cliprdr_messages.c \
  ohos_cliprdr_pasteboard.c \
  ohos_cliprdr_private.h \
  ohos_cliprdr_protocol.c \
  ohos_cliprdr.h \
  xrdp_ohos.h

libxrdpohos_la_LIBADD = \
  $(top_builddir)/common/libcommon.la \
  $(XRDP_OHOS_BACKEND_LIBS)

if XRDP_OHOS
libxrdpohos_la_LDFLAGS = \
  -avoid-version \
  -module \
  -Wl,-rpath,'$$ORIGIN' \
  -Wl,-rpath,'$$ORIGIN/../lib' \
  -Wl,-rpath,'$$ORIGIN/..'
else
if !MACOS
libxrdpohos_la_LDFLAGS = -avoid-version -module
endif
endif
