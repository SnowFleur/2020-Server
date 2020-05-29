#include"Server.h"

int main() {
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
        std::cout << "Error_WSAStartup() \n";

    g_iocp.listenSocket_ = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (g_iocp.listenSocket_ == INVALID_SOCKET)
        std::cout << "Error_WSASocket\n";

    SOCKADDR_IN server_addr;
    ZeroMemory(&server_addr, sizeof(server_addr));
    server_addr.sin_family = PF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(g_iocp.listenSocket_, reinterpret_cast<sockaddr*>(&server_addr),
        sizeof(server_addr)) == SOCKET_ERROR) {
        std::cout << "Error_Bind\n";
        std::cout << GetLastError() << "\n";
        closesocket(g_iocp.listenSocket_);
        WSACleanup();
        while (1);
    }

    if (listen(g_iocp.listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cout << "Error_listen\n";
        closesocket(g_iocp.listenSocket_);
        WSACleanup();
    }

    // listne()에서 accept 받지 못하게한다.
    BOOL on = TRUE;
    setsockopt(g_iocp.listenSocket_, SOL_SOCKET, SO_CONDITIONAL_ACCEPT,
        reinterpret_cast<char*>(&on), sizeof(on));


    //Create IOCP
    g_iocp.ioHandle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);

    //Iocp에 리슨소켓 등록
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_iocp.listenSocket_), g_iocp.ioHandle_, NULL, NULL);

    for (int i = 0; i < MAX_SOCKET; ++i) {
        SOCKET socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
        OVER_EX* over_ex = new OVER_EX();

        over_ex->socket_ = socket;
        over_ex->iokey_ = IOKEY_ACCEPT;
        ZeroMemory(&over_ex->over_, sizeof(over_ex->over_));
        std::cout << "Init Scoket Value: " << socket << "\n";

        //Iocp에 소켓풀 소켓들 등록
        CreateIoCompletionPort(reinterpret_cast<HANDLE>(over_ex->socket_), g_iocp.ioHandle_, i, 0);

        if (AcceptEx(g_iocp.listenSocket_, over_ex->socket_, &over_ex->buffer_, 0,
            sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, NULL,
            reinterpret_cast<LPOVERLAPPED>(&over_ex->over_)) == SOCKET_ERROR) {
            std::cout << "Error\n";
        }
    }

    std::cout << "\n==============\n";

    //Create Thread
    std::vector<std::thread>threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back(std::thread(WorkThread));
    }

    for (auto& i : threads)
        i.join();
}

void WorkThread() {
    while (TRUE) {
        DWORD		io_byte{};
#ifdef _WIN64
        ULONGLONG	id{}; //x64
#else  _WIN32
        DWORD	    id{};	  //x86
#endif
        OVER_EX* over_ex;
        int error = GetQueuedCompletionStatus(g_iocp.ioHandle_, &io_byte, &id,
            reinterpret_cast<LPOVERLAPPED*>(&over_ex), INFINITE);

        if (error == false) {
            g_clients[id].lock_.lock();
            SOCKET disconnectSocket = g_clients[id].socket_;
            g_clients[id].lock_.unlock();

            //OVER_EX는 재활용해서 해보자
            over_ex->socket_ = disconnectSocket;
            over_ex->iokey_ = IOKEY_DISCONNECT;
            ZeroMemory(&over_ex->over_, sizeof(over_ex->over_));

            GUID guid = WSAID_DISCONNECTEX;
            LPFN_DISCONNECTEX lpfn_disconnect = (LPFN_DISCONNECTEX)DisconnectEx(disconnectSocket, guid);

            if (lpfn_disconnect == NULL) {
                std::cout << "lpfn disconnect NULL Error: ";
                std::cout << GetLastError() << "\n";
            }
            else {
                int error = lpfn_disconnect(disconnectSocket,
                    reinterpret_cast<LPOVERLAPPED>(&over_ex->over_), TF_REUSE_SOCKET, 0);
                if (error == FALSE) {
                    if (GetLastError() != WSA_IO_PENDING) {
                        std::cout << "Disconnect Error: ";
                        std::cout << GetLastError() << "\n";
                        while (1);
                    }
                }
            }
        }

        else {
            switch (over_ex->iokey_) {
            case IOKEY_ACCEPT: {
                //ID를 얻음과 동시에 userid를 증가한다(Atomic)
                int user_id = g_iocp.userId_++;

                // Client 소켓 IOCP에 등록 및 소켓 복사
                g_clients[user_id].socket_ = over_ex->socket_;
                g_clients[user_id].isUsed_ = TRUE;

                std::cout << "Accept Socket: " << g_clients[user_id].socket_ << "\n";
                //client에게 환영인사 및 비동기 Recv시작
                MySend(user_id);
                MyRecv(user_id);
                break;
            }
            case IOKEY_DISCONNECT: {
                int user_id = id;
                std::cout << "Disconnect:" << over_ex->socket_ << "\n";

                //userid를 감소한다(Atomic)
                g_iocp.userId_--;
                g_clients[user_id].isUsed_ = FALSE;

                //디스커넥트가 됐으니 다시 이 소켓으로 Accpet를 진행하자
                //OVER_EX는 재활용해서 해보자
                over_ex->iokey_ = IOKEY_ACCEPT;
                ZeroMemory(&over_ex->over_, sizeof(over_ex->over_));

                //다시 Accpet overlapped 호출
                int error = AcceptEx(g_iocp.listenSocket_, over_ex->socket_, &over_ex->buffer_, 0,
                    sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, NULL,
                    reinterpret_cast<LPOVERLAPPED>(&over_ex->over_));

                if (error == FALSE) {
                    if (GetLastError() != WSA_IO_PENDING) {
                        std::cout << "AcceptEx Error: ";
                        std::cout << GetLastError() << "\n";
                        while (1);
                    }
                }
                break;
            }
            case IOKEY_SEND: {
                delete over_ex;
                break;
            }
            case IOKEY_RECV: {
                int user_id = id;
                MyRecv(user_id);
                break;
            }
            default: {
                std::cout << "work threads error \n";
                while (1);
                break;
            }
            }
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
    DWORD flag{};
    ZeroMemory(&g_clients[id].over_ex_.over_, sizeof(g_clients[id].over_ex_.over_));

    int error = WSARecv(g_clients[id].socket_, &g_clients[id].over_ex_.wsabuf_,
        1, NULL, &flag, &g_clients[id].over_ex_.over_, 0);

    if (error == SOCKET_ERROR) {
        if (GetLastError() != WSA_IO_PENDING) {
            std::cout << "Recv: ";
            std::cout << GetLastError() << "\n";
            while (1);
        }
    }
}
void* DisconnectEx(SOCKET& socket, GUID guid) {
    DWORD dwbyte{ 0 };
    void* ex = nullptr;

    WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guid, sizeof(guid),
        &ex, sizeof(ex),
        &dwbyte, NULL, NULL);

    return ex;
}