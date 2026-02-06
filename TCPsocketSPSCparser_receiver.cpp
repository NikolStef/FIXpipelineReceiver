// to do: resync-safe, BodyLength+CheckSum compliant receiver
#include "SPSCqueue.h"
#include "FIXparser.h"
#include <winsock2.h>
#include <ws2tcpip.h> // Needed for converting "127.0.0.1" into a binary address.
#include <thread>
#include <iostream>

// Link against the Windows Sockets library
#pragma comment(lib, "Ws2_32.lib")

SQueue<FixMessage, 1024> recvQueue;

int main() {

	// Winsock v2.2
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
	
	// Create the TCP socket - will stay open for other connections until we close it
	SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(5001);
	// addr.sin_addr.s_addr = INADDR_ANY; // listen on all local interfaces.
	addr.sin_addr.s_addr = htonl((10 << 24) | (0 << 16) | (1 << 8) | 53); // listen to only 10.0.1.53 - Equivalent to inet_pton

	bind(listenSock, (sockaddr*)&addr, sizeof(addr));
	listen(listenSock, 1); // 1 pending connection only
	std::cout << "Receiver listening...\n";

	// Wait for incoming connection(s)
	SOCKET clientSock = accept(listenSock, nullptr, nullptr);
	std::cout << "Client connected\n";

    // receiver
	std::string streamBuffer;
	std::thread recvThread([&] {
		// TCP is a byte stream
		std::string streamBuffer;
		streamBuffer.reserve(32768); // for demo purposes

		while (true) {
			char buf[16384]; // for demo purposes
			// Read TCP data into buf
			int bytes_read = recv(clientSock, buf, sizeof(buf), 0);
            // 0: connection closed, <0: error
			if (bytes_read <= 0) break;
			streamBuffer.append(buf, bytes_read);

			// Extract complete FIX messages
            while (true) {

                // Look for msg start: 8=FIX
                size_t beginPos = streamBuffer.find("8=FIX");

                // No FIX header yet, drop garbage before it
                if (beginPos == std::string::npos) {
                    streamBuffer.clear();
                    break;
                }

                // Discard everyrhing before "8=FIX"
                if (beginPos > 0) streamBuffer.erase(0, beginPos);

                // Find start of BodyLength field - 2nd field
                size_t bodyLenTagEnd = streamBuffer.find(SOH, 0);
                // incomplete msg, wait for more data to arrive
                if (bodyLenTagEnd == std::string::npos) break;

                // Find end of BodyLength field
                size_t bodyLenFieldEnd = streamBuffer.find(SOH, bodyLenTagEnd + 1);
                if (bodyLenFieldEnd == std::string::npos) break;

                // After the first SOH (after BeginString), we expect 9= (BodyLength).
                if (streamBuffer.compare(bodyLenTagEnd + 1, 2, "9=") != 0) {
                    // If not present then msg is malformed: Invalid FIX header - delete & start over
                    streamBuffer.erase(0, bodyLenTagEnd + 1);
                    continue;
                }

                // Extract BodyLength value
                std::string_view bodyLenStr(
                    streamBuffer.data() + bodyLenTagEnd + 3,
                    bodyLenFieldEnd - (bodyLenTagEnd + 3)
                );

                // If int conversion fails (non-digits) then its malformed - delete & start over
                int bodyLength = 0;
                if (!parseIntField(bodyLenStr, bodyLength)) {
                    streamBuffer.erase(0, bodyLenFieldEnd + 1);
                    continue;
                }

                // BodyLength defines message content size from bodyStart -> checksumStart
                size_t bodyStart = bodyLenFieldEnd + 1; // first byte of tag 35=MessageType
                size_t checksumStart = bodyStart + bodyLength; // where 10= should begin, according to BodyLength

                // missmatch: checksum-body length
                if (checksumStart > streamBuffer.size()) break;

                // last field 10= is exactly 7 bytes long (from checksumStart)
                if (streamBuffer.size() < checksumStart + 7) break;

                // Sanity check: "10=" is exactly where BodyLength says it should be
                if (streamBuffer.compare(checksumStart, 3, "10=") != 0) {
                    streamBuffer.erase(0, checksumStart); 
                    continue;
                }

                // Find checksum field end (SOH after 10=XYZ)
                size_t checksumEnd = streamBuffer.find(SOH, checksumStart);
                if (checksumEnd == std::string::npos) break;

                // full msg length: From 8=FIX -> last SOH of checksum
                size_t msgLen = checksumEnd + 1;

                // Validate FIX checksum algorithm: (Add all bytes from BeginString through SOH before 10=)%256
                unsigned int sum = 0;
                for (size_t i = 0; i < checksumStart; ++i) sum += static_cast<unsigned char>(streamBuffer[i]);
                unsigned int expected = sum % 256;

                // Extract checksum value
                std::string_view checksumStr(
                    streamBuffer.data() + checksumStart + 3,
                    checksumEnd - (checksumStart + 3)
                );
                int received = 0;
                if (checksumStr.size() != 3 || !parseIntField(checksumStr, received) || received != expected) {
                    std::cerr << "Bad checksum: expected " << expected
                        << " got " << received << "\n";
                    streamBuffer.erase(0, msgLen);
                    continue;
                }

                // Extract FixMessage
                FixMessage msg{};
                msg.len = msgLen;
                if (msg.len > sizeof(msg.data)) {
                    std::cerr << "FIX message too large\n";
                    streamBuffer.erase(0, msgLen);
                    continue;
                }

                // Get FixMessage
                memcpy(msg.data, streamBuffer.data(), msg.len);
                while (!recvQueue.enqueue(msg)) {
                    _mm_pause();
                }
                streamBuffer.erase(0, msgLen);
            }

		}
		});

	// consumer
	std::thread processor([&] {
		FixMessage msg;
		while (true) {
			if (recvQueue.dequeue(msg)) {
				std::string fix(msg.data, msg.len);
				std::cout << "FIX RECEIVED: " << fix << "\n";
				std::vector<output> get_out;
				std::string_view msg = fix;
				ParserOutcomes result = parseMsg(msg, get_out);

				if (result == ParserOutcomes::good) std::cout << "Message parsed good" << std::endl;
				else if (result == ParserOutcomes::unknown) std::cout << "Message parse  unknown" << std::endl;
				else if (result == ParserOutcomes::bad) std::cout << "Message parse bad" << std::endl;
			}
			else _mm_pause();
		}
		});

	recvThread.join();
	processor.join();

	closesocket(clientSock);
	closesocket(listenSock);
	WSACleanup();
	return 0;
}
