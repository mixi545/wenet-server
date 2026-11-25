// openssl_tools.h
#ifndef OPENSSL_TOOLS_H
#define OPENSSL_TOOLS_H

#include <fstream>
#include <string.h>
#include <iostream>
#include <vector>

#include "openssl/rsa.h"
#include "openssl/pem.h"
#include <openssl/err.h>

#define KEY_LENGTH  2048             // 密钥长度
#define PUB_KEY_FILE "pubkey.pem"    // 公钥路径
#define PRI_KEY_FILE "prikey.pem"    // 私钥路径


std::string rsa_pub_decrypt(unsigned char *cipher_text);

//unsigned char *base64_encode(unsigned char *str);
unsigned char *base64_decode(unsigned char *code);

typedef unsigned char uchar;
static const std::string base64_char = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";//=
class OpensslTools {
public:
    /*
    制造密钥对：私钥和公钥
    **/
    // static void GenerateRSAKey(std::string & out_pub_key, std::string & out_pri_key);


    /*
        @brief : 私钥加密
        @para  : clear_text  -[i] 需要进行加密的明文
                 pri_key     -[i] 私钥
        @return: 加密后的数据
    **/
    static std::string RsaPriEncrypt(const std::string &clear_text);


    /*
        @brief : 公钥解密
        @para  : cipher_text -[i] 加密的密文
                 pub_key     -[i] 公钥
        @return: 解密后的数据
    **/
    static std::string RsaPubDecrypt(const std::string & cipher_text);


    /*
        @brief : 公钥加密
        @para  : clear_text  -[i] 需要进行加密的明文
                 pri_key     -[i] 私钥
        @return: 加密后的数据
    **/
    static std::string RsaPubEncrypt(const std::string &clear_text);


    /*
        @brief : 私钥解密
        @para  : cipher_text -[i] 加密的密文
         pub_key     -[i] 公钥
        @return: 解密后的数据
    **/
    static std::string RsaPriDecrypt(const std::string &cipher_text);


    /*
       @brief : base64 加密函数
       @para  : in       -[i] 待编码字符串
       @return: 编码后base64字符串
   **/
    static std::string Base64Encode(const std::string &in);

    /*
       @brief : base64 解密函数
       @para  : in       -[i] 待解码base64字符串
       @return: 解码后字符串（注意解密后字符串可能不可见）
   **/
    static std::string Base64Dencode(const std::string &in);
};


#endif //OPENSSL_TOOLS_H
