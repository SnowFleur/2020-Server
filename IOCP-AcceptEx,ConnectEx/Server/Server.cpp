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
        while (1);
        closesocket(g_iocp.listenSocket_);
        WSACleanup();
    }

    if (listen(g_iocp.listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cout << "Error_listen\n";
        closesocket(g_iocp.listenSocket_);
        WSACleanup();
    }

	// listne()에서 accept 받지 못하게한다.
	BOOL on = true;
	setsockopt(g_iocp.listenSocket_, SOL_SOCKET, SO_CONDITIONAL_ACCEPT,
		reinterpret_cast<char*>(&on), sizeof(on));

	//Create IOCP
	g_iocp.ioHandle_ =CreateIoCompletionPort(INVALID_HANDLE_VALUE,NULL, NULL,0);
	
	//Client Information
	SOCKADDR_IN client_addr;
	int addr_len = sizeof(client_addr);
	OVER_EX over_ex;
	SOCKET client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	ZeroMemory(&client_addr, addr_len);
	ZeroMemory(&over_ex.over_, sizeof(over_ex.over_));
	over_ex.iokey_ = IOKEY_ACCEPT;
	over_ex.socket_ = client_socket; 
	
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_iocp.listenSocket_), g_iocp.ioHandle_, NULL, NULL);
	if (AcceptEx(g_iocp.listenSocket_, client_socket, &over_ex.buffer_, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, NULL,
		reinterpret_cast<LPOVERLAPPED>(&over_ex.over_)) == SOCKET_ERROR) {
		std::cout << "Error\n";
	}
	//Create Thread
	std::vector<std::thread>threads;
	for (int i = 0; i < 8; ++i) {
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
		
		//Accept
		if (over_ex->iokey_ == IOKEY_ACCEPT) {
			PSOCKADDR pRemoteSocketAddr=nullptr;
			PSOCKADDR pLocalSocketAddr = nullptr;
			INT pRemoteSocketAddrLength=0;
			INT pLocalSocketAddrLength = 0;

			//정보 얻기
            GetAcceptExSockaddrs(
                &over_ex->buffer_, 0,
                sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
                &pLocalSocketAddr, &pLocalSocketAddrLength, &pRemoteSocketAddr, &pRemoteSocketAddrLength);

			SOCKADDR_IN remoteAddr = *(reinterpret_cast<PSOCKADDR_IN>(pRemoteSocketAddr));
			//접속한 클라이언트 IP와 포트 정보 얻기
			std::cout << "Accept New Clients IP: " <<
				inet_ntoa(remoteAddr.sin_addr) << " PORT: " <<
				ntohs(remoteAddr.sin_port) << "\n";

			//ID를 얻는다
			int id= g_iocp.userId_++;
			//MAX_CLIENT 보다 많으면 Accpet를 중단한다.
			if (id < MAX_CLIENT) {

			g_clients[id].socket_ = over_ex->socket_;

			// Client 소켓 IOCP에 등록
			CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_clients[id].socket_), g_iocp.ioHandle_, id, 0);
			//client에게 환영인사 보내기
			MySend(id);

			ZeroMemory(&over_ex->over_, sizeof(over_ex->over_));
			SOCKET socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			over_ex->iokey_ = IOKEY_ACCEPT;
			over_ex->socket_ = socket;
			
			//다시 Accpet overlapped 호출
			AcceptEx(g_iocp.listenSocket_, socket, &over_ex->buffer_, 0,
				sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, NULL,
				reinterpret_cast<LPOVERLAPPED>(&over_ex->over_));
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
	word+=std::to_string(id);
	
	for (int i = 0; i < word.size()+1; ++i) {
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

	int error=WSARecv(g_clients[id].socket_, &g_clients[id].over_ex_.wsabuf_,
		1, NULL, &flag, &g_clients[id].over_ex_.over_, 0);

	if (error == SOCKET_ERROR) {
		if (GetLastError() != WSA_IO_PENDING) {
			std::cout << GetLastError() << "\n";
			while (1);
		}
	}
}
