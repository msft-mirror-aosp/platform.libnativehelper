/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "libnativehelper_lazy.h"

#include <dlfcn.h>
#include <pthread.h>

#include <stdatomic.h>
#include <stdbool.h>

#include "jni.h"
#include "log/log.h"

#include "android/file_descriptor_jni.h"
#include "nativehelper/JNIHelp.h"
#include "nativehelper/JNIPlatformHelp.h"
#include "nativehelper/JniInvocation.h"

// This file provides a lazy interface to libnativehelper.so to address early boot dependencies.
// Specifically bootanimation now runs before the ART APEX is loaded and libnativehelper.so is
// in the ART APEX. bootanimation does not call any code in libnativehelper.

// Method pointers to libnativehelper methods are held in array which simplifies checking
// all pointers are initialized.
enum MethodIndex {
    // NDK file descriptor API in file_descriptor_jni.h.
    k_AFileDescriptor_create,
    k_AFileDescriptor_getFD,
    k_AFileDescriptor_setFD,

    // JNI_Invocation API declared in jni.h.
    k_JNI_CreateJavaVM,
    k_JNI_GetCreatedJavaVMs,
    k_JNI_GetDefaultJavaVMInitArgs,

    // Methods in JNIHelp.h.
    k_jniCreateString,
    k_jniLogException,
    k_jniRegisterNativeMethods,
    k_jniThrowException,
    k_jniThrowExceptionFmt,
    k_jniThrowIOException,
    k_jniThrowNullPointerException,
    k_jniThrowRuntimeException,

    // Methods in JNIPlatformHelp.h.
    k_jniCreateFileDescriptor,
    k_jniGetFDFromFileDescriptor,
    k_jniGetNioBufferBaseArray,
    k_jniGetNioBufferBaseArrayOffset,
    k_jniGetNioBufferFields,
    k_jniGetNioBufferPointer,
    k_jniSetFileDescriptorOfFD,
    k_jniUninitializeConstants,

    // Methods in JniInvocation.h.
    k_JniInvocationCreate,
    k_JniInvocationDestroy,
    k_JniInvocationGetLibrary,
    k_JniInvocationInit,

    // Marker for count of methods
    k_MethodCount
};

// Table of methods pointers in libnativehelper APIs.
static void* g_Methods[k_MethodCount];

//
// Libnativehelper lazy loading.
//

static atomic_bool gPreventLibnativehelperLoading = false;  // Allows tests to block loading.

void PreventLibnativehelperLazyLoadingForTests() {
    atomic_store_explicit(&gPreventLibnativehelperLoading, true, memory_order_release);
}

static void* LoadLibnativehelper(int dlopen_flags) {
    if (atomic_load_explicit(&gPreventLibnativehelperLoading, memory_order_acquire)) {
        return NULL;
    }
    return dlopen("libnativehelper.so", dlopen_flags);
}

static bool IsLibnativehelperLoaded() {
    return LoadLibnativehelper(RTLD_NOLOAD) != NULL;
}

//
// Initialization and symbol binding.
//

static void BindSymbol(void* handle, const char* name, enum MethodIndex index) {
    g_Methods[index] = dlsym(handle, name);
    LOG_FATAL_IF(*symbol == NULL,
                 "Failed to find symbol '%s' in libnativehelper.so: %s", name, dlerror());
}

static void InitializeOnce() {
    void* handle = LoadLibnativehelper(RTLD_NOW);
    LOG_FATAL_IF(handle == NULL, "Failed to load libnativehelper.so: %s", dlerror());

#undef BIND_SYMBOL
#define BIND_SYMBOL(name) BindSymbol(handle, #name, k_ ## name);

    // NDK file descriptor API in file_descriptor_jni.h.
    BIND_SYMBOL(AFileDescriptor_create);
    BIND_SYMBOL(AFileDescriptor_getFD);
    BIND_SYMBOL(AFileDescriptor_setFD);

    // JNI_Invocation API declared in jni.h.
    BIND_SYMBOL(JNI_CreateJavaVM);
    BIND_SYMBOL(JNI_GetCreatedJavaVMs);
    BIND_SYMBOL(JNI_GetDefaultJavaVMInitArgs);

    // Methods in JNIHelp.h.
    BIND_SYMBOL(jniCreateString);
    BIND_SYMBOL(jniLogException);
    BIND_SYMBOL(jniRegisterNativeMethods);
    BIND_SYMBOL(jniThrowException);
    BIND_SYMBOL(jniThrowExceptionFmt);
    BIND_SYMBOL(jniThrowIOException);
    BIND_SYMBOL(jniThrowNullPointerException);
    BIND_SYMBOL(jniThrowRuntimeException);

    // Methods in JNIPlatformHelp.h.
    BIND_SYMBOL(jniCreateFileDescriptor);
    BIND_SYMBOL(jniGetFDFromFileDescriptor);
    BIND_SYMBOL(jniGetNioBufferBaseArray);
    BIND_SYMBOL(jniGetNioBufferBaseArrayOffset);
    BIND_SYMBOL(jniGetNioBufferFields);
    BIND_SYMBOL(jniGetNioBufferPointer);
    BIND_SYMBOL(jniSetFileDescriptorOfFD);
    BIND_SYMBOL(jniUninitializeConstants);

    // Methods in JniInvocation.h.
    BIND_SYMBOL(JniInvocationCreate);
    BIND_SYMBOL(JniInvocationDestroy);
    BIND_SYMBOL(JniInvocationGetLibrary);
    BIND_SYMBOL(JniInvocationInit);

#undef BIND_SYMBOL

    // Check every symbol is bound.
    for (int i = 0; i < k_MethodCount; ++i) {
        LOG_FATAL_IF(g_Methods[i] == NULL,
                    "Uninitialized method in libnativehelper_lazy at index: %d", i);
    }
}

static void EnsureInitialized() {
    static pthread_once_t initialized = PTHREAD_ONCE_INIT;
    pthread_once(&initialized, InitializeOnce);
}

#define INVOKE_METHOD(name, method_type, args...)       \
    do {                                                \
        EnsureInitialized();                            \
        void* method = g_Methods[k_ ## name];           \
        return ((method_type) method)(args);            \
    } while (0)

#define INVOKE_VOID_METHOD(name, method_type, args...)  \
    do {                                                \
        EnsureInitialized();                            \
        void* method = g_Methods[k_ ## name];           \
        ((method_type) method)(args);                   \
    } while (0)

//
// Forwarding for methods in file_descriptor_jni.h.
//

jobject AFileDescriptor_create(JNIEnv* env) {
    typedef jobject (*M)(JNIEnv*);
    INVOKE_METHOD(AFileDescriptor_create, M, env);
}

int AFileDescriptor_getFD(JNIEnv* env, jobject fileDescriptor) {
    typedef int (*M)(JNIEnv*, jobject);
    INVOKE_METHOD(AFileDescriptor_getFD, M, env, fileDescriptor);
}

void AFileDescriptor_setFD(JNIEnv* env, jobject fileDescriptor, int fd) {
    typedef void (*M)(JNIEnv*, jobject, int);
    INVOKE_VOID_METHOD(AFileDescriptor_setFD, M, env, fileDescriptor, fd);
}

//
// Forwarding for the JNI_Invocation API declarded in jni.h.
//

// Some code may attempt to use this JNI_Invocation API to establish if there is a VM (b/174768641).
// Because INVOKE_METHOD produces a fatal error if used before libnativehelper.so, we need some
// additional logic for the JNI_Invocation API to allow JNI_GetCreatedJavaVMs to be called even
// if libnativehelper.so is not loaded.
//
// Consequently, we use an atomic variable if a VM is created through this API. But note
// this is not the only way a JavaVM may be created so checking this flag alone is not enough.
static atomic_bool gJavaVmCreatedLazily = false;

static jint JNI_CreateJavaVMImpl(JavaVM** p_vm, JNIEnv** p_env, void* vm_args) {
    typedef jint (*M)(JavaVM**, JNIEnv**, void*);
    INVOKE_METHOD(JNI_CreateJavaVM, M, p_vm, p_env, vm_args);
}

jint JNI_CreateJavaVM(JavaVM** p_vm, JNIEnv** p_env, void* vm_args) {
    jint status = JNI_CreateJavaVMImpl(p_vm, p_env, vm_args);
    if (status == JNI_OK) {
        atomic_store_explicit(&gJavaVmCreatedLazily, true, memory_order_release);
    }
    return status;
}

jint JNI_GetDefaultJavaVMInitArgs(void* vm_args) {
    typedef jint (*M)(void*);
    INVOKE_METHOD(JNI_GetDefaultJavaVMInitArgs, M, vm_args);
}

jint JNI_GetCreatedJavaVMs(JavaVM** p_vm, jsize vm_max, jsize* p_vm_count) {
    typedef jint (*M)(JavaVM**, jsize, jsize*);
    // If no VMs have been created created lazily and libnativehelper.so has not been loaded
    // by other means, then fill-in the VM count as zero and return JNI_OK.
    if (!atomic_load_explicit(&gJavaVmCreatedLazily, memory_order_acquire) &&
        !IsLibnativehelperLoaded()) {
        *p_vm_count = 0;
        return JNI_OK;
    }
    INVOKE_METHOD(JNI_GetCreatedJavaVMs, M, p_vm, vm_max, p_vm_count);
}

//
// Forwarding for methods in JNIHelp.h.
//

jstring jniCreateString(JNIEnv* env, const jchar* unicodeChars, jsize len) {
    typedef jstring (*M)(JNIEnv*, const jchar*, jsize);
    INVOKE_METHOD(jniCreateString, M, env, unicodeChars, len);
}

void jniLogException(JNIEnv* env, int priority, const char* tag, jthrowable exception) {
    typedef void (*M)(JNIEnv*, int, const char*, jthrowable);
    INVOKE_VOID_METHOD(jniLogException, M, env, priority, tag, exception);
}

int jniRegisterNativeMethods(JNIEnv* env,
                             const char* className,
                             const JNINativeMethod* gMethods,
                             int numMethods) {
    typedef int (*M)(JNIEnv*, const char*, const JNINativeMethod*, int numMethods);
    INVOKE_METHOD(jniRegisterNativeMethods, M, env, className, gMethods, numMethods);
}

int jniThrowException(JNIEnv* env, const char* className, const char* msg) {
    typedef int (*M)(JNIEnv*, const char*, const char*);
    INVOKE_METHOD(jniThrowException, M, env, className, msg);
}

int jniThrowExceptionFmt(JNIEnv* env, const char* className, const char* fmt, va_list args) {
    typedef int (*M)(JNIEnv*, const char*, const char*, va_list);
    INVOKE_METHOD(jniThrowExceptionFmt, M, env, className, fmt, args);
}

int jniThrowIOException(JNIEnv* env, int errnum) {
    typedef int (*M)(JNIEnv*, int);
    INVOKE_METHOD(jniThrowIOException, M, env, errnum);
}

int jniThrowNullPointerException(JNIEnv* env, const char* msg) {
    typedef int (*M)(JNIEnv*, const char*);
    INVOKE_METHOD(jniThrowNullPointerException, M, env, msg);
}

int jniThrowRuntimeException(JNIEnv* env, const char* msg) {
    typedef int (*M)(JNIEnv*, const char*);
    INVOKE_METHOD(jniThrowRuntimeException, M, env, msg);
}

//
// Forwarding for methods in JNIPlatformHelp.h.
//

jobject jniCreateFileDescriptor(JNIEnv* env, int fd) {
    typedef jobject (*M)(JNIEnv*, int);
    INVOKE_METHOD(jniCreateFileDescriptor, M, env, fd);
}

int jniGetFDFromFileDescriptor(JNIEnv* env, jobject fileDescriptor) {
    typedef int (*M)(JNIEnv*, jobject);
    INVOKE_METHOD(jniGetFDFromFileDescriptor, M, env, fileDescriptor);
}

jarray jniGetNioBufferBaseArray(JNIEnv* env, jobject nioBuffer) {
    typedef jarray (*M)(JNIEnv*, jobject);
    INVOKE_METHOD(jniGetNioBufferBaseArray, M, env, nioBuffer);
}

int jniGetNioBufferBaseArrayOffset(JNIEnv* env, jobject nioBuffer) {
    typedef int (*M)(JNIEnv*, jobject);
    INVOKE_METHOD(jniGetNioBufferBaseArrayOffset, M, env, nioBuffer);
}

jlong jniGetNioBufferFields(JNIEnv* env, jobject nioBuffer,
                            jint* position, jint* limit, jint* elementSizeShift) {
    typedef jlong (*M)(JNIEnv*, jobject, jint*, jint*, jint*);
    INVOKE_METHOD(jniGetNioBufferFields, M, env, nioBuffer, position, limit,
                          elementSizeShift);
}

jlong jniGetNioBufferPointer(JNIEnv* env, jobject nioBuffer) {
    typedef jlong (*M)(JNIEnv*, jobject);
    INVOKE_METHOD(jniGetNioBufferPointer, M, env, nioBuffer);
}

void jniSetFileDescriptorOfFD(JNIEnv* env, jobject fileDescriptor, int value) {
    typedef void (*M)(JNIEnv*, jobject, int);
    INVOKE_VOID_METHOD(jniSetFileDescriptorOfFD, M, env, fileDescriptor, value);
}

void jniUninitializeConstants() {
    typedef void (*M)();
    INVOKE_VOID_METHOD(jniUninitializeConstants, M);
}

//
// Forwarding for methods in JniInvocation.h.
//

struct JniInvocationImpl* JniInvocationCreate() {
    typedef struct JniInvocationImpl* (*M)();
    INVOKE_METHOD(JniInvocationCreate, M);
}

void JniInvocationDestroy(struct JniInvocationImpl* instance) {
    typedef void (*M)(struct JniInvocationImpl*);
    INVOKE_METHOD(JniInvocationDestroy, M, instance);
}

bool JniInvocationInit(struct JniInvocationImpl* instance, const char* library) {
    typedef bool (*M)(struct JniInvocationImpl*, const char*);
    INVOKE_METHOD(JniInvocationInit, M, instance, library);
}

const char* JniInvocationGetLibrary(const char* library, char* buffer) {
    typedef const char* (*M)(const char*, char*);
    INVOKE_METHOD(JniInvocationGetLibrary, M, library, buffer);
}