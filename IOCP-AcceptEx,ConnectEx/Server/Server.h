#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "mswsock.lib")
#endif
#include<iostream>
#include<thread>
#include<vector>
#include<mutex>
#include<string>
#include<chrono>
#include<atomic>


#define SERVER_PORT     9000
#define MAX_BUFFER      256
#define MAX_CLIENT      50
#define ENDPOINT        "127.0.0.1"


using namespace std::chrono_literals;

enum IOKEY {
    IOKEY_ACCEPT = 1,
    IOKEY_CONNECT = 2,
    IOKEY_SEND = 3,
    IOKEY_RECV = 4,
};


class OVER_EX {
private:
public:
    WSAOVERLAPPED   over_;
    WSABUF          wsabuf_;
    char            buffer_[MAX_BUFFER];
    SOCKET          socket_;
    IOKEY           iokey_;
};

class SocketInfo {
private:
public:
    OVER_EX			over_ex_;
    SOCKET			socket_;
    char			packet_buffer_[MAX_BUFFER];
    int				prev_size_;
    std::mutex		lock_;
};


class PlayerInfo :public SocketInfo {
public:
    SOCKET          socket_;
    bool            isUsed_;
    PlayerInfo() {
        ZeroMemory(&over_ex_.over_, sizeof(over_ex_.over_));
        over_ex_.wsabuf_.buf = over_ex_.buffer_;
        over_ex_.wsabuf_.len = sizeof(over_ex_.buffer_);
        over_ex_.iokey_ = {};
    }
};

class Iocp {
private:
public:
    HANDLE              ioHandle_;
    SOCKET              listenSocket_;
    std::mutex          lock_;
    std::atomic<int>    userId_;
};


/* Global*/
Iocp            g_iocp;
PlayerInfo      g_clients[MAX_CLIENT];
void            WorkThread();
void            MySend(int);
void            MyRecv(int);