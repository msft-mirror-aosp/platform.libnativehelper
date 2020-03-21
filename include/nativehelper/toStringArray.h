/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef LIBNATIVEHELPER_INCLUDE_NATIVEHELPER_TOSTRINGARRAY_H_
#define LIBNATIVEHELPER_INCLUDE_NATIVEHELPER_TOSTRINGARRAY_H_

#include "libnativehelper_api.h"

#ifdef __cplusplus

#include <string>
#include <vector>
#include "ScopedLocalRef.h"

template <typename StringVisitor>
jobjectArray toStringArray(JNIEnv* env, size_t count, StringVisitor&& visitor) {
    jobjectArray result = jniCreateStringArray(env, count);
    if (result == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < count; ++i) {
        ScopedLocalRef<jstring> s(env, env->NewStringUTF(visitor(i)));
        if (env->ExceptionCheck()) {
            return nullptr;
        }
        env->SetObjectArrayElement(result, i, s.get());
        if (env->ExceptionCheck()) {
            return nullptr;
        }
    }
    return result;
}

inline jobjectArray toStringArray(JNIEnv* env, const std::vector<std::string>& strings) {
    return toStringArray(env, strings.size(), [&strings](size_t i) { return strings[i].c_str(); });
}

inline jobjectArray toStringArray(JNIEnv* env, const char* const* strings) {
    size_t count = 0;
    for (; strings[count] != nullptr; ++count) {}
    return toStringArray(env, count, [&strings](size_t i) { return strings[i]; });
}

template <typename Counter, typename Getter>
jobjectArray toStringArray(JNIEnv* env, Counter* counter, Getter* getter) {
    return toStringArray(env, counter(), [getter](size_t i) { return getter(i); });
}

#endif  // __cplusplus

#endif  // LIBNATIVEHELPER_INCLUDE_NATIVEHELPER_TOSTRINGARRAY_H_
