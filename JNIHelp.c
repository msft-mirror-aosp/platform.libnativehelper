/*
 * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <nativehelper/libnativehelper_api.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jni.h>

#define LOG_TAG "JNIHelp"
#include "ALog-priv.h"

#include "ExpandableString.h"
#include "JniConstants.h"

//
// Helper methods
//

static const char* platformStrError(int errnum, char* buf, size_t buflen) {
#ifdef _WIN32
    strerror_s(buf, buflen, errnum);
    return buf;
#elif defined(__USE_GNU) && __ANDROID_API__ >= 23
    // char *strerror_r(int errnum, char *buf, size_t buflen);  /* GNU-specific */
    return strerror_r(errnum, buf, buflen);
#else
    // int strerror_r(int errnum, char *buf, size_t buflen);    /* XSI-compliant */
    int rc = strerror_r(errnum, buf, buflen);
    if (rc != 0) {
        snprintf(buf, buflen, "errno %d", errnum);
    }
    return buf;
#endif
}

static int GetBufferPosition(JNIEnv* env, jobject nioBuffer) {
    return(*env)->GetIntField(env, nioBuffer, JniConstants_NioBuffer_position(env));
}

static int GetBufferLimit(JNIEnv* env, jobject nioBuffer) {
    return(*env)->GetIntField(env, nioBuffer, JniConstants_NioBuffer_limit(env));
}

static int GetBufferElementSizeShift(JNIEnv* env, jobject nioBuffer) {
    return(*env)->GetIntField(env, nioBuffer, JniConstants_NioBuffer__elementSizeShift(env));
}

static bool AppendJString(JNIEnv* env, jstring text, struct ExpandableString* dst) {
    const char* utfText = (*env)->GetStringUTFChars(env, text, NULL);
    if (utfText == NULL) {
        return false;
    }
    bool success = ExpandableStringAppend(dst, utfText);
    (*env)->ReleaseStringUTFChars(env, text, utfText);
    return success;
}

/*
 * Returns a human-readable summary of an exception object.  The buffer will
 * be populated with the "binary" class name and, if present, the
 * exception message.
 */
static bool GetExceptionSummary(JNIEnv* env, jthrowable thrown, struct ExpandableString* dst) {
    // Summary is <exception_class_name> ": " <exception_message>
    jclass exceptionClass = (*env)->GetObjectClass(env, thrown);        // always succeeds

    jstring className =
        (jstring) (*env)->CallObjectMethod(env, exceptionClass, JniConstants_Class_getName(env));
    if (className == NULL) {
        ExpandableStringAssign(dst, "<error getting class name>");
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, exceptionClass);
        return false;
    }
    (*env)->DeleteLocalRef(env, exceptionClass);
    exceptionClass = NULL;

    if (!AppendJString(env, className, dst)) {
        ExpandableStringAssign(dst,  "<error getting class name UTF-8>");
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, className);
        return false;
    }
    (*env)->DeleteLocalRef(env, className);
    className = NULL;

    jstring message =
        (jstring) (*env)->CallObjectMethod(env, thrown, JniConstants_Throwable_getMessage(env));
    if (message == NULL) {
        return true;
    }

    bool success = (ExpandableStringAppend(dst, ": ") && AppendJString(env, message, dst));
    if (!success) {
        // Two potential reasons for reaching here:
        //
        // 1. managed heap allocation failure (OOME).
        // 2. native heap allocation failure for the storage in |dst|.
        //
        // Attempt to append failure notification, okay to fail, |dst| contains the class name
        // of |thrown|.
        ExpandableStringAppend(dst, "<error getting message>");
        // Clear OOME if present.
        (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, message);
    message = NULL;
    return success;
}

static jobject NewStringWriter(JNIEnv* env) {
    jclass clazz = (*env)->FindClass(env, "java/io/StringWriter");
    jmethodID init = (*env)->GetMethodID(env, clazz, "<init>", "()V");
    jobject instance = (*env)->NewObject(env, clazz, init);
    (*env)->DeleteLocalRef(env, clazz);
    return instance;
}

static jstring StringWriterToString(JNIEnv* env, jobject stringWriter) {
    jclass clazz = (*env)->FindClass(env, "java/io/StringWriter");
    jmethodID toString = (*env)->GetMethodID(env, clazz, "toString", "()Ljava/lang/String;");
    jobject result = (*env)->CallObjectMethod(env, stringWriter, toString);
    (*env)->DeleteLocalRef(env, clazz);
    return (jstring) result;
}

static jobject NewPrintWriter(JNIEnv* env, jobject writer) {
    jclass clazz = (*env)->FindClass(env, "java/io/PrintWriter");
    jmethodID init = (*env)->GetMethodID(env, clazz, "<init>", "(Ljava/io/Writer;)V");
    jobject instance = (*env)->NewObject(env, clazz, init, writer);
    (*env)->DeleteLocalRef(env, clazz);
    return instance;
}

static bool GetStackTrace(JNIEnv* env, jthrowable thrown, struct ExpandableString* dst) {
    // This function is equivalent to the following Java snippet:
    //   StringWriter sw = new StringWriter();
    //   PrintWriter pw = new PrintWriter(sw);
    //   thrown.printStackTrace(pw);
    //   String trace = sw.toString();
    //   return trace;
    jobject sw = NewStringWriter(env);
    if (sw == NULL) {
        return false;
    }

    jobject pw = NewPrintWriter(env, sw);
    if (pw == NULL) {
        (*env)->DeleteLocalRef(env, sw);
        return false;
    }

    (*env)->CallVoidMethod(env, thrown, JniConstants_Throwable_printStackTrace(env), pw);
    jstring trace = StringWriterToString(env, sw);

    (*env)->DeleteLocalRef(env, pw);
    pw = NULL;
    (*env)->DeleteLocalRef(env, sw);
    sw = NULL;

    if (trace == NULL) {
        return false;
    }

    bool success = AppendJString(env, trace, dst);
    (*env)->DeleteLocalRef(env, trace);
    return success;
}

static void GetStackTraceOrSummary(JNIEnv* env, jthrowable thrown, struct ExpandableString* dst) {
    // This method attempts to get a stack trace or summary info for an exception.
    // The exception may be provided in the |thrown| argument to this function.
    // If |thrown| is NULL, then any pending exception is used if it exists.

    // Save pending exception, callees may raise other exceptions. Any pending exception is
    // rethrown when this function exits.
    jthrowable pendingException = (*env)->ExceptionOccurred(env);
    if (pendingException != NULL) {
        (*env)->ExceptionClear(env);
    }

    if (thrown == NULL) {
        if (pendingException == NULL) {
            ExpandableStringAssign(dst,  "<no pending exception>");
            return;
        }
        thrown = pendingException;
    }

    if (!GetStackTrace(env, thrown, dst)) {
        // GetStackTrace may have raised an exception, clear it since it's not for the caller.
        (*env)->ExceptionClear(env);
        GetExceptionSummary(env, thrown, dst);
    }

    if (pendingException != NULL) {
        // Re-throw the pending exception present when this method was called.
        (*env)->Throw(env, pendingException);
        (*env)->DeleteLocalRef(env, pendingException);
    }
}

static void DiscardPendingException(JNIEnv* env, const char* className) {
    jthrowable exception = (*env)->ExceptionOccurred(env);
    (*env)->ExceptionClear(env);
    if (exception == NULL) {
        return;
    }

    struct ExpandableString summary;
    ExpandableStringInitialize(&summary);
    GetExceptionSummary(env, exception, &summary);
    const char* details = (summary.data != NULL) ? summary.data : "Unknown";
    ALOGW("Discarding pending exception (%s) to throw %s", details, className);
    ExpandableStringRelease(&summary);
    (*env)->DeleteLocalRef(env, exception);
}

//
// libnativehelper API
//

int jniRegisterNativeMethods(JNIEnv* env, const char* className,
    const JNINativeMethod* methods, int numMethods)
{
    ALOGV("Registering %s's %d native methods...", className, numMethods);
    jclass clazz = (*env)->FindClass(env, className);
    ALOG_ALWAYS_FATAL_IF(clazz == NULL,
                         "Native registration unable to find class '%s'; aborting...",
                         className);
    int result = (*env)->RegisterNatives(env, clazz, methods, numMethods);
    (*env)->DeleteLocalRef(env, clazz);
    if (result == 0) {
        return 0;
    }

    // Failure to register natives is fatal. Try to report the corresponding exception,
    // otherwise abort with generic failure message.
    jthrowable thrown = (*env)->ExceptionOccurred(env);
    if (thrown != NULL) {
        struct ExpandableString summary;
        ExpandableStringInitialize(&summary);
        if (GetExceptionSummary(env, thrown, &summary)) {
            ALOGF("%s", summary.data);
        }
        ExpandableStringRelease(&summary);
        (*env)->DeleteLocalRef(env, thrown);
    }
    ALOGF("RegisterNatives failed for '%s'; aborting...", className);
    return result;
}

void jniLogException(JNIEnv* env, int priority, const char* tag, jthrowable thrown) {
    struct ExpandableString summary;
    ExpandableStringInitialize(&summary);
    GetStackTraceOrSummary(env, thrown, &summary);
    const char* details = (summary.data != NULL) ? summary.data : "No memory to report exception";
    __android_log_write(priority, tag, details);
    ExpandableStringRelease(&summary);
}

int jniThrowException(JNIEnv* env, const char* className, const char* message) {
    DiscardPendingException(env, className);

    jclass exceptionClass = (*env)->FindClass(env, className);
    if (exceptionClass == NULL) {
        ALOGE("Unable to find exception class %s", className);
        /* ClassNotFoundException now pending */
        return -1;
    }

    int status = 0;
    if ((*env)->ThrowNew(env, exceptionClass, message) != JNI_OK) {
        ALOGE("Failed throwing '%s' '%s'", className, message);
        /* an exception, most likely OOM, will now be pending */
        status = -1;
    }
    (*env)->DeleteLocalRef(env, exceptionClass);

    return status;
}

int jniThrowExceptionFmt(JNIEnv* env, const char* className, const char* fmt, va_list args) {
    char msgBuf[512];
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    return jniThrowException(env, className, msgBuf);
}

int jniThrowNullPointerException(JNIEnv* env, const char* msg) {
    return jniThrowException(env, "java/lang/NullPointerException", msg);
}

int jniThrowRuntimeException(JNIEnv* env, const char* msg) {
    return jniThrowException(env, "java/lang/RuntimeException", msg);
}

int jniThrowIOException(JNIEnv* env, int errnum) {
    char buffer[80];
    const char* message = platformStrError(errnum, buffer, sizeof(buffer));
    return jniThrowException(env, "java/io/IOException", message);
}

jobject jniCreateFileDescriptor(JNIEnv* env, int fd) {
    jobject fileDescriptor = (*env)->NewObject(env,
                                               JniConstants_FileDescriptorClass(env),
                                               JniConstants_FileDescriptor_init(env));
    // NOTE: NewObject ensures that an OutOfMemoryError will be seen by the Java
    // caller if the alloc fails, so we just return nullptr when that happens.
    if (fileDescriptor != NULL)  {
        jniSetFileDescriptorOfFD(env, fileDescriptor, fd);
    }
    return fileDescriptor;
}

int jniGetFDFromFileDescriptor(JNIEnv* env, jobject fileDescriptor) {
    if (fileDescriptor != NULL) {
        return (*env)->GetIntField(env, fileDescriptor,
                                   JniConstants_FileDescriptor_descriptor(env));
    } else {
        return -1;
    }
}

void jniSetFileDescriptorOfFD(JNIEnv* env, jobject fileDescriptor, int value) {
    if (fileDescriptor == NULL) {
        jniThrowNullPointerException(env, "null FileDescriptor");
    } else {
        (*env)->SetIntField(env,
                            fileDescriptor, JniConstants_FileDescriptor_descriptor(env), value);
    }
}

jarray jniGetNioBufferBaseArray(JNIEnv* env, jobject nioBuffer) {
    jclass nioAccessClass = JniConstants_NIOAccessClass(env);
    jmethodID getBaseArrayMethod = JniConstants_NIOAccess_getBaseArray(env);
    jobject object = (*env)->CallStaticObjectMethod(env,
                                                    nioAccessClass, getBaseArrayMethod, nioBuffer);
    return (jarray) object;
}

int jniGetNioBufferBaseArrayOffset(JNIEnv* env, jobject nioBuffer) {
    jclass nioAccessClass = JniConstants_NIOAccessClass(env);
    jmethodID getBaseArrayOffsetMethod = JniConstants_NIOAccess_getBaseArrayOffset(env);
    return (*env)->CallStaticIntMethod(env, nioAccessClass, getBaseArrayOffsetMethod, nioBuffer);
}

jlong jniGetNioBufferPointer(JNIEnv* env, jobject nioBuffer) {
    jlong baseAddress = (*env)->GetLongField(env, nioBuffer, JniConstants_NioBuffer_address(env));
    if (baseAddress != 0) {
        const int position = GetBufferPosition(env, nioBuffer);
        const int shift = GetBufferElementSizeShift(env, nioBuffer);
        baseAddress += position << shift;
    }
    return baseAddress;
}

jlong jniGetNioBufferFields(JNIEnv* env, jobject nioBuffer,
                            jint* position, jint* limit, jint* elementSizeShift) {
    *position = GetBufferPosition(env, nioBuffer);
    *limit = GetBufferLimit(env, nioBuffer);
    *elementSizeShift = GetBufferElementSizeShift(env, nioBuffer);
    return (*env)->GetLongField(env, nioBuffer, JniConstants_NioBuffer_address(env));
}

jobject jniGetReferent(JNIEnv* env, jobject ref) {
    return (*env)->CallObjectMethod(env, ref, JniConstants_Reference_get(env));
}

jstring jniCreateString(JNIEnv* env, const jchar* unicodeChars, jsize len) {
    return (*env)->NewString(env, unicodeChars, len);
}

jobjectArray jniCreateStringArray(C_JNIEnv* env, size_t count) {
    return (*env)->NewObjectArray(env, count, JniConstants_StringClass(env), NULL);
}
