// openssl_tools.cpp
#include "sherpa-onnx/csrc/openssl_tools.h"
#include <stdlib.h>
#include <string.h>

static const char * prvkey_ = {
        "-----BEGIN PRIVATE KEY-----\n"
        "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCR5+j3hxUc1KdO\n"
        "zFWLQxD8LKp9PEIZwDazr6at4+XgmXLR1kmrfUAvhKWWgzxyOpbBK0YQxkRB66xG\n"
        "L1gZMzHpbMOh2OP1sn1i8nX1226nTyyhdVkchNeRSmw2J29S2nayMWsehmuC1izh\n"
        "1AqLy2XfrAoP0W5Y9A07jJDMs68P6iBRh3XPP/6R/2RcpRB5bsRrI2jYgrHZGB5L\n"
        "k265ZDULgHP1D1JZNbaSm5o0/M0MTngSgijV1qoN0JhEciTfH8UF+wjA7xA5GfYB\n"
        "AFxu4VnByNnfL4NhKwjtxJjC6Hy+3HiT/45qAdsdxloYUGTrXe6JM6RApotgVAjr\n"
        "I5laSOrTAgMBAAECggEAHPAH7n/mGg+7Fw/Dr5kfl0QslCYNXhoXP1AJGNfoUcFd\n"
        "WmzLs61lu4btaIqixG0GVU2F2fHYu9JIGqttJ6JVtdwSeNkRbTt3TtaTuq1Ipvo2\n"
        "+dbSU6utLW27RIpRs/cML49JlYZHcG2V/TwW018F13TBkh2RaycMMNFSLqc8g0U5\n"
        "dhHArnO4RvrnGMurTPxHyykw3CFb2cLJpdlYO3lIF0MsQLDDMuzCpwV45zGpisKp\n"
        "4/L2j2ZyFRVeuYoqECTu/SppGYK5UxL8S3DfomAoZIJD4YFpdvaWLyexCJvdwgBi\n"
        "pkC3gJp1+rTB0Nx/4/SKr1VEwzUhv+HzbWUPK4dbgQKBgQDBkyQUCtki95xIxx0K\n"
        "vf+psbRTI6Rih+QKIqqH/ERZNsL2ZVg7uEs72kYk2AVVwiWx+Qa7bZlF0Mg6X949\n"
        "F0I72Wek+eqOIiCAjuQybCPjkS3bDcgSAlw08KIveNEO6B1mWnILD7VOntdwZukb\n"
        "ySCM0X8/kekkcuJHJCILDlCIoQKBgQDA9WSgq4nSh4kABD5SDvNS0+KT7XfvEFB9\n"
        "ttquAAT2pTXMlO8kvJGpZv1Xzpj+HZq33hBuqRiwjCPTaq98l7XY3Aicdk9BVeAQ\n"
        "cIz/SV8lMIOCyADt4HsjwpAcUGprAV1AVrbgzQFhQuMqBoU/RvGArl63eO8sS1F4\n"
        "XbSJNCr68wKBgEwyJCBziLc36l/HaMoisfFVDtsvC3TxcrT+oku25ByN7r5YdBvF\n"
        "1m9v4u57zdXM/1OH57t9T7jojvhTZkJ71kmCsUaCTW63Rj30gSTL5nCcZAV27ksQ\n"
        "fMUl92FiWEBa47+3WG68Ny0EX19SUu3qBJMJn+IhCDmdsy43reLWNqMhAoGBAI8r\n"
        "xQ1eGjYmsKVdgKw8QZjcdIrUMSAl18NfkKR4iTZE4UU54LM+Y0nxi9rCBUzPxIPW\n"
        "9hRVzWb5pH9y8mCc5KXSpWUKd8X/pPAtwsCMvVcHxxybr+zQmi2vaGRJ2pwo9/sM\n"
        "HEdydSqoiYseKXII/po/RzIVPkl8XSfVR2ZiWd23AoGANv8SJ04rPExOc/Lfu3nl\n"
        "tyBtu3zHLg27pdkyUxPHcrzck6BVBwfDHYwXvNKWMnJbOwXOCsa+sS/PUU35ezhx\n"
        "EK8ZbAhzbXNIqBH2PuUAgGp0FePAJfhUSFx6zyUsaysYSb8qNyW4WmmDD+dlwwNV\n"
        "joqDR7fJZxmBAZ+TSz0joaM=\n"
        "-----END PRIVATE KEY-----"
};

static const char* pubkey_ = {
        "-----BEGIN PUBLIC KEY-----\n"
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAkefo94cVHNSnTsxVi0MQ\n"
        "/CyqfTxCGcA2s6+mrePl4Jly0dZJq31AL4SlloM8cjqWwStGEMZEQeusRi9YGTMx\n"
        "6WzDodjj9bJ9YvJ19dtup08soXVZHITXkUpsNidvUtp2sjFrHoZrgtYs4dQKi8tl\n"
        "36wKD9FuWPQNO4yQzLOvD+ogUYd1zz/+kf9kXKUQeW7EayNo2IKx2RgeS5NuuWQ1\n"
        "C4Bz9Q9SWTW2kpuaNPzNDE54EoIo1daqDdCYRHIk3x/FBfsIwO8QORn2AQBcbuFZ\n"
        "wcjZ3y+DYSsI7cSYwuh8vtx4k/+OagHbHcZaGFBk613uiTOkQKaLYFQI6yOZWkjq\n"
        "0wIDAQAB\n"
        "-----END PUBLIC KEY-----"
};

/*
@brief : 私钥加密
@para  : clear_text  -[i] 需要进行加密的明文
         pri_key     -[i] 私钥
@return: 加密后的数据
**/
std::string OpensslTools::RsaPriEncrypt(const std::string &clear_text)
{
    std::string encrypt_text;
    BIO *keybio = BIO_new_mem_buf((unsigned char *)prvkey_, -1);
    RSA* rsa = RSA_new();
    rsa = PEM_read_bio_RSAPrivateKey(keybio, &rsa, NULL, NULL);
    if (!rsa)
    {
        std::cout << "Call PEM_read_bio_RSAPrivateKey failed ..." << "\n";
        BIO_free_all(keybio);
        return std::string("");
    }

    // 获取RSA单次可以处理的数据的最大长度
    int len = RSA_size(rsa);

    unsigned  char *text = (unsigned char*)calloc(1,len+1);

    // 对数据进行私钥加密（返回值是加密后数据的长度）
    int ret = RSA_private_encrypt(clear_text.length(), (const unsigned char*)clear_text.c_str(), (unsigned char*)text, rsa, RSA_PKCS1_PADDING);

    if (ret < 0) {
        free(text) ;
        BIO_free_all(keybio);
        RSA_free(rsa);
        std::cout << "RSA_private_encrypt return size of signature is :" << ERR_get_error() << "\n";
        return NULL;
    }

    std::string raw_data = std::string((char*)text);

    free(text);
    BIO_free_all(keybio);
    RSA_free(rsa);

    return raw_data;
}

/*
@brief : 公钥解密
@para  : cipher_text -[i] 加密的密文
         pub_key     -[i] 公钥
@return: 解密后的数据
**/
std::string OpensslTools::RsaPubDecrypt(const std::string & cipher_text)
{
    std::string decrypt_text;
    BIO *keybio = BIO_new_mem_buf((unsigned char *)pubkey_, -1);
    RSA *rsa = RSA_new();

    // 注意--------使用第1种格式的公钥进行解密
    //rsa = PEM_read_bio_RSAPublicKey(keybio, &rsa, NULL, NULL);
    // 注意--------使用第2种格式的公钥进行解密（我们使用这种格式作为示例）
    rsa = PEM_read_bio_RSA_PUBKEY(keybio, &rsa, NULL, NULL);
    if (!rsa)
    {
        unsigned long err= ERR_get_error(); //获取错误号
        char err_msg[1024] = { 0 };
        ERR_error_string(err, err_msg); // 格式：error:errId:库:函数:原因
        std::cout << "RsaPubDecrypt::PEM_read_bio_RSA_PUBKEY failed msg:" << err_msg << "\n";
        BIO_free_all(keybio);
        return decrypt_text;
    }

    int len = RSA_size(rsa);
    char *text = new char[len + 1];
    memset(text, 0, len + 1);
    // 对密文进行解密
    int ret = RSA_public_decrypt(len, (const unsigned char*)cipher_text.c_str(), (unsigned char*)text, rsa, RSA_PKCS1_PADDING);
    if (ret >= 0) {
        decrypt_text.append(std::string(text, ret));
    }

    // 释放内存
    delete[] text;
    RSA_free(rsa);
    BIO_free_all(keybio);

    return decrypt_text;
}

/*
@brief : 公钥加密
@para  : clear_text  -[i] 需要进行加密的明文
         pri_key     -[i] 私钥
@return: 加密后的数据
**/
std::string OpensslTools::RsaPubEncrypt(const std::string &clear_text)
{
    std::string encrypt_text;
    BIO *keybio = BIO_new_mem_buf((unsigned char *)pubkey_, -1);
    RSA* rsa = RSA_new();
    // 注意-----第1种格式的公钥
    //rsa = PEM_read_bio_RSAPublicKey(keybio, &rsa, NULL, NULL);
    // 注意-----第2种格式的公钥（这里以第二种格式为例）
    rsa = PEM_read_bio_RSA_PUBKEY(keybio, &rsa, NULL, NULL);

    // 获取RSA单次可以处理的数据块的最大长度
    int key_len = RSA_size(rsa);
    int block_len = key_len - 11;    // 因为填充方式为RSA_PKCS1_PADDING, 所以要在key_len基础上减去11

    // 申请内存：存贮加密后的密文数据
    char *sub_text = new char[key_len + 1];
    memset(sub_text, 0, key_len + 1);
    int ret = 0;
    int pos = 0;
    std::string sub_str;
    // 对数据进行分段加密（返回值是加密后数据的长度）
    while (pos < clear_text.length()) {
        sub_str = clear_text.substr(pos, block_len);
        memset(sub_text, 0, key_len + 1);
        ret = RSA_public_encrypt(sub_str.length(), (const unsigned char*)sub_str.c_str(), (unsigned char*)sub_text, rsa, RSA_PKCS1_PADDING);
        if (ret >= 0) {
            encrypt_text.append(std::string(sub_text, ret));
        }
        pos += block_len;
    }

    // 释放内存
    BIO_free_all(keybio);
    RSA_free(rsa);
    delete[] sub_text;

    return encrypt_text;
}

/*
@brief : 私钥解密
@para  : cipher_text -[i] 加密的密文
         pub_key     -[i] 公钥
@return: 解密后的数据
**/
std::string OpensslTools::RsaPriDecrypt(const std::string &cipher_text)
{
    std::string decrypt_text;
    RSA *rsa = RSA_new();
    BIO *keybio;
    keybio = BIO_new_mem_buf((unsigned char *)prvkey_, -1);

    rsa = PEM_read_bio_RSAPrivateKey(keybio, &rsa, NULL, NULL);
    if (rsa == nullptr) {
        unsigned long err = ERR_get_error(); //获取错误号
        char err_msg[1024] = { 0 };
        ERR_error_string(err, err_msg); // 格式：error:errId:库:函数:原因
        printf("err msg: err:%ld, msg:%s\n", err, err_msg);
        return std::string();
    }

    // 获取RSA单次处理的最大长度
    int key_len = RSA_size(rsa);
    char *sub_text = new char[key_len + 1];
    memset(sub_text, 0, key_len + 1);
    int ret = 0;
    std::string sub_str;
    int pos = 0;
    // 对密文进行分段解密
    while (pos < cipher_text.length()) {
        sub_str = cipher_text.substr(pos, key_len);
        memset(sub_text, 0, key_len + 1);
        ret = RSA_private_decrypt(sub_str.length(), (const unsigned char*)sub_str.c_str(), (unsigned char*)sub_text, rsa, RSA_PKCS1_PADDING);
        if (ret >= 0) {
            decrypt_text.append(std::string(sub_text, ret));
            pos += key_len;
        }
    }
    // 释放内存
    delete[] sub_text;
    BIO_free_all(keybio);
    RSA_free(rsa);

    return decrypt_text;
}


std::string OpensslTools::Base64Encode(const std::string &in) {
    std::string out;

    int val=0, valb=-6;
    for (uchar c : in) {
        val = (val<<8) + c;
        valb += 8;
        while (valb>=0) {
            out.push_back(base64_char[(val>>valb)&0x3F]);
            valb-=6;
        }
    }
    if (valb>-6) out.push_back(base64_char[((val<<8)>>(valb+8))&0x3F]);
    while (out.size()%4) out.push_back('=');
    return out;
}


std::string OpensslTools::Base64Dencode(const std::string &in) {

    std::string out;

    std::vector<int> T(256,-1);
    for (int i=0; i<64; i++) T[base64_char[i]] = i;

    int val=0, valb=-8;
    for (uchar c : in) {
        if (T[c] == -1) break;
        val = (val<<6) + T[c];
        valb += 6;
        if (valb>=0) {
            out.push_back(char((val>>valb)&0xFF));
            valb-=8;
        }
    }
    return out;
}



 // ================================ C function ===================================================

unsigned char *base64_decode(unsigned char *code)
{
//根据base64表，以字符找到对应的十进制数据
    int table[]={0,0,0,0,0,0,0,0,0,0,0,0,
                 0,0,0,0,0,0,0,0,0,0,0,0,
                 0,0,0,0,0,0,0,0,0,0,0,0,
                 0,0,0,0,0,0,0,62,0,0,0,
                 63,52,53,54,55,56,57,58,
                 59,60,61,0,0,0,0,0,0,0,0,
                 1,2,3,4,5,6,7,8,9,10,11,12,
                 13,14,15,16,17,18,19,20,21,
                 22,23,24,25,0,0,0,0,0,0,26,
                 27,28,29,30,31,32,33,34,35,
                 36,37,38,39,40,41,42,43,44,
                 45,46,47,48,49,50,51
    };

    long len;
    long str_len;
    unsigned char *res;
    int i,j;

    //计算解码后的字符串长度
    len = strlen((const char*)code);

    //判断编码后的字符串后是否有=
    if(strstr((const char*)code,"=="))
        str_len=len/4*3-2;
    else if(strstr((const char*)code,"="))
        str_len=len/4*3-1;
    else
        str_len=len/4*3;

    res=(unsigned char*)malloc(sizeof(unsigned char)*str_len+1);
    res[str_len]='\0';

//以4个字符为一位进行解码
    for(i=0,j=0;i < len-2;j+=3,i+=4)
    {
        res[j]=((unsigned char)table[code[i]])<<2 | (((unsigned char)table[code[i+1]])>>4); //取出第一个字符对应base64表的十进制数的前6位与第二个字符对应base64表的十进制数的后2位进行组合
        res[j+1]=(((unsigned char)table[code[i+1]])<<4) | (((unsigned char)table[code[i+2]])>>2); //取出第二个字符对应base64表的十进制数的后4位与第三个字符对应bas464表的十进制数的后4位进行组合
        res[j+2]=(((unsigned char)table[code[i+2]])<<6) | ((unsigned char)table[code[i+3]]); //取出第三个字符对应base64表的十进制数的后2位与第4个字符进行组合
    }

    return res;

}


/*
@brief : 公钥解密
@para  : cipher_text -[i] 加密的密文
         pub_key     -[i] 公钥
@return: 解密后的数据
**/
std::string rsa_pub_decrypt(unsigned char *cipher_text)
{

    BIO *keybio = BIO_new_mem_buf((unsigned char *)pubkey_, -1);
    RSA *rsa = RSA_new();

    // 注意--------使用第1种格式的公钥进行解密
    //rsa = PEM_read_bio_RSAPublicKey(keybio, &rsa, NULL, NULL);
    // 注意--------使用第2种格式的公钥进行解密（我们使用这种格式作为示例）
    rsa = PEM_read_bio_RSA_PUBKEY(keybio, &rsa, NULL, NULL);
    if (!rsa)
    {
        unsigned long err = ERR_get_error(); //获取错误号
        char err_msg[1024] = { 0 };
        ERR_error_string(err, err_msg); // 格式：error:errId:库:函数:原因
        std::cout << "err msg: err: " << err << ", msg:" << err_msg;
        BIO_free_all(keybio);
        return NULL;
    }

    int len = RSA_size(rsa);

    std::cout << "RSA_size: " << len << "\n";
    unsigned  char *text = (unsigned char*)calloc(1,len+1);
    // 对密文进行解密
    int ret = RSA_public_decrypt(len, (unsigned char*)cipher_text, text, rsa, RSA_PKCS1_PADDING);
    if (ret < 0) {
        free(text) ;
        BIO_free_all(keybio);
        RSA_free(rsa);
        std::cout << "RSA_public_decrypt retcode: " << ret << "\n";
        return NULL;
    }

    std::string raw_data = std::string((char*)text);

    free(text);
    BIO_free_all(keybio);
    RSA_free(rsa);

    return raw_data;
}