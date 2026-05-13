#include <string>
#include <string_view>
#include <stdexcept>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "ip_type.hpp"

#define NOMINMAX
#pragma comment(lib, "ws2_32.lib")
#include <WinSock2.h>
#include <WS2tcpip.h>

int g_bufferSize = 100;

// WSAStartup() -> getaddrinfo() -> socket() -> bind() -> listen() -> accept() -> send()/recv()
// WSAStartup() -> getaddrinfo() -> socket() -> connect()

SOCKET CreateSocket(const char* ipAddress, IP_TYPE ipType, std::uint16_t portNumber, bool isServer)
{
	const std::string portStr{ std::to_string(portNumber) };

	addrinfo hints{};
	hints.ai_socktype = SOCK_STREAM;

	switch (ipType)
	{
	case IP_TYPE::IPV4:
		hints.ai_family = AF_INET;
		break;
	case IP_TYPE::IPV6:
		hints.ai_family = AF_INET6;
		break;
	}

	if (isServer)
		hints.ai_flags = AI_PASSIVE;

	addrinfo* addresses = nullptr;
	const bool addressesSuccess = getaddrinfo(ipAddress, portStr.data(), &hints, &addresses) == 0;
	if (!addressesSuccess)
		throw std::runtime_error{ "Failed to configure socket address" };

	SOCKET sock = INVALID_SOCKET;

	for (addrinfo* address = addresses;
		(sock == INVALID_SOCKET) && address;
		address = address->ai_next)
	{
		sock = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
		if (sock == INVALID_SOCKET)
			continue;

		if (isServer)
		{
			const bool bindSuccess = bind(sock, address->ai_addr, address->ai_addrlen) == 0;
			if (!bindSuccess)
			{
				closesocket(sock);
				sock = INVALID_SOCKET;
				continue;
			}

			const bool listenSuccess = listen(sock, SOMAXCONN) == 0;
			if (!listenSuccess)
			{
				closesocket(sock);
				sock = INVALID_SOCKET;
				continue;
			}
		}
		else
		{
			const bool connectSuccess = connect(sock, address->ai_addr, address->ai_addrlen) == 0;
			if (!connectSuccess)
			{
				closesocket(sock);
				sock = INVALID_SOCKET;
				continue;
			}
		}
	}

	return sock;
}