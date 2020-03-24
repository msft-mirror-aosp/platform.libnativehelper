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

#include "nativehelper/JNIHelp.h"

#include <string.h>

#include <cstring>
#include <string>

#define LOG_TAG "JNIHelp"
#include "ALog-priv.h"

#include "jni.h"
#include "JniConstants.h"
#include "nativehelper/scoped_local_ref.h"

namespace {

/*
 * Returns a human-readable summary of an exception object.  The buffer will
 * be populated with the "binary" class name and, if present, the
 * exception message.
 */
bool getExceptionSummary(JNIEnv* e, jthrowable exception, std::string& result) {
    // Get the name of the exception's class.
    ScopedLocalRef<jclass> exceptionClass(e, e->GetObjectClass(exception)); // can't fail
    ScopedLocalRef<jclass> classClass(e,      e->GetObjectClass(exceptionClass.get())); // j.l.Class, can't fail
    jmethodID classGetNameMethod = e->GetMethodID( classClass.get(), "getName",
                                                   "()Ljava/lang/String;");
    ScopedLocalRef<jstring> classNameStr(e,
            (jstring) e->CallObjectMethod(exceptionClass.get(), classGetNameMethod));
    if (classNameStr.get() == nullptr) {
        e->ExceptionClear();
        result = "<error getting class name>";
        return false;
    }
    const char* classNameChars = e->GetStringUTFChars(classNameStr.get(), nullptr);
    if (classNameChars == nullptr) {
        e->ExceptionClear();
        result = "<error getting class name UTF-8>";
        return false;
    }
    result += classNameChars;
    e->ReleaseStringUTFChars(classNameStr.get(), classNameChars);

    /* if the exception has a detail message, get that */
    jmethodID getMessage =
        e->GetMethodID(exceptionClass.get(), "getMessage", "()Ljava/lang/String;");
    ScopedLocalRef<jstring> messageStr(e,
            (jstring) e->CallObjectMethod(exception, getMessage));
    if (messageStr.get() == nullptr) {
        return true;
    }

    result += ": ";

    const char* messageChars = e->GetStringUTFChars(messageStr.get(), nullptr);
    if (messageChars != nullptr) {
        result += messageChars;
        e->ReleaseStringUTFChars(messageStr.get(), messageChars);
    } else {
        result += "<error getting message>";
        e->ExceptionClear(); // clear OOM
    }

    return true;
}

/*
 * Returns an exception (with stack trace) as a string.
 */
bool getStackTrace(JNIEnv* e, jthrowable exception, std::string& result) {
    ScopedLocalRef<jclass> stringWriterClass(e, e->FindClass("java/io/StringWriter"));
    if (stringWriterClass.get() == nullptr) {
        return false;
    }

    jmethodID stringWriterCtor = e->GetMethodID(stringWriterClass.get(), "<init>", "()V");
    jmethodID stringWriterToStringMethod =
        e->GetMethodID(stringWriterClass.get(), "toString", "()Ljava/lang/String;");

    ScopedLocalRef<jclass> printWriterClass(e, e->FindClass("java/io/PrintWriter"));
    if (printWriterClass.get() == nullptr) {
        return false;
    }

    jmethodID printWriterCtor =
            e->GetMethodID(printWriterClass.get(), "<init>", "(Ljava/io/Writer;)V");

    ScopedLocalRef<jobject> stringWriter(e,
            e->NewObject(stringWriterClass.get(), stringWriterCtor));
    if (stringWriter.get() == nullptr) {
        return false;
    }

    ScopedLocalRef<jobject> printWriter(e,
            e->NewObject(printWriterClass.get(), printWriterCtor, stringWriter.get()));
    if (printWriter.get() == nullptr) {
        return false;
    }

    ScopedLocalRef<jclass> exceptionClass(e, e->GetObjectClass(exception)); // can't fail
    jmethodID printStackTraceMethod =
            e->GetMethodID(exceptionClass.get(), "printStackTrace", "(Ljava/io/PrintWriter;)V");
    e->CallVoidMethod(exception, printStackTraceMethod, printWriter.get());

    if (e->ExceptionCheck()) {
        return false;
    }

    ScopedLocalRef<jstring> messageStr(e,
            (jstring) e->CallObjectMethod(stringWriter.get(), stringWriterToStringMethod));
    if (messageStr.get() == nullptr) {
        return false;
    }

    const char* utfChars = e->GetStringUTFChars( messageStr.get(), nullptr);
    if (utfChars == nullptr) {
        return false;
    }

    result = utfChars;

    e->ReleaseStringUTFChars(messageStr.get(), utfChars);
    return true;
}

std::string jniGetStackTrace(JNIEnv* e, jthrowable exception) {
    ScopedLocalRef<jthrowable> currentException(e, e->ExceptionOccurred());
    if (exception == nullptr) {
        exception = currentException.get();
        if (exception == nullptr) {
          return "<no pending exception>";
        }
    }

    if (currentException.get() != nullptr) {
        e->ExceptionClear();
    }

    std::string trace;
    if (!getStackTrace(e, exception, trace)) {
        e->ExceptionClear();
        getExceptionSummary(e, exception, trace);
    }

    if (currentException.get() != nullptr) {
        e->Throw(currentException.get()); // re-throw
    }

    return trace;
}

// Note: glibc has a nonstandard strerror_r that returns char* rather than POSIX's int.
// char *strerror_r(int errnum, char *buf, size_t n);
//
// Some versions of bionic support the glibc style call. Since the set of defines that determine
// which version is used is byzantine in its complexity we will just use this C++ template hack to
// select the correct jniStrError implementation based on the libc being used.

using GNUStrError = char* (*)(int,char*,size_t);
using POSIXStrError = int (*)(int,char*,size_t);

inline const char* realJniStrError(GNUStrError func, int errnum, char* buf, size_t buflen) {
    return func(errnum, buf, buflen);
}

inline const char* realJniStrError(POSIXStrError func, int errnum, char* buf, size_t buflen) {
    int rc = func(errnum, buf, buflen);
    if (rc != 0) {
        // (POSIX only guarantees a value other than 0. The safest
        // way to implement this function is to use C++ and overload on the
        // type of strerror_r to accurately distinguish GNU from POSIX.)
        snprintf(buf, buflen, "errno %d", errnum);
    }
    return buf;
}

static const char* platformStrError(int errnum, char* buf, size_t buflen) {
#ifdef _WIN32
    strerror_s(buf, buflen, errnum);
    return buf;
#else
    return realJniStrError(strerror_r, errnum, buf, buflen);
#endif
}

}  // namespace

int jniRegisterNativeMethods(C_JNIEnv* env, const char* className,
    const JNINativeMethod* gMethods, int numMethods)
{
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);

    ALOGV("Registering %s's %d native methods...", className, numMethods);

    ScopedLocalRef<jclass> c(e, e->FindClass(className));
    ALOG_ALWAYS_FATAL_IF(c.get() == nullptr,
                         "Native registration unable to find class '%s'; aborting...",
                         className);

    int result = e->RegisterNatives(c.get(), gMethods, numMethods);
    ALOG_ALWAYS_FATAL_IF(result < 0, "RegisterNatives failed for '%s'; aborting...",
                         className);

    return 0;
}

int jniThrowException(C_JNIEnv* env, const char* className, const char* msg) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);

    if (e->ExceptionCheck()) {
        /* TODO: consider creating the new exception with this as "cause" */
        ScopedLocalRef<jthrowable> exception(e, e->ExceptionOccurred());
        e->ExceptionClear();

        if (exception.get() != nullptr) {
            std::string text;
            getExceptionSummary(e, exception.get(), text);
            ALOGW("Discarding pending exception (%s) to throw %s", text.c_str(), className);
        }
    }

    ScopedLocalRef<jclass> exceptionClass(e, e->FindClass(className));
    if (exceptionClass.get() == nullptr) {
        ALOGE("Unable to find exception class %s", className);
        /* ClassNotFoundException now pending */
        return -1;
    }

    if (e->ThrowNew(exceptionClass.get(), msg) != JNI_OK) {
        ALOGE("Failed throwing '%s' '%s'", className, msg);
        /* an exception, most likely OOM, will now be pending */
        return -1;
    }

    return 0;
}

int jniThrowExceptionFmt(C_JNIEnv* env, const char* className, const char* fmt, va_list args) {
    char msgBuf[512];
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    return jniThrowException(env, className, msgBuf);
}

int jniThrowNullPointerException(C_JNIEnv* env, const char* msg) {
    return jniThrowException(env, "java/lang/NullPointerException", msg);
}

int jniThrowRuntimeException(C_JNIEnv* env, const char* msg) {
    return jniThrowException(env, "java/lang/RuntimeException", msg);
}

int jniThrowIOException(C_JNIEnv* env, int errnum) {
    char buffer[80];
    const char* message = platformStrError(errnum, buffer, sizeof(buffer));
    return jniThrowException(env, "java/io/IOException", message);
}

void jniLogException(C_JNIEnv* env, int priority, const char* tag, jthrowable exception) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    std::string trace(jniGetStackTrace(e, exception));
    __android_log_write(priority, tag, trace.c_str());
}

jobject jniCreateFileDescriptor(C_JNIEnv* env, int fd) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    jobject fileDescriptor = e->NewObject(JniConstants::GetFileDescriptorClass(e),
                                          JniConstants::GetFileDescriptorInitMethod(e));
    // NOTE: NewObject ensures that an OutOfMemoryError will be seen by the Java
    // caller if the alloc fails, so we just return nullptr when that happens.
    if (fileDescriptor != nullptr)  {
        jniSetFileDescriptorOfFD(env, fileDescriptor, fd);
    }
    return fileDescriptor;
}

int jniGetFDFromFileDescriptor(C_JNIEnv* env, jobject fileDescriptor) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    if (fileDescriptor != nullptr) {
        return e->GetIntField(fileDescriptor,
                              JniConstants::GetFileDescriptorDescriptorField(e));
    } else {
        return -1;
    }
}

void jniSetFileDescriptorOfFD(C_JNIEnv* env, jobject fileDescriptor, int value) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    if (fileDescriptor == nullptr) {
        jniThrowNullPointerException(e, "null FileDescriptor");
    } else {
        e->SetIntField(fileDescriptor, JniConstants::GetFileDescriptorDescriptorField(e), value);
    }
}

jlong jniGetOwnerIdFromFileDescriptor(C_JNIEnv* env, jobject fileDescriptor) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    return e->GetLongField(fileDescriptor, JniConstants::GetFileDescriptorOwnerIdField(e));
}

jarray jniGetNioBufferBaseArray(C_JNIEnv* env, jobject nioBuffer) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    jclass nioAccessClass = JniConstants::GetNioAccessClass(e);
    jmethodID getBaseArrayMethod = JniConstants::GetNioAccessGetBaseArrayMethod(e);
    jobject object = e->CallStaticObjectMethod(nioAccessClass, getBaseArrayMethod, nioBuffer);
    return static_cast<jarray>(object);
}

int jniGetNioBufferBaseArrayOffset(C_JNIEnv* env, jobject nioBuffer) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    jclass nioAccessClass = JniConstants::GetNioAccessClass(e);
    jmethodID getBaseArrayOffsetMethod = JniConstants::GetNioAccessGetBaseArrayOffsetMethod(e);
    return e->CallStaticIntMethod(nioAccessClass, getBaseArrayOffsetMethod, nioBuffer);
}

jlong jniGetNioBufferPointer(C_JNIEnv* env, jobject nioBuffer) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    jlong baseAddress = e->GetLongField(nioBuffer, JniConstants::GetNioBufferAddressField(e));
    if (baseAddress != 0) {
      const int position = e->GetIntField(nioBuffer, JniConstants::GetNioBufferPositionField(e));
      const int shift =
          e->GetIntField(nioBuffer, JniConstants::GetNioBufferElementSizeShiftField(e));
      baseAddress += position << shift;
    }
    return baseAddress;
}

jlong jniGetNioBufferFields(C_JNIEnv* env, jobject nioBuffer,
                            jint* position, jint* limit, jint* elementSizeShift) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    *position = e->GetIntField(nioBuffer, JniConstants::GetNioBufferPositionField(e));
    *limit = e->GetIntField(nioBuffer, JniConstants::GetNioBufferLimitField(e));
    *elementSizeShift =
        e->GetIntField(nioBuffer, JniConstants::GetNioBufferElementSizeShiftField(e));
    return e->GetLongField(nioBuffer, JniConstants::GetNioBufferAddressField(e));
}

jobject jniGetReferent(C_JNIEnv* env, jobject ref) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    return e->CallObjectMethod(ref, JniConstants::GetReferenceGetMethod(e));
}

jstring jniCreateString(C_JNIEnv* env, const jchar* unicodeChars, jsize len) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    return e->NewString(unicodeChars, len);
}

jobjectArray jniCreateStringArray(C_JNIEnv* env, size_t count) {
    JNIEnv* e = reinterpret_cast<JNIEnv*>(env);
    return e->NewObjectArray(count, JniConstants::GetStringClass(e), nullptr);
}

void jniUninitializeConstants() {
  JniConstants::Uninitialize();
}
