#pragma once
#define F_SETFL 1
#define O_NONBLOCK 2
inline int fcntl(int,int,int){ return 0; }
