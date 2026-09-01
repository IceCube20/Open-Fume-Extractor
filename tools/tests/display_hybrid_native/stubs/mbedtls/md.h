#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#define MBEDTLS_MD_SHA256 1
inline int mbedtls_md_info_from_type(int t){ return t; }
inline int mbedtls_md_hmac(int,const unsigned char* key,size_t keyLen,const unsigned char* data,size_t size,unsigned char* digest) {
  BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0) return -1;
  auto result=BCryptCreateHash(algorithm,&hash,nullptr,0,const_cast<PUCHAR>(key),(ULONG)keyLen,0);
  if(result>=0) result=BCryptHashData(hash,const_cast<PUCHAR>(data),(ULONG)size,0);
  if(result>=0) result=BCryptFinishHash(hash,digest,32,0);
  if(hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm,0); return result<0 ? -1 : 0;
}
