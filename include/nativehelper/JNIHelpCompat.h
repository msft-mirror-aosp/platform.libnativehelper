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

/*
 * DO NOT INCLUDE THIS HEADER IN NEW CODE. IT IS FOR LEGACY COMPATIBILITY ONLY.
 */

#pragma once

#include <jni.h>

__BEGIN_DECLS

/*
 * Returns the UNIX file descriptor as an integer from a
 * java.io.FileDescriptor instance or -1 if the java.io.FileDescriptor
 * is NULL.
 *
 * Exported by libnativehelper_compat_libc++.so.
 *
 * Note:
 *
 * This method exists solely for NetworkStack and Tethering until a better
 * solution can be found (b/158749603).
 *
 * In Android S, libnativehelper_compat_libc++.so had methods depending on
 * private Java API surfaces removed and those methods are only available in
 * libnativehelper.so. From Android S, libnativehelper.so is a public
 * library that is distributed in ART module and so we can ensure the
 * private API it depends on is compatible with the current Java core
 * libraries that are also part of the ART module.
 *
 * jniGetFDFromFileDescriptor() could not be removed from
 * libnativehelper_compat_libc++.so because NetworkStack and Tethering need
 * to be compatible with Android versions Q and R. On these releases,
 * libnativehelper.so is not available to these modules, ie NetworkStack
 * ships as an APK, so effectively an app.
 */
int jniGetFDFromFileDescriptor(C_JNIEnv* env, jobject fileDescriptor);

/*
 * Returns the UNIX file descriptor as an integer from a
 * java.io.FileDescriptor instance or -1 if the java.io.FileDescriptor
 * is NULL.
 *
 * Exported by libnativehelper_compat_libc++.so
 *
 * Note:
 *
 * This method exists primarily for testing purposes. It is the
 * implementation used by jniGetFDFromFileDescriptor() exported by
 * libnativehelper_compat_libc++.so.
 *
 * This method exists to make the tested surface area explicit in the
 * CtsLibnativehelperTestCases and ensure the compatibility version is
 * tested. The symbol jniGetFDFromFileDescriptor() is exported by both
 * libnativehelper_compat_libc++.so and libnativehelper.so and the test
 * harness depends on libnativehelper_compat_libc++.so, but most of the
 * tests cover libnativehelper.so.
 */
int jniGetFDFromFileDescriptor_QR(C_JNIEnv* env, jobject fileDescriptor);

__END_DECLS

/*
 * For C++ code, we provide inlines that map to the C functions.  g++ always
 * inlines these, even on non-optimized builds.
 */
#if defined(__cplusplus)

inline int jniGetFDFromFileDescriptor(JNIEnv* env, jobject fileDescriptor) {
    return jniGetFDFromFileDescriptor(&env->functions, fileDescriptor);
}

inline int jniGetFDFromFileDescriptor_QR(JNIEnv* env, jobject fileDescriptor) {
    return jniGetFDFromFileDescriptor_QR(&env->functions, fileDescriptor);
}

#endif  // defined(__cplusplus)
