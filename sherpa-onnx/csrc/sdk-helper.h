// sdk-helper.cpp
#pragma once
#include <string>
#include <vector>

bool valid_check(const char* license_file, const std::string device_id);
std::vector<std::string> get_macs();
std::string get_license_key();
int get_max_connection();
std::string get_jni_package_name();

