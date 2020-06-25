/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <dlfcn.h>
#include "include_jni/jni.h"
#include <stdbool.h>
#include <stddef.h>

#define LOG_TAG "JNIHelpCompat"
#include "ALog-priv.h"

/*
 * This is compatibility code so the NetworkStack.apk and Tethering mainline module continues to
 * work on Q and R. This code should not be used anywhere else. When this module is no longer
 * required to run on Q and R, this compat code should be removed. Ideally we find a solution
 * in NetworkStack and Tethering instead of here (b/158749603).
 */
int jniGetFDFromFileDescriptor_QR(JNIEnv* env, jobject fileDescriptor) {
    if (fileDescriptor == NULL) {
        return -1;
    }

    static jfieldID g_descriptorFieldID = NULL;
    if (g_descriptorFieldID == NULL) {
        jclass fileDescriptorClass = (*env)->FindClass(env, "java/io/FileDescriptor");
        g_descriptorFieldID = (*env)->GetFieldID(env, fileDescriptorClass, "descriptor", "I");
        (*env)->DeleteLocalRef(env, fileDescriptorClass);
    }

    return (*env)->GetIntField(env, fileDescriptor, g_descriptorFieldID);
}

jint jniGetFDFromFileDescriptor(JNIEnv* env, jobject fileDescriptor) {
    return jniGetFDFromFileDescriptor_QR(env, fileDescriptor);
}
