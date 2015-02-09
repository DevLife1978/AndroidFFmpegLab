/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class app_jni_ffmpegandroid_ffmpeglib */

#ifndef _Included_app_jni_ffmpegandroid_ffmpeglib
#define _Included_app_jni_ffmpegandroid_ffmpeglib
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     app_jni_ffmpegandroid_ffmpeglib
 * Method:    initialize
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_app_jni_ffmpegandroid_ffmpeglib_initialize
  (JNIEnv *, jobject);

/*
 * Class:     app_jni_ffmpegandroid_ffmpeglib
 * Method:    media_length
 * Signature: (Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_app_jni_ffmpegandroid_ffmpeglib_media_1length
  (JNIEnv *, jobject, jstring);

/*
 * Class:     app_jni_ffmpegandroid_ffmpeglib
 * Method:    media_total_frame
 * Signature: (Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_app_jni_ffmpegandroid_ffmpeglib_media_1total_1frame
  (JNIEnv *, jobject, jstring);

/*
 * Class:     app_jni_ffmpegandroid_ffmpeglib
 * Method:    ffmpeg_test
 * Signature: (Ljava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_app_jni_ffmpegandroid_ffmpeglib_ffmpeg_1test
  (JNIEnv *, jobject, jstring, jstring);

/*
 * Class:     app_jni_ffmpegandroid_ffmpeglib
 * Method:    stop
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_app_jni_ffmpegandroid_ffmpeglib_stop
  (JNIEnv *, jobject);

void log_frame(int frame, int total_frame);

void log_finish();

#ifdef __cplusplus
}
#endif
#endif
