// sdk-helper.cpp
#include "sherpa-onnx/csrc/sdk-helper.h"

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <charconv>
#include <filesystem>
#include <algorithm>

#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/openssl_tools.h"

#define AUTHOR_FILED_NUM 3
#define LICENSE_DATA_LENGTH 1024

std::string license_data;

#if !defined(ANDROID)
std::vector<std::string> get_macs() {
  std::vector<std::string> macs;
  /*
  for (auto& entry : std::filesystem::directory_iterator("/sys/class/net")) {
    std::ifstream addr(entry.path() / "address");
    std::string mac;
    if (std::getline(addr, mac)){
      macs.push_back(mac)
      std::cout << entry.path().filename().string() << " : " << mac << '\n';
    }
  }
  */
  
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return macs;

  struct if_nameindex* ni = if_nameindex();
  if (!ni) return macs;

  for (int i = 0; ni[i].if_index != 0; ++i) {
    struct ifreq ifr{};
    strncpy(ifr.ifr_name, ni[i].if_name, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
      unsigned char* mac = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
      char buf[32];
      snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      macs.push_back(std::string(buf));

      //std::cout << "macs: " << buf << '\n';
    }
  }
  if_freenameindex(ni);
  close(fd);
  return macs;
}
#endif

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


static bool is_exists(const char* license_file, std::string &license_data){
  FILE *ftmp = fopen(license_file,"r");
  if (!ftmp)
    return false;
  char tmp_str[LICENSE_DATA_LENGTH] = {"\0"};
  fgets(tmp_str, LICENSE_DATA_LENGTH, ftmp);
  license_data = std::string(tmp_str);
  fclose(ftmp);
  return true;
}

std::vector<std::string> decrypt_license(std::string& data) {
  std::vector<std::string> license_keys;

  std::string rsa_data = OpensslTools::Base64Dencode(data.c_str());
  if (rsa_data.size() <= 0) {
    std::cout << "uthorRetCode::RETCODE_ERRR_BASE64_DEC";
    return license_keys;
  }
  std::string license_str = OpensslTools::RsaPubDecrypt(rsa_data);
  //std::cout << "Decrypt data: " << license_str << "\n";
  license_keys = split(license_str, "|||");
  return license_keys;
}

static bool is_license_expired(const std::string& expire_date) {
  int year, month, day;
  if (sscanf(expire_date.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
    std::cout << "AuthorRetCode::RETCODE_ERR_EXPIRED;";
    return true;
  }

  tm expire_tm = {0};
  expire_tm.tm_year = year - 1900;
  expire_tm.tm_mon = month - 1;
  expire_tm.tm_mday = day + 1;
  expire_tm.tm_hour = 0;
  expire_tm.tm_min = 0;
  expire_tm.tm_sec = 0;
  expire_tm.tm_isdst = -1;

  time_t expire_exclusive = mktime(&expire_tm);
  if (expire_exclusive == (time_t)-1) {
    std::cout << "AuthorRetCode::RETCODE_ERR_EXPIRED;";
    return true;
  }

  time_t now;
  time(&now);
  return now >= expire_exclusive;
}

bool valid_check(const char* license_file, const std::string device_id) {
  if (!is_exists(license_file, license_data)) return false;

  std::vector<std::string> license_keys = decrypt_license(license_data);
  // 1.Compare
  if (license_keys.size() != 4) {
    // license_key[0]: mac_id
    // license_key[1]: expiration date
    // license_key[2]: max connections
    // license_key[3]: model obfuscation key
    //        LOGI("SDK license data has invalid field number (%lu != 4)!!!", license_keys.size());
    //    _ret_code = AuthorRetCode::RETCODE_ERR_FILED_NUM;
    std::cout << "AuthorRetCode::RETCODE_ERR_FILED_NUM";
    return false;
  }

#if !defined(ANDROID)
  std::vector<std::string> macs = get_macs();
  bool exist = std::find(macs.begin(), macs.end(), license_keys[0]) != macs.end();
  if (!exist) {
    std::cout << "AuthorRetCode::RETCODE_ERR_MAC;";
    return false;
  }
#endif

  // 2. Treat license_keys[1] as the license expiration date.
  if (is_license_expired(license_keys[1])) {
    std::cout << "AuthorRetCode::RETCODE_ERR_EXPIRED;";
    return false;
  }

  return true;
}

std::string get_license_key() {
  std::string key = "123456";
  if (license_data.empty()) return key;
  
  std::vector<std::string> license_keys = decrypt_license(license_data);
  if (license_keys.size() != 4)
    return key;
  
  return license_keys[3];
}

int get_max_connection() {
  int max_connection = 10;
  if (license_data.empty()) return max_connection;
  
  std::vector<std::string> license_keys = decrypt_license(license_data);
  if (license_keys.size() != 4)
    return max_connection;
  
  return std::stoi(license_keys[2]);
}

/*
namespace sherpa_ncnn {

SHERPA_NCNN_EXTERN_C
JNIEXPORT jboolean JNICALL
Java_com_newtranx_nlp_onnx_SdkHelper_00024Companion_isValidLicense(JNIEnv *env, jobject thiz) {

  jstring packname_str = getPackageName(env, thiz);
  jstring android_str = getAndroidId(env, thiz);

  const char *packname_chars = env->GetStringUTFChars(packname_str, NULL);
  const char *android_chars = env->GetStringUTFChars(android_str, NULL);

  char license_path[256] = {'\0'};
  snprintf(license_path, sizeof(license_path), "/data/data/%s/.license",
           packname_chars);

  set_jni_package_name(packname_chars);
  
  env->ReleaseStringUTFChars(android_str, android_chars);
  env->ReleaseStringUTFChars(packname_str, packname_chars);

  return valid_check(license_path, android_chars);
}

}
*/
