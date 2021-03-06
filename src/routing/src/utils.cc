/*
  Copyright (c) 2015, 2016, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "utils.h"

#include <algorithm>
#include <assert.h>
#include <cstring>
#include <stdexcept>
#include <stdlib.h>

#ifndef _MSC_VER
# include <arpa/inet.h>
# include <fcntl.h>
# include <sys/socket.h>
# include <sys/un.h>
#else
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include <stdint.h>
# if _MSC_VER <= 1900
#   define not !
# endif
#endif

void *get_in_addr(struct sockaddr *addr) {
  if (addr->sa_family == AF_INET) {
    return &(((struct sockaddr_in *) addr)->sin_addr);
  }

  return &(((struct sockaddr_in6 *) addr)->sin6_addr);
}

std::pair<std::string, int > get_peer_name(int sock) {
  socklen_t sock_len;
  struct sockaddr_storage addr;
  char result_addr[105];  // For IPv4, IPv6 and Unix socket
  int port;

  sock_len = static_cast<socklen_t>(sizeof addr);
  getpeername(sock, (struct sockaddr*)&addr, &sock_len);

  if (addr.ss_family == AF_INET6) {
    // IPv6
    auto *sin6 = (struct sockaddr_in6 *)&addr;
    port = ntohs(sin6->sin6_port);
    inet_ntop(AF_INET6, &sin6->sin6_addr, result_addr, static_cast<socklen_t>(sizeof result_addr));
  } else if (addr.ss_family == AF_INET) {
    // IPv4
    auto *sin4 = (struct sockaddr_in *)&addr;
    port = ntohs(sin4->sin_port);
    inet_ntop(AF_INET, &sin4->sin_addr, result_addr, static_cast<socklen_t>(sizeof result_addr));
  } else if (addr.ss_family == AF_UNIX) {
    // Unix socket, no good way to find peer
    return std::make_pair(std::string("unix socket"), 0);
  }

  return std::make_pair(std::string(result_addr), port);
}

std::vector<std::string> split_string(const std::string& data, const char delimiter, bool allow_empty) {
  std::stringstream ss(data);
  std::string token;
  std::vector<std::string> result;

  if (data.empty()) {
    return {};
  }

  while (std::getline(ss, token, delimiter)) {
    if (token.empty() && not allow_empty) {
      // Skip empty
      continue;
    }
    result.push_back(token);
  }

  // When last character is delimiter, it denotes an empty token
  if (allow_empty && data.back() == delimiter) {
    result.push_back("");
  }

  return result;
}

std::vector<std::string> split_string(const std::string& data, const char delimiter) {
  return split_string(data, delimiter, true);
}

std::array<uint8_t, 16> in_addr_to_array(const sockaddr_storage &addr) {
  std::array<uint8_t, 16> result{{0}};

  switch (addr.ss_family) {
  case AF_INET6:
    {
      const sockaddr_in6 *addr_intet6 = reinterpret_cast<const sockaddr_in6*>(&addr);
      std::memcpy(result.data(), &addr_intet6->sin6_addr, sizeof(addr_intet6->sin6_addr));
      break;
    }
  default:
    {
      const sockaddr_in *addr_intet = reinterpret_cast<const sockaddr_in*>(&addr);
      std::memcpy(result.data(), &addr_intet->sin_addr, sizeof(addr_intet->sin_addr));
    }
  }

  return result;
}


std::string get_message_error(int errcode)
{
#ifndef _WIN32
  return std::string(strerror(errcode));
#else
  if (errcode == SOCKET_ERROR || errcode == 0) {
    errcode = WSAGetLastError();
  }
  LPTSTR lpMsgBuf;

  FormatMessage(
    FORMAT_MESSAGE_ALLOCATE_BUFFER |
    FORMAT_MESSAGE_FROM_SYSTEM |
    FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL,
    errcode,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    (LPTSTR)&lpMsgBuf,
    0, NULL);
  std::string msgerr = "SystemError: ";
  msgerr += lpMsgBuf;
  LocalFree(lpMsgBuf);
  return msgerr;
#endif
}
