#include"../TestAcceptEx/Server.h"

LPFN_CONNECTEX g_connect;

void ConnectEx(SOCKET& socket, GUID guid) {
    DWORD dwbyte{ 0 };
    WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guid, sizeof(guid),
        &g_connect, sizeof(g_connect),
        &dwbyte, NULL, NULL);
}

int main() {
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
        std::cout << "Error_WSAStartup() \n";

    SOCKADDR_IN server_addr;
    ZeroMemory(&server_addr, sizeof(server_addr));
    server_addr.sin_family = PF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);


    SOCKET client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (bind(client_socket, reinterpret_cast<sockaddr*>(&server_addr),
        sizeof(server_addr)) == SOCKET_ERROR) {
        std::cout << "Error_Bind\n";
        std::cout << GetLastError() << "\n";
        while (1);
        closesocket(client_socket);
        WSACleanup();
    }

    //Create IOCP
    g_iocp.ioHandle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);

    OVER_EX over_ex;
    ZeroMemory(&over_ex.over_, sizeof(over_ex.over_));
    over_ex.iokey_ = IOKEY_CONNECT;
    over_ex.socket_ = client_socket;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(ENDPOINT);

    //Test
    std::this_thread::sleep_for(5s);


    ConnectEx(client_socket, WSAID_CONNECTEX);
    if (g_connect(client_socket, reinterpret_cast<SOCKADDR*>(&server_addr),
        sizeof(server_addr), NULL, NULL, NULL,
        reinterpret_cast<LPOVERLAPPED>(&over_ex.over_)) == FALSE) {

        auto error = GetLastError();
        if (error != WSA_IO_PENDING) {
            std::cout << "Error_Connect\n";
            closesocket(client_socket);
            WSACleanup();
        }
    }

    int id = g_iocp.userId_++;
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(client_socket), g_iocp.ioHandle_, id, 0);

    //Create Thread
    std::vector<std::thread>threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(std::thread(WorkThread));
    }

    for (auto& i : threads)
        i.join();
}

void WorkThread() {
    while (true) {
        DWORD		io_byte{};
        //DWORD	id{};	  //x86
        ULONGLONG	id{}; //x64

        OVER_EX* over_ex;
        int error = GetQueuedCompletionStatus(g_iocp.ioHandle_, &io_byte, &id,
            reinterpret_cast<LPOVERLAPPED*>(&over_ex), INFINITE);

        //connect
        if (over_ex->iokey_ == IOKEY_CONNECT) {
            //3초 정도 기다린다음 타임아웃 상태이면 연결이 실패
            auto waitTime=WaitForSingleObject(reinterpret_cast<HANDLE>(over_ex->socket_), 3000);
           
            //3초가 지나도 시그널 상태가 안됨 연결 종료
            if (waitTime == WAIT_TIMEOUT) {
                std::cout << "Exit\n";
            }

            else if (WAIT_FAILED) {
                int error = GetLastError();
                if (error !=0 && error!= ERROR_IO_PENDING) {
                    std::cout << "WAIT_FAILED";
                    std::cout << error << "\n";
                    while (1);
                }
            }
            std::cout << "CONNECT Server: ";

            //문제없으면 연결
            g_clients[id].socket_ = over_ex->socket_;
            MyRecv(id);

            //1초 동안 스레드를 쉬게한다.
            std::this_thread::sleep_for(1s);

            //다시커넥트
            SOCKADDR_IN server_addr;
            ZeroMemory(&server_addr, sizeof(server_addr));
            server_addr.sin_family = PF_INET;
            server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
            SOCKET client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

            if (bind(client_socket, reinterpret_cast<sockaddr*>(&server_addr),
                sizeof(server_addr)) == SOCKET_ERROR) {
                std::cout << "Error_Bind\n";
                std::cout << GetLastError() << "\n";
                while (1);
                closesocket(client_socket);
                WSACleanup();
            }

            server_addr.sin_port = htons(SERVER_PORT);
            server_addr.sin_addr.s_addr = inet_addr(ENDPOINT);

            //Overlapeed 재설정
            ZeroMemory(&over_ex->over_, sizeof(over_ex->over_));
            over_ex->iokey_ = IOKEY_CONNECT;
            over_ex->socket_ = client_socket;

            int id = g_iocp.userId_++;
            CreateIoCompletionPort(reinterpret_cast<HANDLE>(client_socket), g_iocp.ioHandle_, id, 0);

            if (g_connect(client_socket, reinterpret_cast<SOCKADDR*>(&server_addr),
                sizeof(server_addr), NULL, NULL, NULL,
                reinterpret_cast<LPOVERLAPPED>(&over_ex->over_)) == FALSE) {

                auto error = GetLastError();
                if (error != WSA_IO_PENDING) {
                    std::cout << "Error_Connect\n";
                    closesocket(client_socket);
                    WSACleanup();
                }
            }

        }
        else if (over_ex->iokey_ == IOKEY_RECV) {
            std::cout << g_clients[id].over_ex_.wsabuf_.buf << std::endl;
            MyRecv(id);
        }
        else if (over_ex->iokey_ == IOKEY_SEND) {
            delete over_ex;
        }
        else {
            std::cout << "work threads error \n";
            while (1);
        }
    }
}

void MySend(int id) {
    OVER_EX* over_ex = new OVER_EX;
    char s[MAX_BUFFER];
    ZeroMemory(&over_ex->over_, sizeof(over_ex->over_));
    std::string word = { "HELLO WORLD " };
    word += std::to_string(id);

    for (int i = 0; i < word.size() + 1; ++i) {
        s[i] = word[i];
    }

    over_ex->wsabuf_.len = MAX_BUFFER;
    over_ex->wsabuf_.buf = over_ex->buffer_;
    over_ex->iokey_ = IOKEY_SEND;
    memcpy(over_ex->buffer_, &s, MAX_BUFFER);
    WSASend(g_clients[id].socket_, &over_ex->wsabuf_,
        1, 0, 0, &over_ex->over_, NULL);
}

void MyRecv(int id) {
    DWORD flag{ 0 };
    g_clients[id].over_ex_.iokey_ = IOKEY_RECV;

    int error = WSARecv(g_clients[id].socket_, &g_clients[id].over_ex_.wsabuf_,
        1, NULL, &flag, &g_clients[id].over_ex_.over_, 0);

    if (error == SOCKET_ERROR) {
        if (GetLastError() != WSA_IO_PENDING) {
            std::cout << GetLastError() << "\n";
            while (1);
        }
    }
}
