#include <iostream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <array>
#include <vector>
#include <utility>
#include <memory>
#include <functional>
#include <mutex>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <stop_token>
#include <cstdint>
#include <cstddef>
#include <conio.h>
#include "ring_buffer.hpp"
#include "ip_type.hpp"

#define NOMINMAX
#pragma comment(lib, "ws2_32.lib")
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

extern int g_bufferSize;

SOCKET CreateSocket(const char*, IP_TYPE, std::uint16_t, bool);

int main()
{
	std::this_thread::sleep_for(std::chrono::seconds(1));

	// essentials:
	bool wsaSuccess = false;
	const std::array<std::string, 2> knownPeerIpAddresses{ "127.0.0.1", "127.0.0.1" };
	SOCKET localSocket = INVALID_SOCKET;

	std::mutex mtx;
	std::stop_source src;
	std::condition_variable receivedFromPeerCv;
	std::vector<SOCKET> peerSockets;
	std::vector<std::jthread> peerThreads;

	using PeerEntry = std::pair<std::string, ring_buffer<char, 25>>;
	std::vector<std::unique_ptr<PeerEntry>> peerEntries;

	std::jthread acceptIncomingConnThread,
				 displayThread;

	auto cleanup = [&]() {
		src.request_stop();

		receivedFromPeerCv.notify_all();

		if (localSocket != INVALID_SOCKET)
		{
			closesocket(localSocket);
			localSocket = INVALID_SOCKET;
		}

		if (acceptIncomingConnThread.joinable())
			acceptIncomingConnThread.join();

		for (SOCKET peerSocket : peerSockets)
			if (peerSocket != INVALID_SOCKET)
				closesocket(peerSocket);

		peerSockets.clear();

		for (std::jthread& t : peerThreads)
			if (t.joinable())
				t.join();

		peerThreads.clear();

		if (displayThread.joinable())
			displayThread.join();

		if (wsaSuccess)
			WSACleanup();
	};

	// set up node:
	try
	{
		WSAData wsa;
		wsaSuccess = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
		if (!wsaSuccess)
			throw std::runtime_error{ "Failed to initialize winsock library" };

		localSocket = CreateSocket(nullptr, IP_TYPE::IPV4, 8080, true);
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error> " << e.what() << '\n';
		cleanup();
		return 1;
	}

	auto listenToPeer = [&](std::stop_token st, SOCKET peerSocket, PeerEntry& pRef) {
		std::string buffer(g_bufferSize, '\0');

		while (!st.stop_requested())
		{
			const int bytesReceived = recv(peerSocket, buffer.data(), g_bufferSize, 0);
			if (bytesReceived <= 0)
				break;

			for (int i = 0; i < bytesReceived; ++i)
				pRef.second.push(buffer[i]);
			receivedFromPeerCv.notify_one();
		}
	};

	for (const std::string& peerIpAddress : knownPeerIpAddresses)
	{
		try
		{
			SOCKET peerSocket = CreateSocket(peerIpAddress.data(), IP_TYPE::IPV4, 8080, false);
			peerSockets.push_back(peerSocket);
			peerEntries.emplace_back(std::make_unique<PeerEntry>());
			auto& pPtr = peerEntries.back();
			pPtr->first = peerIpAddress;
			peerThreads.emplace_back(listenToPeer, src.get_token(), peerSocket, std::ref(*pPtr));
		}
		catch (...)
		{
		}
	}

	// set up threads:
	acceptIncomingConnThread = std::jthread{ [&](std::stop_token st) {
		while (!st.stop_requested())
		{
			fd_set readFds{};
			FD_ZERO(&readFds);
			FD_SET(localSocket, &readFds);

			const timeval tv{ .tv_sec = 0, .tv_usec = 250'000 };
			const int ready = select(0, &readFds, nullptr, nullptr, &tv);

			if ((ready > 0) && FD_ISSET(localSocket, &readFds))
			{
				sockaddr_storage peerAddress{};
				int peerSize = static_cast<int>(sizeof(peerAddress));

				SOCKET peerSocket
					= accept(localSocket, reinterpret_cast<sockaddr*>(&peerAddress), &peerSize);
				if (peerSocket == INVALID_SOCKET)
					continue;

				char peerIpAddress[NI_MAXHOST];
				const bool getnameinfoSuccess = getnameinfo(
					reinterpret_cast<sockaddr*>(&peerAddress),
					peerSize,
					peerIpAddress, sizeof(peerIpAddress),
					nullptr, 0,
					NI_NUMERICHOST
				) == 0;

				std::unique_lock<std::mutex> lck{ mtx };
				peerSockets.push_back(peerSocket);
				peerEntries.emplace_back(std::make_unique<PeerEntry>());
				auto& pPtr = peerEntries.back();
				pPtr->first = peerIpAddress;
				lck.unlock();

				peerThreads.emplace_back(listenToPeer, src.get_token(), peerSocket, std::ref(*pPtr));
			}
		}
	}, src.get_token() };

	displayThread = std::jthread{ [&](std::stop_token st) {
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		CONSOLE_CURSOR_INFO cursorInfo;
		GetConsoleCursorInfo(hOut, &cursorInfo);
		cursorInfo.bVisible = FALSE;
		SetConsoleCursorInfo(hOut, &cursorInfo);

		while (!st.stop_requested())
		{
			std::unique_lock<std::mutex> lck{ mtx };
			receivedFromPeerCv.wait(lck, [&peerEntries]() {return !peerEntries.empty(); });

			for (std::size_t row = 0; row < peerEntries.size(); ++row)
			{
				for (std::size_t column = 0; column < peerEntries[row]->first.size(); ++column)
				{
					COORD pos{ .X = static_cast<SHORT>(column), .Y = static_cast<SHORT>(row) };
					SetConsoleCursorPosition(hOut, pos);
					std::cout << peerEntries[row]->first[column];
				}

				const std::size_t offset = peerEntries[row]->first.size() + 2;

				auto& ringBufferRef = peerEntries[row]->second;
				for (std::size_t column = 0; column < ringBufferRef.size(); ++column)
				{
					COORD pos{ .X = static_cast<SHORT>(offset + column), .Y = static_cast<SHORT>(row) };
					SetConsoleCursorPosition(hOut, pos);
					std::cout << ringBufferRef.get_by_value(column);
				}
			}
		}
	}, src.get_token() };

	// propagate user input:
	char inputChar;
	while ((inputChar = _getch()) != '$')
	{
		std::lock_guard<std::mutex> lck{ mtx };
		for (SOCKET peerSocket : peerSockets)
			send(peerSocket, &inputChar, 1, 0);
	}

	cleanup();

	return 0;
}
