// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

// Standalone inclusion is permitted in dev builds and under clangd, so this
// file can be viewed and parsed on its own; release builds must come through
// the entry point.
#if !defined(CORVID_OS_ENUMS_ENTRY) && defined(NDEBUG) &&                     \
    !defined(CORVID_CLANGD)
#error "Include \"os_enums.h\" instead of this implementation header."
#endif

#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include "../../enums/sequence_enum.h"

// Windows implementation of "os_enums.h".
//
// Registered enums wrapping the Windows error macros used by the filesys
// classes.

namespace corvid { inline namespace filesys {

#pragma region win_error_code

// `ERROR_*`/`WSAE*` wrapper for `GetLastError` and `WSAGetLastError` values.
//
// This is a curated subset covering the values the library classifies or that
// commonly surface from file, pipe, and socket calls; it makes no attempt to
// be exhaustive. An unnamed value still round-trips through the enum and
// prints numerically.
enum class win_error_code : uint32_t {
  ok = ERROR_SUCCESS,                              // 0
  invalid_function = ERROR_INVALID_FUNCTION,       // 1
  file_not_found = ERROR_FILE_NOT_FOUND,           // 2
  path_not_found = ERROR_PATH_NOT_FOUND,           // 3
  too_many_open_files = ERROR_TOO_MANY_OPEN_FILES, // 4
  access_denied = ERROR_ACCESS_DENIED,             // 5
  invalid_handle = ERROR_INVALID_HANDLE,           // 6
  invalid_parameter = ERROR_INVALID_PARAMETER,     // 87
  broken_pipe = ERROR_BROKEN_PIPE,                 // 109
  open_failed = ERROR_OPEN_FAILED,                 // 110
  buffer_overflow = ERROR_BUFFER_OVERFLOW,         // 111
  disk_full = ERROR_DISK_FULL,                     // 112
  insufficient_buffer = ERROR_INSUFFICIENT_BUFFER, // 122
  invalid_name = ERROR_INVALID_NAME,               // 123
  no_data = ERROR_NO_DATA,                         // 232
  pipe_not_connected = ERROR_PIPE_NOT_CONNECTED,   // 233
  more_data = ERROR_MORE_DATA,                     // 234
  operation_aborted = ERROR_OPERATION_ABORTED,     // 995
  io_incomplete = ERROR_IO_INCOMPLETE,             // 996
  io_pending = ERROR_IO_PENDING,                   // 997
  noaccess = ERROR_NOACCESS,                       // 998
  intr = WSAEINTR,                                 // 10004
  badf = WSAEBADF,                                 // 10009
  acces = WSAEACCES,                               // 10013
  fault = WSAEFAULT,                               // 10014
  inval = WSAEINVAL,                               // 10022
  mfile = WSAEMFILE,                               // 10024
  wouldblock = WSAEWOULDBLOCK,                     // 10035
  inprogress = WSAEINPROGRESS,                     // 10036
  already = WSAEALREADY,                           // 10037
  notsock = WSAENOTSOCK,                           // 10038
  destaddrreq = WSAEDESTADDRREQ,                   // 10039
  msgsize = WSAEMSGSIZE,                           // 10040
  prototype = WSAEPROTOTYPE,                       // 10041
  noprotoopt = WSAENOPROTOOPT,                     // 10042
  protonosupport = WSAEPROTONOSUPPORT,             // 10043
  socktnosupport = WSAESOCKTNOSUPPORT,             // 10044
  opnotsupp = WSAEOPNOTSUPP,                       // 10045
  pfnosupport = WSAEPFNOSUPPORT,                   // 10046
  afnosupport = WSAEAFNOSUPPORT,                   // 10047
  addrinuse = WSAEADDRINUSE,                       // 10048
  addrnotavail = WSAEADDRNOTAVAIL,                 // 10049
  netdown = WSAENETDOWN,                           // 10050
  netunreach = WSAENETUNREACH,                     // 10051
  netreset = WSAENETRESET,                         // 10052
  connaborted = WSAECONNABORTED,                   // 10053
  connreset = WSAECONNRESET,                       // 10054
  nobufs = WSAENOBUFS,                             // 10055
  isconn = WSAEISCONN,                             // 10056
  notconn = WSAENOTCONN,                           // 10057
  shutdown = WSAESHUTDOWN,                         // 10058
  toomanyrefs = WSAETOOMANYREFS,                   // 10059
  timedout = WSAETIMEDOUT,                         // 10060
  connrefused = WSAECONNREFUSED,                   // 10061
  loop = WSAELOOP,                                 // 10062
  nametoolong = WSAENAMETOOLONG,                   // 10063
  hostdown = WSAEHOSTDOWN,                         // 10064
  hostunreach = WSAEHOSTUNREACH,                   // 10065
};
consteval auto corvid_enum_spec(win_error_code*) {
  return corvid::enums::sequence::make_sequence_enum_spec<win_error_code,
      "0,ok,invalid_function,file_not_found,path_not_found,"
      "too_many_open_files,access_denied,invalid_handle|"
      "87,invalid_parameter|"
      "109,broken_pipe,open_failed,buffer_overflow,disk_full|"
      "122,insufficient_buffer,invalid_name|"
      "232,no_data,pipe_not_connected,more_data|"
      "995,operation_aborted,io_incomplete,io_pending,noaccess|"
      "10004,intr,,,,,badf,,,,acces,fault,,,,,,,,inval,,mfile,,,,,,,,,,,"
      "wouldblock,inprogress,already,notsock,destaddrreq,msgsize,prototype,"
      "noprotoopt,protonosupport,socktnosupport,opnotsupp,pfnosupport,"
      "afnosupport,addrinuse,addrnotavail,netdown,netunreach,netreset,"
      "connaborted,connreset,nobufs,isconn,notconn,shutdown,toomanyrefs,"
      "timedout,connrefused,loop,nametoolong,hostdown,hostunreach">();
}

// Type-safe aliasing for `GetLastError` values.
using EC = win_error_code;

#pragma endregion
}} // namespace corvid::filesys
