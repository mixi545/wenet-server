//
// Created by liangss on 2024/9/29.
//

#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/jni/common.h"

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "sherpa-onnx/csrc/openssl_tools.h"
#include "jni_utils.h"


#define AUTHOR_FILED_NUM 4

#define AUTHOR_VERIFY_INTERVAL 30

#define LICENSE_DATA_LENGTH 1024


#define TAG "SDKHelper"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,     TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,     TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,     TAG, __VA_ARGS__)


static std::string current_date() {
  char time_buffer[30] = {0x00};
  struct tm *t;
  time_t tt;
  time(&tt);
  t = localtime(&tt);
  snprintf(time_buffer, sizeof(time_buffer),"%d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

  return std::string(time_buffer);
}

static const time_t convert(int year, int month, int day) {
  tm info = {0};
  info.tm_year = year - 1900;
  info.tm_mon = month - 1;
  info.tm_mday = day;
  return mktime(&info);
}

// split string by delim into vector<string>
static std::vector<std::string> split( std::string const &str, const std::string  delim){
  std::vector<std::string> str_list;
  size_t start;
  size_t end = 0;
  while ((start = str.find_first_not_of(delim, end)) != std::string::npos)
  {
    end = str.find(delim, start);
    str_list.push_back(str.substr(start, end - start));
  }
  return str_list;
}

// compute days betwen two dates in seconds
static int days(const char* from, const char* to)
{
  int year, month, day;
  sscanf(from, "%d-%d-%d", &year, &month, &day);
  int fromSecond = (int) convert(year, month, day);

  sscanf(to, "%d-%d-%d", &year, &month, &day);
  int toSecond = (int) convert(year, month, day);

  return (toSecond - fromSecond) / 24 / 3600;
}

static bool is_exists(const char* license_file, std::string &license_data) {
  FILE *ftmp = fopen(license_file,"r");
  if (!ftmp)
    return false;
  char tmp_str[LICENSE_DATA_LENGTH] = {"\0"};
  fgets(tmp_str, LICENSE_DATA_LENGTH, ftmp);
  license_data = std::string(tmp_str);
  fclose(ftmp);
  return true;
}


static bool valid_check(const char* license_file, const std::string device_id) {
  std::string license_data;
  if (!is_exists(license_file, license_data)) return false;

  std::string rsa_data = OpensslTools::Base64Dencode(license_data.c_str());
  if (rsa_data.size() <= 0) {
    //    _ret_code = AuthorRetCode::RETCODE_ERRR_BASE64_DEC;
    LOGE("uthorRetCode::RETCODE_ERRR_BASE64_DEC");
    return false;
  }

  std::string license_str = OpensslTools::RsaPubDecrypt(rsa_data);

  //LOGE("Decrypt data: %s\n",license_str.c_str());

  std::vector<std::string> license_keys = split(license_str, "|||");

  // 1.Compare
  if (!((license_keys.size() == AUTHOR_FILED_NUM) || (license_keys.size() == 4))) {
    // licese_key[0]:mac_id license_key[1]: init_date license_key[2]:characters
    //        LOGI("SDK license data has invalid filed number (%lu != 3)!!!", license_keys.size());
    //    _ret_code = AuthorRetCode::RETCODE_ERR_FILED_NUM;
    LOGE("AuthorRetCode::RETCODE_ERR_FILED_NUM;");
    return false;
  }

  if (license_keys[0] != "NO_CHECK_DEVICE_ID") {
    if (device_id != license_keys[0]) {
      //                LOGE("The current device id not matched in license(%s != %s)!!!", device_id.c_str(),license_keys[0].c_str());
      //        _ret_code = AuthorRetCode::RETCODE_ERR_DEVICE;
      LOGE("AuthorRetCode::RETCODE_ERR_DEVICE;");
      //LOGE("Real device_id:%s",device_id.c_str());
      //LOGE("License device_id:%s",license_keys[0].c_str());
      return false;
    }
  } else {
    LOGI("CHECK_DEVICE_ID ....");
  }

  // 2.Compare the current date with license date
  std::string now_date = current_date();
  int activate_days = days(current_date().c_str(), license_keys[1].c_str());

  if (activate_days > AUTHOR_VERIFY_INTERVAL) {
    LOGE("AuthorRetCode::RETCODE_ERR_EXPIRED;");
    return false;
  } 

  /*
  LOGE("*** obf_key %s", license_keys[3].data());
  
  if (!license_keys[3].empty()) {
    set_jni_obf_key(license_keys[3]);
    //LOGE("*** obf_key %s", license_keys[3].data());
  }
 
  
  long jni_ooffset = 518L;
  std::from_chars(license_keys[3].data(), 
        license_keys[3].data() + license_keys[3].size(), jni_ooffset);
  set_jni_ooffset(jni_ooffset);

  long jni_llength = 233079284L;
  std::from_chars(license_keys[4].data(), 
        license_keys[4].data() + license_keys[4].size(), jni_llength);
  set_jni_llength(jni_llength);
  */

  return true;
}


SHERPA_ONNX_EXTERN_C
JNIEXPORT jboolean JNICALL
Java_com_k2fsa_sherpa_onnx_SdkHelper_00024Companion_isValidLicense(JNIEnv *env, jobject thiz) {

  jstring packname_str = getPackageName(env,thiz);
  jstring android_str = getAndroidId(env, thiz);

  const char *packname_chars = env->GetStringUTFChars(packname_str, NULL);
  const char *android_chars = env->GetStringUTFChars(android_str, NULL);

  char license_path[256] = {'\0'};
  snprintf(license_path, sizeof(license_path), "/data/data/%s/.license",
           packname_chars);

  env->ReleaseStringUTFChars(android_str, android_chars);
  env->ReleaseStringUTFChars(packname_str, packname_chars);

  return valid_check(license_path, android_chars);
}
