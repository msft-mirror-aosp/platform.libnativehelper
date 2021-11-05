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

#include "include_platform/nativehelper/JNIPlatformHelp.h"

#include <stddef.h>

#include "JniConstants.h"

static int GetBufferPosition(JNIEnv* env, jobject nioBuffer) {
    return(*env)->GetIntField(env, nioBuffer, JniConstants_NioBuffer_position(env));
}

static int GetBufferLimit(JNIEnv* env, jobject nioBuffer) {
    return(*env)->GetIntField(env, nioBuffer, JniConstants_NioBuffer_limit(env));
}

static int GetBufferElementSizeShift(JNIEnv* env, jobject nioBuffer) {
#ifdef __ANDROID__
    return(*env)->GetIntField(env, nioBuffer, JniConstants_NioBuffer__elementSizeShift(env));
#else
    jclass bufferDelegateClass = JniConstants_NioBufferDelegateClass(env);
    jmethodID elementSizeShiftMethod = JniConstants_NioBufferDelegate_elementSizeShift(env);
    return (*env)->CallStaticIntMethod(env, bufferDelegateClass, elementSizeShiftMethod, nioBuffer);
#endif
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
#ifndef __ANDROID__
    // in Java 11, the address field of a HeapByteBuffer contains a non-zero value despite
    // HeapByteBuffer being a non-direct buffer. In that case, this should still return 0.
    jmethodID isDirectMethod = JniConstants_NioBuffer_isDirect(env);
    jboolean isDirect = (*env)->CallBooleanMethod(env, nioBuffer, isDirectMethod);
    if (isDirect == JNI_FALSE) {
        return 0L;
    }
#endif
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
