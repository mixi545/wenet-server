//#include "http_post.h"
#include "openssl_tools.h"
#include "author_verify.h"
#include "cJSON.h"

#include <android/log.h>

#include <fstream>
#include <iostream>
#include <vector>


#define AUTHOR_FILED_NUM 4
#define LICENSE_DATA_LENGTH 1024

#ifdef JNI_AUTHORIZATION_SSL
#include "json.hpp"
using json = nlohmann::json;

#define AUTHOR_SERVER_URL "http://117.161.174.230:10088/test"
#define AUTHOR_HTTP_HEADER "Content-Type:application/json;charset=UTF-8"
#endif

#define TAG "AuthorVerify"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,     TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,     TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,     TAG, __VA_ARGS__)

AuthorVerify::AuthorVerify() {}
AuthorVerify::AuthorVerify(std::string license_file,
                           std::string device_id):_license_file(license_file),
                                                    _device_id(device_id),
                                                    _ret_code(AuthorRetCode::RETCODE_OK)
{
#ifdef JNI_AUTHORIZATION_SSL
  _pclient = new httplib::SSLClient(AUTHORIZATION_SERVER_HOST, AUTHORIZATION_SERVER_PORT);
#endif
  FILE *ftmp = fopen(_license_file.c_str(),"r");
  if (ftmp != NULL){
    char license_str[LICENSE_DATA_LENGTH] = {'\0'};
    fgets(license_str, LICENSE_DATA_LENGTH, ftmp);
    _license_data = std::string(license_str);
    fclose(ftmp);
  } else
    _ret_code = AuthorRetCode::RETCODE_ERR_NO_LICENSE;
}

AuthorVerify::~AuthorVerify() {
#ifdef JNI_AUTHORIZATION_SSL
  if (_pclient)
    delete _pclient;
#endif

}

bool AuthorVerify::validCheck() {
    if (_license_data.length()<0){
      _ret_code = AuthorRetCode::RETCODE_ERR_LICENSE_EMPTY;
      return false;
    }

    std::string rsa_data = OpensslTools::Base64Dencode(_license_data.c_str());
    if (rsa_data.size() <= 0) {
//        LOGI("Decode base64 data failed\n");
        _ret_code = AuthorRetCode::RETCODE_ERRR_BASE64_DEC;
        return false;
    }

    std::string license_str =  OpensslTools::RsaPubDecrypt(rsa_data);

    //LOGE("Decrypt data: %s\n",license_str.c_str());

    std::vector<std::string> license_keys = split(license_str, "|||");
    if (license_keys.size() != AUTHOR_FILED_NUM) {  //licese_key[0]:mac_id license_key[1]: init_date license_key[2]:characters
//        LOGI("SDK license data has invalid filed number (%lu != 3)!!!", license_keys.size());
        _ret_code = AuthorRetCode::RETCODE_ERR_FILED_NUM;
        return false;
    } else {
        if (license_keys[0] != "NO_CHECK_DEVICE_ID" ){
            if (_device_id != license_keys[0]) {
//                LOGE("The current device id not matched in license(%s != %s)!!!", device_id.c_str(),license_keys[0].c_str());
                _ret_code = AuthorRetCode::RETCODE_ERR_DEVICE;
                return false;
            }

        } else {
            LOGI("NO_CHECK_DEVICE_ID ....");
        }

        std::string now_date = current_date();

        //2.Compare the current date with license date
        int remain_days = days(current_date().c_str(), license_keys[1].c_str());

//        LOGI("%d days have passed since the last activation date !!!", remain_days);
        if (remain_days < AUTHOR_VERIFY_INTERVAL) {
          _ret_code = AuthorRetCode::RETCODE_OK;
          return true;
        } else {
          _ret_code = AuthorRetCode::RETCODE_ERR_EXPIRED;
          return false;
        }
    }
}

std::string AuthorVerify::encrypt_data(const char*input){

    std::string encrypt_data = OpensslTools::RsaPriEncrypt(input);
    std::string base64_data = OpensslTools::Base64Encode(encrypt_data);
    return base64_data;
}

#ifdef JNI_AUTHORORIZATION
bool AuthorVerify::initAuthorized(std::string token){

  LOGI("AuthorVerify::initAuthorized ...");

  char req_data[1024] = {'\0'};
  snprintf(req_data, REQUEST_DATA_LENGTH, HTTP_REQUEST_TEMPLATE,
           token.c_str(), _device_id.c_str(), current_date().c_str());

  CURL *pCurl = createCurl(AUTHORIZATION_SERVER_URL, HTTP_POST_HEADER, 30);
  if (pCurl != NULL) {

    char resp_data[2048] ={'\0'};

    int ret = sendPostRequest(pCurl, req_data, strlen(req_data), (void*)resp_data);
    if (ret == AuthorRetCode::RETCODE_OK) {

      cJSON* pJson = cJSON_Parse(resp_data);
      if (pJson != NULL) {
        cJSON *pRetCode = cJSON_GetObjectItem(pJson, "ret_code");
        cJSON *pRetMsg = cJSON_GetObjectItem(pJson, "ret_msg");
        cJSON *pLicense = cJSON_GetObjectItem(pJson, "license");
        if (pRetCode->valueint == AuthorRetCode::RETCODE_OK) {
          _license_data = std::string(pLicense->valuestring);
          wirteLicense(_license_data);
          cleanCurl(pCurl);
          _ret_code = AuthorRetCode::RETCODE_OK;
          return true;
        } else {
          LOGE("Parse license(request from server) get error: %s", pRetMsg->valuestring);
          _ret_code = AuthorRetCode::RETCODE_ERR_SERVER_RETCODE;
          cleanCurl(pCurl);
          return false;
        }
      } else {
        LOGE("Server response data is invalid json!!!");
        cleanCurl(pCurl);
        _ret_code = AuthorRetCode::RETCODE_ERRR_JSON;
        return false;
      }

    } else {
      LOGE("Request license send data  failed !!!");
      _ret_code = AuthorRetCode::RETCODE_ERRR_SEND;
      return false;
    }
  } else {
    LOGE("Request license createCurl failed !!!");
    _ret_code = RETCODE_ERRR_NET;
    return false;
  }
}
#endif

#ifdef JNI_AUTHORIZATION_SSL
bool AuthorVerify::initAuthorizedSSL(std::string token){

  LOGI("AuthorVerify::initAuthorizedSSL ...");

  httplib::Headers headers = {
      { "Authorization", token.c_str() }
  };

  char body_data[1024] = {'\0'};
  snprintf(body_data,
           HTTPS_REQUEST_BODY_LENGTH,
           HTTPS_REQUEST_BODY_TEMPLATE,
           _device_id.c_str(),
           current_date().c_str());

  if (auto res = _pclient->Post("/authorize",
                               headers,
                               body_data,
                               strlen(body_data),
                               "application/json")) {
    if (res->status != 200) {
      LOGI("AuthorVerify::initAuthorizedSSL(%d) %s",res->status,httplib::detail::status_message(res->status));
      return false;
    }

    LOGI("AuthorVerify::initAuthorizedSSL status=%d",res->status);

    if (res->get_header_value("Content-Type").length() >0 &&
        "application/json" != res->get_header_value("Content-Type")) {
      LOGE("AuthorVerify::initAuthorizedSSL response Content-Type(%s) is not json !!!", res->get_header_value("Content-Type").c_str());
      _ret_code = RETCODE_ERRR_JSON;
      return false;
    }

    json res_json =json::parse(res->body);
    if (!res_json.contains("license")){
      LOGE("AuthorVerify::initAuthorizedSSL response data no license filed!!");
      _ret_code = RETCODE_ERRR_JSON;
      return false;
    }

    _license_data = res_json["license"];
    wirteLicense(_license_data);
    _ret_code = AuthorRetCode::RETCODE_OK;
    _is_activated = true;
    return true;
  } else {
    LOGE("AuthorVerify::initAuthorizedSSL post error(%d)",res.error());
    _ret_code = RETCODE_ERRR_JSON;
    return false;
  }
  return false;
}
#endif

bool AuthorVerify::isActivated(){
  FILE *ftmp = fopen(_license_file.c_str(),"r");
  if (!ftmp)
    return false;
  fclose(ftmp);
  return true;
}

int AuthorVerify::wirteLicense(std::string data) {

  FILE *fp = fopen(_license_file.c_str(),"w");
  if (fp == NULL){
    LOGE("SDK has not been activated...");
    _ret_code = AuthorRetCode::RETCODE_ERR_NO_WRT_PERMISSION;
    return _ret_code;
  }

  fputs(_license_data.c_str(), fp);
  fclose(fp);
  _ret_code = AuthorRetCode::RETCODE_OK;
  return _ret_code;
}

int AuthorVerify::getErrCode(){
  return _ret_code;
}


