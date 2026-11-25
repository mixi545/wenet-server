//
// Created by sensen on 2024/9/10.
//

#ifndef SHERPA_ONNX_JNI_UTILS_H
#define SHERPA_ONNX_JNI_UTILS_H

#include <jni.h>
#include <android/log.h>

#ifdef JNI_AUTHORORIZATION
//#include "http_post.h"
#endif

#include "cJSON.h"
#include "author_verify.h"
#include <iostream>
#include <fstream>
#include <unistd.h>

#define REQUEST_DATA_LENGTH 1024          // Request data max length
#define LICENSE_DATA_LENGTH 1024          // License data max length


#define RETCODE_ERR_NEW     1
#define RETCODE_OK                    0
#define RETCODE_ERRR_SEND             -1
#define RETCODE_ERR_PARSE             -2
#define RETCODE_ERRR_NET              -3
#define RETCODE_ERRR_JSON             -4
#define RETCODE_ERRR_BASE64_DEC       -5
#define RETCODE_ERR_FILED_NUM         -6
#define RETCODE_ERR_DEVICE            -7
#define RETCODE_ERR_EXPIRED           -8
#define RETCODE_ERR_NO_WRT_PERMISSION -9
#define RETCODE_ERR_NO_LICENSE        -10
#define RETCODE_ERR_SERVER_RETCODE    -11
#define RETCODE_ERR_LICENSE_EMPTY     -12

#define UNUSED(x) (void)(x)


#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,     TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,     TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,     TAG, __VA_ARGS__)

//#define MAX_RETRY_NUM 3

extern AuthorVerify *g_pAuthorVerify;

void set_jni_ooffset(long offset);
void set_jni_llength(long length);
void set_jni_package_name(const char* package_name);
//void set_jni_obf_key(std::string obf_key);


long get_jni_ooffset();
long get_jni_llength();
std::string get_jni_package_name();

bool file_exists(const std::string& path);
bool extract_range(const std::string& src, const std::string& dst,
                long offset, long length);

JNIEXPORT jobject JNICALL
getApplication(JNIEnv *env, jobject thiz);

JNIEXPORT jobject JNICALL
getGlobalContext(JNIEnv* env,jobject thiz);

JNIEXPORT jstring JNICALL
getPackageName(JNIEnv *env,jobject thiz);

JNIEXPORT jstring JNICALL
getAndroidId(JNIEnv* env,jobject thiz);

#ifdef TTT
JNIEXPORT jint JNICALL
isActivated(JNIEnv *env, jobject thiz);

JNIEXPORT jlong JNICALL
initAuthorization(JNIEnv *env, jobject thiz, jstring token);

JNIEXPORT jboolean JNICALL
verifyLicense(JNIEnv *env, jobject thiz);

int request_license(const char* req_data, char* ret_license, size_t max_size);
#endif

JNIEXPORT  jint JNICALL
authorVerify(JNIEnv *env, jobject thiz);

std::string get_obfuse_key(JNIEnv *env, jobject thiz);
void deobfuse_model(JNIEnv *env, jobject thiz, std::string model_dir);
void delete_deobfuse_model(JNIEnv *env, jobject thiz, std::string model_dir);
#endif  // SHERPA_ONNX_JNI_UTILS_H
