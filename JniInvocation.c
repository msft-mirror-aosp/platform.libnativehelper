/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include "nativehelper/libnativehelper_api.h"

#include "jni.h"

#define LOG_TAG "JniInvocation"
#include <log/log.h>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include <stdbool.h>
#include <string.h>

#include "DlHelp.h"

// Name of fallback library.
static const char* kLibraryFallback = "libart.so";

struct JniInvocationImpl {
  // Name of library providing JNI_ method implementations.
  const char* jni_provider_library_name;

  // Opaque pointer to shared library from dlopen / LoadLibrary.
  void* jni_provider_library;

  // Function pointers to methods in JNI provider.
  jint (*JNI_GetDefaultJavaVMInitArgs)(void*);
  jint (*JNI_CreateJavaVM)(JavaVM**, JNIEnv**, void*);
  jint (*JNI_GetCreatedJavaVMs)(JavaVM**, jsize, jsize*);
};

static struct JniInvocationImpl g_impl;

//
// Internal helpers.
//

#define UNUSED(x) (x) = (x)

static int IsDebuggable() {
#ifdef __ANDROID__
  char debuggable[PROP_VALUE_MAX] = {0};
  __system_property_get("ro.debuggable", debuggable);
  return strcmp(debuggable, "1") == 0;
#else
  return 0;
#endif
}

static int GetLibrarySystemProperty(char* buffer) {
#ifdef __ANDROID__
  return __system_property_get("persist.sys.dalvik.vm.lib.2", buffer);
#else
  UNUSED(buffer);
  return 0;
#endif
}

static DlSymbol FindSymbol(DlLibrary library, const char* symbol) {
  DlSymbol s = DlGetSymbol(library, symbol);
  if (s == NULL) {
    ALOGE("Failed to find symbol: %s", symbol);
  }
  return s;
}

//
// Exported functions for JNI based VM management from JNI spec.
//

jint JNI_GetDefaultJavaVMInitArgs(void* vmargs) {
  return g_impl.JNI_GetDefaultJavaVMInitArgs(vmargs);
}

jint JNI_CreateJavaVM(JavaVM** p_vm, JNIEnv** p_env, void* vm_args) {
  return g_impl.JNI_CreateJavaVM(p_vm, p_env, vm_args);
}

jint JNI_GetCreatedJavaVMs(JavaVM** vms, jsize size, jsize* vm_count) {
  return g_impl.JNI_GetCreatedJavaVMs(vms, size, vm_count);
}

//
// JniInvocation functions for setting up JNI functions.
//

const char* JniInvocationGetLibraryWith(const char* library, char* buffer,
                                        int (*is_debuggable)(),
                                        int (*get_library_system_property)(char* buffer)) {
#ifdef __ANDROID__
  const char* default_library;

  if (!is_debuggable()) {
    // Not a debuggable build.
    // Do not allow arbitrary library. Ignore the library parameter. This
    // will also ignore the default library, but initialize to fallback
    // for cleanliness.
    library = kLibraryFallback;
    default_library = kLibraryFallback;
  } else {
    // Debuggable build.
    // Accept the library parameter. For the case it is NULL, load the default
    // library from the system property.
    if (buffer != NULL) {
      if (get_library_system_property(buffer) > 0) {
        default_library = buffer;
      } else {
        default_library = kLibraryFallback;
      }
    } else {
      // No buffer given, just use default fallback.
      default_library = kLibraryFallback;
    }
  }
#else
  UNUSED(buffer);
  UNUSED(is_debuggable);
  UNUSED(get_library_system_property);
  const char* default_library = kLibraryFallback;
#endif
  if (library == NULL) {
    library = default_library;
  }

  return library;
}

const char* JniInvocationGetLibrary(const char* library, char* buffer) {
  return JniInvocationGetLibraryWith(library, buffer, IsDebuggable, GetLibrarySystemProperty);
}

struct JniInvocationImpl* JniInvocationCreate() {
  // Android only supports a single JniInvocation instance and only a single JavaVM.
  if (g_impl.jni_provider_library != NULL) {
    return NULL;
  }
  return &g_impl;
}

bool JniInvocationInit(struct JniInvocationImpl* instance, const char* library_name) {
#ifdef __ANDROID__
  char buffer[PROP_VALUE_MAX];
#else
  char* buffer = NULL;
#endif
  library_name = JniInvocationGetLibrary(library_name, buffer);
  DlLibrary library = DlOpenLibrary(library_name);
  if (library == NULL) {
    if (strcmp(library_name, kLibraryFallback) == 0) {
      // Nothing else to try.
      ALOGE("Failed to dlopen %s: %s", library_name, DlGetError());
      return false;
    }
    // Note that this is enough to get something like the zygote
    // running, we can't property_set here to fix this for the future
    // because we are root and not the system user. See
    // RuntimeInit.commonInit for where we fix up the property to
    // avoid future fallbacks. http://b/11463182
    ALOGW("Falling back from %s to %s after dlopen error: %s",
          library_name, kLibraryFallback, DlGetError());
    library_name = kLibraryFallback;
    library = DlOpenLibrary(library_name);
    if (library == NULL) {
      ALOGE("Failed to dlopen %s: %s", library, DlGetError());
      return false;
    }
  }

  DlSymbol JNI_GetDefaultJavaVMInitArgs_ = FindSymbol(library, "JNI_GetDefaultJavaVMInitArgs");
  if (JNI_GetDefaultJavaVMInitArgs_ == NULL) {
    return false;
  }

  DlSymbol JNI_CreateJavaVM_ = FindSymbol(library, "JNI_CreateJavaVM");
  if (JNI_CreateJavaVM_ == NULL) {
    return false;
  }

  DlSymbol JNI_GetCreatedJavaVMs_ = FindSymbol(library, "JNI_CreateJavaVM");
  if (JNI_GetCreatedJavaVMs_ == NULL) {
    return false;
  }

  instance->jni_provider_library_name = library_name;
  instance->jni_provider_library = library;
  instance->JNI_GetDefaultJavaVMInitArgs = (jint (*)(void *)) JNI_GetDefaultJavaVMInitArgs_;
  instance->JNI_CreateJavaVM = (jint (*)(JavaVM**, JNIEnv**, void*)) JNI_CreateJavaVM_;
  instance->JNI_GetCreatedJavaVMs = (jint (*)(JavaVM**, jsize, jsize*)) JNI_GetCreatedJavaVMs;

  return true;
}

void JniInvocationDestroy(struct JniInvocationImpl* instance) {
  DlCloseLibrary(instance->jni_provider_library);
  memset(instance, 0, sizeof(*instance));
}
