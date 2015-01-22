
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FFMPEG Information : ", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FFMPEG Error : ", __VA_ARGS__)