//
// Created by sensen on 2024/9/10.
//

#include "jni_utils.h"
#include "openssl_tools.h"
#include "sherpa-onnx/csrc/obfuscator_tool.h"

#define AUTHOR_FILED_NUM 4

#define TAG "AuthorVerify"

AuthorVerify *g_pAuthorVerify = NULL;

long jni_ooffset = 0;
long jni_llength = 0;
std::string jni_package_name;
std::string jni_obf_key;
void set_jni_ooffset(long offset) { jni_ooffset = offset; }
void set_jni_llength(long length) { jni_llength = length; }
void set_jni_package_name(const char* package_name) { 
  jni_package_name = std::string(package_name);
}
void set_jni_obf_key(std::string obf_key) { 
  jni_obf_key = obf_key;
}

long get_jni_ooffset() { return jni_ooffset; }
long get_jni_llength() { return jni_llength; }
std::string get_jni_package_name() { return jni_package_name; }

bool file_exists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0; 
}

bool extract_range(const std::string& src,
                   const std::string& dst,
                   long offset,
                   long length) {
  std::ifstream in(src, std::ios::binary);
  if (!in) {
    std::cerr << "Cannot open source file: " << src << '\n';
    return false;
  }

  // 跳转到偏移
  in.seekg(offset);
  if (!in) {
    std::cerr << "Seek failed\n";
    return false;
  }

  std::ofstream out(dst, std::ios::binary);
  if (!out) {
    std::cerr << "Cannot open destination file: " << dst << '\n';
    return false;
  }

  // 分块读写，支持大文件
  constexpr std::size_t kBufSize = 1 << 20;  // 1 MB
  std::vector<char> buffer(kBufSize);

  long remaining = length;
  while (remaining > 0) {
    std::size_t to_read = (remaining > kBufSize) ? kBufSize : remaining;
    in.read(buffer.data(), to_read);
    std::size_t actually_read = in.gcount();
    if (actually_read == 0) break;  // EOF
    out.write(buffer.data(), actually_read);
    remaining -= actually_read;
  }

  return in.good() && out.good();
}

/**
 * get application context
 * @param env
 * @return
 */
JNIEXPORT jobject JNICALL
getApplication(JNIEnv *env, jobject thiz) {

  UNUSED(thiz);
  jclass localClass = env->FindClass("android/app/ActivityThread");
  if (localClass != NULL) {
    jmethodID getapplication = env->GetStaticMethodID(localClass, "currentApplication",
                                                         "()Landroid/app/Application;");
    if (getapplication != NULL) {
      jobject application = env->CallStaticObjectMethod(localClass, getapplication);
      return application;
    }
  }
  return NULL;
}


JNIEXPORT jobject JNICALL
getGlobalContext(JNIEnv* env, jobject thiz) {
  // Get the instance object of Activity Thread
  jclass activityThread = env->FindClass("android/app/ActivityThread");
  jmethodID currentActivityThread =
      env->GetStaticMethodID(activityThread, "currentActivityThread",
                                "()Landroid/app/ActivityThread;");
  if (0 == currentActivityThread) {
    return NULL;
  }
  jobject at =
      env->CallStaticObjectMethod(activityThread, currentActivityThread);
  // Get Application, which is the global Context
  jmethodID getApplication = env->GetMethodID(activityThread, "getApplication",
                                                 "()Landroid/app/Application;");
  if (0 == getApplication) {
    return NULL;
  }
  jobject context = env->CallObjectMethod(at, getApplication);
  return context;
}

/**
 * get package name
 * @param env
 * @return
 */
JNIEXPORT jstring JNICALL
getPackageName(JNIEnv *env,jobject thiz) {
  UNUSED(thiz);
  jobject context = getApplication(env,thiz);
  if (context == NULL) {
    LOGI("getSign: not found context, return default value");
    return env->NewStringUTF("");
  }
  jclass activity = env->GetObjectClass(context);
  // 得到 getPackageManager 方法的 ID
  jmethodID methodID_func = env->GetMethodID(activity, "getPackageManager",
                                                "()Landroid/content/pm/PackageManager;");
  // 获得PackageManager对象
  jobject packageManager = env->CallObjectMethod(context, methodID_func);
  jclass packageManagerclass = env->GetObjectClass(packageManager);
  //得到 getPackageName 方法的 ID
  jmethodID methodID_pack = env->GetMethodID(activity, "getPackageName", "()Ljava/lang/String;");
  //获取包名
  jstring name_str = (jstring)(env->CallObjectMethod(context, methodID_pack));
  return name_str;
}


JNIEXPORT jstring JNICALL
getAndroidId(JNIEnv* env,jobject thiz) {
  // get contentResolver
  jobject instance = getGlobalContext(env,thiz);
  if (NULL == instance) {
    return env->NewStringUTF("");
  }
  jclass activity = env->GetObjectClass(instance);
  jmethodID method = env->GetMethodID(activity, "getContentResolver",
                                         "()Landroid/content/ContentResolver;");
  jobject resolverInstance = env->CallObjectMethod(instance, method);

  // get android_id from android Settings$Secure
  jclass androidSettingsClass =
      env->FindClass("android/provider/Settings$Secure");
  jmethodID methodId =
      env->GetStaticMethodID(androidSettingsClass, "getString",
                                "(Landroid/content/ContentResolver;Ljava/lang/"
                                "String;)Ljava/lang/String;");
  jstring param_android_id = env->NewStringUTF("android_id");
  jstring android_id = (jstring)(env->CallStaticObjectMethod(
      androidSettingsClass, methodId, resolverInstance, param_android_id));
  return android_id;
}


#ifdef TTT
JNIEXPORT jlong JNICALL
initAuthorization(JNIEnv *env, jobject thiz, jstring token) {
  // TODO: implement initAuthorization()

  LOGI("SDK initializing authorization ...");
  jstring android_str = getAndroidId(env, thiz);
  jstring packname_str = getPackageName(env,thiz);

  const char *token_path_chars = env->GetStringUTFChars(token, NULL);
  const char *android_chars = env->GetStringUTFChars(android_str, NULL);
  const char *packname_chars = env->GetStringUTFChars(packname_str, NULL);

  LOGI("Andorid id is %s",android_chars);
  LOGI("Package name is %s",packname_chars);

  //1.Get license from authorization server
  char req_data[1024] = {'\0'};
  char license_data[1024] = {'\0'};
  char cur_date[12] = {'\0'};

//  struct AuthorVerifyWrapper * pAuthor = getInstance();
  AuthorVerify author_verify;

//  curDate(pAuthor,cur_date,20);
//  releaseInstance(&pAuthor);

  snprintf(req_data, REQUEST_DATA_LENGTH,HTTP_REQUEST_TEMPLATE, token_path_chars, android_chars, cur_date);
  //    LOGI("request data: %s",req_data);

  int ret = request_license(req_data,license_data, LICENSE_DATA_LENGTH);
  if (ret == RETCODE_OK) {
//    ret = verify_license(license_data, android_chars);
//    if ( ret == RETCODE_OK) {
//      //            LOGI("Authorization server response license is: %s",license_data);
      char license_path[256]={'\0'};
      snprintf(license_path,sizeof(license_path),"/data/data/%s/.license",packname_chars);
      // wirte license to .license file
      FILE *ftmp = fopen(license_path,"w");
      if (!ftmp) {
        LOGE("No permission to access workspace %s directory: errno %d, msg:%s",license_path, errno,
             strerror(errno));
        ret = RETCODE_ERRR_INIT_AUTHOR_PERM;
      } else {
        fputs(license_data, ftmp );
      }
      fclose(ftmp);
//
//    } else {
//      LOGE("SDK verify authorization license failed(%d) ...",ret);
//      //            flag = ret;
//    }

  } else {
    LOGE("SDK request authentication license failed(error:%d)!!!",ret);
    //        flag = ret;
  }

  env->ReleaseStringUTFChars(token, token_path_chars);
  env->ReleaseStringUTFChars(android_str, android_chars);
  env->ReleaseStringUTFChars(packname_str, packname_chars);

  return (jlong)ret;
}

JNIEXPORT jboolean JNICALL
 verifyLicense(JNIEnv *env, jobject thiz){

  jstring packname_str = getPackageName(env,thiz);
  jstring android_str = getAndroidId(env, thiz);
  const char *packname_chars = env->GetStringUTFChars(packname_str, NULL);
  const char *android_chars = env->GetStringUTFChars(android_str, NULL);

  char license_path[256]={'\0'};
  char license_data[1024] = {'\0'};
  snprintf(license_path,sizeof(license_path),"/data/data/%s/.license",packname_chars);
  env->ReleaseStringUTFChars(packname_str, packname_chars);
  env->ReleaseStringUTFChars(android_str, android_chars);

  FILE *fp = fopen(license_path,"r");
  if (!fp) {
    //        LOGI("No license file ...");
    fclose(fp);
    return  false;
  }
  fclose(fp);

  AuthorVerify author;
  return author.valid_check(license_data,android_chars) != -1;

}

JNIEXPORT jint JNICALL
isActivated(JNIEnv *env, jobject thiz) {
  // TODO: implement getSystemInfo()
  UNUSED(thiz);

  jstring packname_str = getPackageName(env,thiz);
  jstring android_str = getAndroidId(env, thiz);
  const char *packname_chars = env->GetStringUTFChars(packname_str, NULL);
  const char *android_chars = env->GetStringUTFChars(android_str, NULL);

  char license_path[256]={'\0'};
  snprintf(license_path,sizeof(license_path),"/data/data/%s/.license",packname_chars);
  env->ReleaseStringUTFChars(packname_str, packname_chars);
  env->ReleaseStringUTFChars(android_str, android_chars);

  FILE *fp = fopen(license_path,"r");
  if (!fp) {
    //        LOGI("No license file ...");
    return  NO_ACTIVATE;
  }

  fclose(fp);
  return ACTIVATE_OK;
}


int request_license(const char* req_data, char* ret_license, size_t max_size) {

  CURL *pCurl = createCurl(AUTHORIZATION_SERVER_URL, HTTP_POST_HEADER, 30);
  if (pCurl != NULL) {

    char resp_data[2048] ={'\0'};

    int ret = sendPostRequest(pCurl, req_data, strlen(req_data), (void*)resp_data);
    if (ret == CURLE_OK) {

      cJSON* pJson = cJSON_Parse(resp_data);
      if (pJson != NULL) {
        cJSON *pRetCode = cJSON_GetObjectItem(pJson, "ret_code");
        cJSON *pRetMsg = cJSON_GetObjectItem(pJson, "ret_msg");
        cJSON *pLicense = cJSON_GetObjectItem(pJson, "license");
        if (pRetCode->valueint == RETCODE_OK) {
          memcpy(ret_license, pLicense->valuestring, max_size);
          cleanCurl(pCurl);
          return RETCODE_OK;
        } else {
          //                    LOGE("Parse license(request from server) get error: %s", pRetMsg->valuestring);
          cleanCurl(pCurl);
          return RETCODE_ERRR_INIT_AUTHOR_PARSE;
        }
      } else {
        //                LOGE("Server response data is invalid json!!!");
        cleanCurl(pCurl);
        return RETCODE_ERRR_INIT_AUTHOR_JSON;
      }

    } else {
      //            LOGE("Request license send data  failed !!!");
      return RETCODE_ERRR_INIT_AUTHOR_SEND;

    }
  } else {
    //        LOGE("Request license createCurl failed !!!");
    return RETCODE_ERRR_INIT_AUTHOR_NET;
  }
}
#endif

JNIEXPORT jint JNICALL
authorVerify(JNIEnv *env, jobject thiz) {

  jstring packname_str = getPackageName(env,thiz);
  jstring android_str = getAndroidId(env, thiz);

  const char *packname_chars = env->GetStringUTFChars(packname_str, NULL);
  const char *android_chars = env->GetStringUTFChars(android_str, NULL);

  if (g_pAuthorVerify == NULL) {

    char license_path[256]={'\0'};
    snprintf(license_path, sizeof(license_path), "/data/data/%s/.license", packname_chars);

//    LOGI("SDK initializing authorization ...");
//    LOGI("License path %s ...",license_path);
//    LOGI("Device_id %s ...",android_chars);

    g_pAuthorVerify = new AuthorVerify(license_path, android_chars);

    if (g_pAuthorVerify == NULL)
      return RETCODE_ERR_NEW;
  }

  env->ReleaseStringUTFChars(android_str, android_chars);
  env->ReleaseStringUTFChars(packname_str, packname_chars);

  if (!g_pAuthorVerify->isActivated()) {
      return RETCODE_ERR_NO_LICENSE;
  } else {
      if (g_pAuthorVerify->validCheck())
        return RETCODE_OK;
      return g_pAuthorVerify->getErrCode();
  }

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

std::string get_obfuse_key(JNIEnv *env, jobject thiz) {
  std::string obf_key;
  jstring packname_str = getPackageName(env, thiz);

  const char *packname_chars = env->GetStringUTFChars(packname_str, NULL);

  char license_path[256] = {'\0'};
  snprintf(license_path, sizeof(license_path), "/data/data/%s/.license", packname_chars);

  std::string license_data;
  FILE *ftmp = fopen(license_path,"r");
  if (ftmp != NULL){
    char license_str[LICENSE_DATA_LENGTH] = {'\0'};
    fgets(license_str, LICENSE_DATA_LENGTH, ftmp);
    license_data = std::string(license_str);
    fclose(ftmp);
  } else{
    LOGE("LICENSE FILE NOT EXIST!!!");
    return obf_key;
  }
  
  std::string rsa_data = OpensslTools::Base64Dencode(license_data.c_str());
  if (rsa_data.size() <= 0) {
    LOGE("uthorRetCode::RETCODE_ERRR_BASE64_DEC");
    return obf_key;
  }

  std::string license_str = OpensslTools::RsaPubDecrypt(rsa_data);
  
  //LOGE("Decrypt data: %s\n",license_str.c_str());

  std::vector<std::string> license_keys = split(license_str, "|||");

  if (license_keys.size() >= AUTHOR_FILED_NUM) {
    obf_key = license_keys[3];
  } else {
    LOGE("LICENSE FILE DATA INVALID!!!");
  }

  return obf_key;
}

void deobfuse_model(JNIEnv *env, jobject thiz, std::string model_dir) {
  LOGE("deobfuse_model model_dir:%s", model_dir.c_str());

  std::string obf_key = get_obfuse_key(env, thiz);
  if (obf_key.empty()) {
    LOGE("The obfuse key is null");
    return ;
  }
  LOGE("offline deobfuse_model obf_key:%s", obf_key.c_str());

  std::vector<std::string> obfs;
  if (!model_dir.empty()) {

    obfs = sherpa_onnx::collect_by_extern(model_dir, ".obfus");
    for (auto itr=obfs.begin(); itr!=obfs.end(); ++itr) {
      //LOGE("deobfuse_model file: %s\n", itr->data());
      auto bin_url = sherpa_onnx::remove_extension(*itr);
      //LOGE("deobfuse_model file 2: %s\n", bin_url.data());
      bool bin_exist = file_exists(bin_url);
      if (!bin_exist) {
        sherpa_onnx::obf_defuse(obf_key, *itr, bin_url);
      }
    }
  }
}

void delete_deobfuse_model(JNIEnv *env, jobject thiz, std::string model_dir) {

  std::vector<std::string> obfs;
  if (!model_dir.empty()) {
    obfs = sherpa_onnx::collect_by_extern(model_dir, ".obfus");
    for (auto itr=obfs.begin(); itr!=obfs.end(); ++itr) {
      //LOGE("deobfuse_model file: %s\n", itr->data());
      //std::filesystem::remove(*itr);
      /*
      auto bin_url = sherpa_onnx::remove_extension(*itr);
      std::filesystem::remove(bin_url);
      */
    }
  }
}
