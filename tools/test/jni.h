/* Just enough of <jni.h> for src/droid.c to compile on a machine with no Android NDK. The real
 * header is used for the Android build; this is only ever found by the headless CI compile. */
#pragma once
#include <stdint.h>
typedef uint8_t jboolean;
#define JNI_FALSE 0
#define JNI_TRUE 1
typedef void *jobject; typedef void *jclass; typedef void *jmethodID;
typedef struct JNINativeInterface_ JNINativeInterface;
struct JNINativeInterface_ {
    jclass (*GetObjectClass)(void *env, jobject obj);
    jmethodID (*GetMethodID)(void *env, jclass cls, const char *name, const char *sig);
    jboolean (*CallBooleanMethod)(void *env, jobject obj, jmethodID m, ...);
    jboolean (*ExceptionCheck)(void *env);
    void (*ExceptionClear)(void *env);
    void (*DeleteLocalRef)(void *env, jobject obj);
};
typedef const struct JNINativeInterface_ *JNIEnv;
