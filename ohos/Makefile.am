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
  -I$(top_srcdir) \
  -I$(top_srcdir)/common \
  -I$(top_srcdir)/libxrdp \
  -I$(top_srcdir)/xup \
  -I$(top_srcdir)/xrdp

module_LTLIBRARIES = \
  libxrdpohos.la

libxrdpohos_la_SOURCES = \
  ohos.c \
  ohos_backend_api.c \
  ohos_audio_capture_bridge.cpp \
  ohos_audio_capture_bridge.h \
  ohos_cliprdr_bitmap.c \
  ohos_cliprdr_channel.c \
  ohos_cliprdr.c \
  ohos_cliprdr_file.c \
  ohos_cliprdr_file_pasteboard.c \
  ohos_cliprdr_file_remote.c \
  ohos_cliprdr_formats.c \
  ohos_cliprdr_image.c \
  ohos_cliprdr_image_cache.c \
  ohos_cliprdr_image_source.c \
  ohos_cliprdr_messages.c \
  ohos_cliprdr_pasteboard.c \
  ohos_cliprdr_private.h \
  ohos_cliprdr_protocol.c \
  ohos_cliprdr_rich.c \
  ohos_cliprdr_transfer.c \
  ohos_cursor.c \
  ohos_cursor.h \
  ohos_cursor_image.c \
  ohos_cursor_image.h \
  ohos_cursor_state.c \
  ohos_cursor_system.c \
  ohos_cursor_system.h \
  ohos_capture_common.cpp \
  ohos_capture_common.h \
  ohos_capture_controller.cpp \
  ohos_capture_controller.h \
  ohos_capture_facade.cpp \
  ohos_capture_h264.cpp \
  ohos_capture_h264.h \
  ohos_capture_h264_encoder.cpp \
  ohos_capture_h264_encoder.h \
  ohos_capture_raw.cpp \
  ohos_capture_raw.h \
  ohos_capture_types.h \
  ohos_desktop_size.c \
  ohos_desktop_size.h \
  ohos_display_geometry.c \
  ohos_events.c \
  ohos_frame.c \
  ohos_frame_submitter.cpp \
  ohos_frame_submitter.h \
  ohos_gfx_avc420.c \
  ohos_gfx_avc420.h \
  ohos_h264_payload.cpp \
  ohos_h264_payload.h \
  ohos_session.c \
  ohos_state.c \
  ohos_input.c \
  ohos_input_auth.c \
  ohos_input.h \
  ohos_input_keymap.c \
  ohos_input_mouse.c \
  ohos_module_callbacks.c \
  ohos_rdpsnd.c \
  ohos_rdpsnd_channel.c \
  ohos_rdpsnd.h \
  ohos_rdpsnd_private.h \
  ohos_rdpsnd_protocol.c \
  ohos_cliprdr.h \
  ohos_private.h \
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
