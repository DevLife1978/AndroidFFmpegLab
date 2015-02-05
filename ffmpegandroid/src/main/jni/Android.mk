LOCAL_PATH := $(call my-dir)
FFMPEG_PATH := $(LOCAL_PATH)/ffmpeg

include $(CLEAR_VARS)
LOCAL_MODULE := avutil
LOCAL_SRC_FILES := $(FFMPEG_PATH)/lib/libavutil.so
LOCAL_EXPORT_C_INCLUDES := $(FFMPEG_PATH)/include
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := avcodec
LOCAL_SRC_FILES := $(FFMPEG_PATH)/lib/libavcodec.so
LOCAL_EXPORT_C_INCLUDES := $(FFMPEG_PATH)/include
LOCAL_SHARED_LIBRARIES := avutil
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := avformat
LOCAL_SRC_FILES := $(FFMPEG_PATH)/lib/libavformat.so
LOCAL_EXPORT_C_INCLUDES := $(FFMPEG_PATH)/include
LOCAL_SHARED_LIBRARIES := avcodec avutil
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := avfilter
LOCAL_SRC_FILES := $(FFMPEG_PATH)/lib/libavfilter.so
LOCAL_EXPORT_C_INCLUDES := $(FFMPEG_PATH)/include
LOCAL_SHARED_LIBRARIES := avcodec avutil
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := swresample
LOCAL_SRC_FILES := $(FFMPEG_PATH)/lib/libswresample.so
LOCAL_EXPORT_C_INCLUDES := $(FFMPEG_PATH)/include/libswresample
LOCAL_SHARED_LIBRARIES := avutil
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := swscale
LOCAL_SRC_FILES := $(FFMPEG_PATH)/lib/libswscale.so
LOCAL_EXPORT_C_INCLUDES := $(FFMPEG_PATH)/include/libswscale
LOCAL_SHARED_LIBRARIES := avutil
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := ffmpeg
LOCAL_SRC_FILES := transcoding.c muxing.c ffmpeglib.c
LOCAL_CFLAGS := -std=c99
LOCAL_LDLIBS := -lz -lm  -ljnigraphics -llog
LOCAL_EXPORT_C_INCLUDES := $(FFMPEG_PATH)/include
LOCAL_SHARED_LIBRARIES := avutil avcodec avformat swscale swresample avfilter
include $(BUILD_SHARED_LIBRARY)