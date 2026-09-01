#pragma once
#define AF_INET 2
#define SOCK_DGRAM 2
#define IPPROTO_UDP 17
#define INADDR_ANY 0
#define MSG_DONTWAIT 0
typedef int socklen_t;
struct in_addr { uint32_t s_addr; };
struct sockaddr { uint16_t sin_family; };
struct sockaddr_in { uint16_t sin_family,sin_port; in_addr sin_addr; };
inline uint16_t htons(uint16_t v){ return (v>>8)|(v<<8); }
inline int socket(int,int,int){ return -1; }
inline int close(int){ return 0; }
inline int bind(int,sockaddr*,size_t){ return 0; }
inline int sendto(int,const void*,size_t,int,const sockaddr*,size_t){ return -1; }
