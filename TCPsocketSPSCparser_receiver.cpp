// server.cpp - receiver
#include "SPSCqueue.h"
#include "FIXparser.h"

// Windows TCP/IP API
#include <winsock2.h>
#include <ws2tcpip.h> // Needed for converting "127.0.0.1" into a binary address.

#include <thread>
#include <iostream>

// Link against the Windows Sockets library - w./out his code compiler but linker fails
#pragma comment(lib, "Ws2_32.lib")
// Not C++: a Microsoft Visual C++ (MSVC) compiler extension - this would be a CMAKE line in a regular project :
// if (WIN32)
// target_link_libraries(my_target ws2_32)
// endif()

// Global queue
SQueue<FixMessage, 1024> recvQueue;

int main() {

	// Winsock init
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa); // requests version 2.2 - wouldnt this go in the CMAKELISTS ???
	
	// Create the TCP socket - will stay open for other connections until we close it
	SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Prepare server address
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(5001);
	// addr.sin_addr.s_addr = INADDR_ANY; // listen on all local interfaces.
	addr.sin_addr.s_addr = htonl((10 << 24) | (0 << 16) | (1 << 8) | 53); // listen to only 10.0.1.53 - Equivalent to inet_pton

	// Bind socket
	bind(listenSock, (sockaddr*)&addr, sizeof(addr));
	// Marks the socket as listening w/ 1 connection pending connection
	listen(listenSock, 1);

	std::cout << "Server listening...\n";

	// Returns a new socket (clientSock) for communication - Waits for one incoming connection
	SOCKET clientSock = accept(listenSock, nullptr, nullptr);
	std::cout << "Client connected\n";

	// TCP recv thread (producer)
	std::thread recvThread([&] {
		// continuously reads from TCPand enqueues messages
		while (true) {
			FixMessage msg{};
			// blocking call - Reads up to sizeof(msg.data) bytes from clientSock
			int bytes_read = recv(clientSock, msg.data, sizeof(msg.data), 0);
			if (bytes_read <= 0) break; // we got no data

			msg.len = bytes_read;
			while (!recvQueue.enqueue(msg)) {
				_mm_pause();
			}
		}
		});

	// FIX processing thread (consumer)
	std::thread processor([&] {
		FixMessage msg;
		while (true) {
			if (recvQueue.dequeue(msg)) {
				std::string fix(msg.data, msg.len);
				std::cout << "FIX RECEIVED: " << fix << "\n";
				// process FIX msg
				std::vector<output> get_out;
				std::string_view msg = fix;
				ParserOutcomes result = parseMsg(msg, get_out);

				if (result == ParserOutcomes::good) {
					std::cout << "Message parsed good" << std::endl;
				}
				else if (result == ParserOutcomes::unknown) {
					std::cout << "Message parse  unknown" << std::endl;
				}
				else if (result == ParserOutcomes::bad) {
					std::cout << "Message parse bad" << std::endl;
				}
			}
			else {
				_mm_pause();
			}
		}
		});

	recvThread.join();
	processor.join();

	closesocket(clientSock);
	closesocket(listenSock);
	WSACleanup();
	return 0;
}
