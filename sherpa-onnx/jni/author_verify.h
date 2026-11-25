//
// Created by sensen on 11/7/2024.
//

#ifndef AUTHOR_VERIFY_H
#define AUTHOR_VERIFY_H

#include <iostream>
#include <vector>

#ifdef JNI_AUTHORIZATION_SSL

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#endif


#define AUTHOR_VERIFY_INTERVAL 30

#define AUTHORIZATION_SERVER_HOST "117.161.174.230"
#define AUTHORIZATION_SERVER_PORT 9099

#define HTTPS_REQUEST_BODY_TEMPLATE "{\"device_id\":\"%s\",\"cur_date\":\"%s\"}"
#define HTTPS_REQUEST_BODY_LENGTH   1024

class AuthorVerify {
public:
    enum AuthorRetCode{
     RETCODE_OK = 0,
     RETCODE_ERRR_SEND = -1,
     RETCODE_ERR_PARSE = -2,
     RETCODE_ERRR_NET = -3,
     RETCODE_ERRR_JSON = -4,
     RETCODE_ERRR_BASE64_DEC = -5,
     RETCODE_ERR_FILED_NUM = -6,
     RETCODE_ERR_DEVICE = -7,
     RETCODE_ERR_EXPIRED = -8,
     RETCODE_ERR_NO_WRT_PERMISSION = -9,
     RETCODE_ERR_NO_LICENSE = -10,
     RETCODE_ERR_SERVER_RETCODE = -11,
     RETCODE_ERR_LICENSE_EMPTY = -12
    };

    AuthorVerify();
    AuthorVerify(std::string license_file, std::string device_id);

#ifdef JNI_AUTHORORIZATION
    bool initAuthorized(std::string token);
#endif

#ifdef JNI_AUTHORIZATION_SSL
    bool initAuthorizedSSL(std::string token);
#endif

    bool isActivated();
    bool validCheck();
    int getErrCode();
    ~AuthorVerify();

private:

#ifdef JNI_AUTHORIZATION_SSL
 httplib::SSLClient *_pclient = nullptr;
#endif

 int _ret_code = 0;
 std::string _req_token;
 std::string _device_id;

 std::string _license_file;
 std::string _license_data;

 std::string encrypt_data(const char*input);

 inline std::string current_date() {
   char time_buffer[30] = {0x00};
   struct tm *t;
   time_t tt;
   time(&tt);
   t = localtime(&tt);
   snprintf(time_buffer, sizeof(time_buffer),"%d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

   return std::string(time_buffer);
 }

 inline time_t convert(int year, int month, int day) {
   tm info = {0};
   info.tm_year = year - 1900;
   info.tm_mon = month - 1;
   info.tm_mday = day;
   return mktime(&info);
 }

 // split string by delim into vector<string>
 inline std::vector<std::string> split( std::string const &str, const std::string  delim){
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
 inline int days(const char* from, const char* to)
 {
   int year, month, day;
   sscanf(from, "%d-%d-%d", &year, &month, &day);
   int fromSecond = (int) convert(year, month, day);

   sscanf(to, "%d-%d-%d", &year, &month, &day);
   int toSecond = (int) convert(year, month, day);

   return (toSecond - fromSecond) / 24 / 3600;
 }

 int wirteLicense(std::string data);
};
                   
#endif //AUTHOR_VERIFY_H