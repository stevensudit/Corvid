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
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <linux/fscrypt.h>
#include <utility>
#include <csignal>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "../meta/formatting.h"
#include "../strings/no_zero.h"
#include "../enums/bitmask_enum.h"
#include "../enums/sequence_enum.h"

namespace corvid { inline namespace filesys {

using namespace corvid::strings::no_zero_funcs;

#pragma region msg_flags

// `MSG_*` wrapper.
enum class msg_flags : int {
  none = 0,                    // 0x0000'0000
  oob = MSG_OOB,               // 0x0000'0001
  peek = MSG_PEEK,             // 0x0000'0002
  dontroute = MSG_DONTROUTE,   // 0x0000'0004
  ctrunc = MSG_CTRUNC,         // 0x0000'0008
  proxy = MSG_PROXY,           // 0x0000'0010
  trunc = MSG_TRUNC,           // 0x0000'0020
  dontwait = MSG_DONTWAIT,     // 0x0000'0040
  eor = MSG_EOR,               // 0x0000'0080
  waitall = MSG_WAITALL,       // 0x0000'0100
  fin = MSG_FIN,               // 0x0000'0200
  syn = MSG_SYN,               // 0x0000'0400
  confirm = MSG_CONFIRM,       // 0x0000'0800
  rst = MSG_RST,               // 0x0000'1000
  errqueue = MSG_ERRQUEUE,     // 0x0000'2000
  nosignal = MSG_NOSIGNAL,     // 0x0000'4000
  more = MSG_MORE,             // 0x0000'8000
  waitforone = MSG_WAITFORONE, // 0x0001'0000
  batch = MSG_BATCH,           // 0x0004'0000
  zerocopy = MSG_ZEROCOPY,     // 0x0400'0000
  fastopen = MSG_FASTOPEN,     // 0x2000'0000
  cloexec = MSG_CMSG_CLOEXEC   // 0x4000'0000
};
consteval auto corvid_enum_spec(msg_flags*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<msg_flags,
      "cloexec,fastopen,,,zerocopy,,,,,,,,batch,,waitforone,more,nosignal,"
      "errqueue,rst,confirm,syn,fin,waitall,eor,dontwait,trunc,proxy,ctrunc,"
      "dontroute,peek,oob">();
}

#pragma endregion
#pragma region fcntl_ops

// `F_*` wrapper for `fcntl` operations.
// NOLINTNEXTLINE(performance-enum-size)
enum class fcntl_ops : int {
  dupfd = F_DUPFD,                 // 0
  getfd = F_GETFD,                 // 1
  setfd = F_SETFD,                 // 2
  getfl = F_GETFL,                 // 3
  setfl = F_SETFL,                 // 4
  getlk = F_GETLK,                 // 5
  setlk = F_SETLK,                 // 6
  setlkw = F_SETLKW,               // 7
  setown = F_SETOWN,               // 8
  getown = F_GETOWN,               // 9
  setsig = F_SETSIG,               // 10
  getsig = F_GETSIG,               // 11
  getlk64 = F_GETLK64,             // 12
  setlk64 = F_SETLK64,             // 13
  setlkw64 = F_SETLKW64,           // 14
  setownex = F_SETOWN_EX,          // 15
  getownex = F_GETOWN_EX,          // 16
  dupfd_cloexec = F_DUPFD_CLOEXEC, // 1030
};
consteval auto corvid_enum_spec(fcntl_ops*) {
  return corvid::enums::sequence::make_sequence_enum_spec<fcntl_ops,
      "dupfd,getfd,setfd,getfl,setfl,getlk,setlk,setlkw,setown,getown,setsig,"
      "getsig,getlk64,setlk64,setlkw64,setownex,getownex">();
}

#pragma endregion
#pragma region errno_code

// `E_*` wrapper for `errno` values.
//
// Note that, although this is much like `std::errc`, it has additional values
// that are found in Linux but not in the C++ standard library, and it's safe
// to register as a sequence enum.
//
// NOLINTNEXTLINE(performance-enum-size)
enum class errno_code : int {
  ok = 0,
  perm = EPERM,                     // 1
  noent = ENOENT,                   // 2
  srch = ESRCH,                     // 3
  intr = EINTR,                     // 4
  io = EIO,                         // 5
  nxio = ENXIO,                     // 6
  toobig = E2BIG,                   // 7
  noexec = ENOEXEC,                 // 8
  badf = EBADF,                     // 9
  child = ECHILD,                   // 10
  again = EAGAIN,                   // 11
  wouldblock = EAGAIN,              // 11 is EAGAIN
  nomem = ENOMEM,                   // 12
  acces = EACCES,                   // 13
  fault = EFAULT,                   // 14
  notblk = ENOTBLK,                 // 15
  busy = EBUSY,                     // 16
  exist = EEXIST,                   // 17
  xdev = EXDEV,                     // 18
  nodev = ENODEV,                   // 19
  notdir = ENOTDIR,                 // 20
  isdir = EISDIR,                   // 21
  inval = EINVAL,                   // 22
  nfile = ENFILE,                   // 23
  mfile = EMFILE,                   // 24
  notty = ENOTTY,                   // 25
  txtbsy = ETXTBSY,                 // 26
  fbig = EFBIG,                     // 27
  nospc = ENOSPC,                   // 28
  spipe = ESPIPE,                   // 29
  rofs = EROFS,                     // 30
  mlink = EMLINK,                   // 31
  pipe = EPIPE,                     // 32
  dom = EDOM,                       // 33
  range = ERANGE,                   // 34
  deadlk = EDEADLK,                 // 35
  nametoolong = ENAMETOOLONG,       // 36
  nolck = ENOLCK,                   // 37
  nosys = ENOSYS,                   // 38
  notempty = ENOTEMPTY,             // 39
  loop = ELOOP,                     // 40
  old_wouldblock = 41,              // 41 was EWOULDBLOCK
  nomsg = ENOMSG,                   // 42
  idrm = EIDRM,                     // 43
  chrng = ECHRNG,                   // 44
  l2nsync = EL2NSYNC,               // 45
  l3hlt = EL3HLT,                   // 46
  l3rst = EL3RST,                   // 47
  lnrng = ELNRNG,                   // 48
  unatch = EUNATCH,                 // 49
  ncsi = ENOCSI,                    // 50
  l2hlt = EL2HLT,                   // 51
  bade = EBADE,                     // 52
  badr = EBADR,                     // 53
  xfull = EXFULL,                   // 54
  noano = ENOANO,                   // 55
  badrqc = EBADRQC,                 // 56
  badslt = EBADSLT,                 // 57
  old_deadlock = 58,                // 58 is EDEADLOCK/EDEADLK
  bfont = EBFONT,                   // 59
  nostr = ENOSTR,                   // 60
  nodata = ENODATA,                 // 61
  time = ETIME,                     // 62
  nosr = ENOSR,                     // 63
  nonet = ENONET,                   // 64
  nopkg = ENOPKG,                   // 65
  remote = EREMOTE,                 // 66
  nolink = ENOLINK,                 // 67
  adv = EADV,                       // 68
  srmnt = ESRMNT,                   // 69
  comm = ECOMM,                     // 70
  proto = EPROTO,                   // 71
  multihop = EMULTIHOP,             // 72
  dotdot = EDOTDOT,                 // 73
  badmsg = EBADMSG,                 // 74
  overflow = EOVERFLOW,             // 75
  notuniq = ENOTUNIQ,               // 76
  badfd = EBADFD,                   // 77
  remchg = EREMCHG,                 // 78
  libacc = ELIBACC,                 // 79
  libbad = ELIBBAD,                 // 80
  libscn = ELIBSCN,                 // 81
  libmax = ELIBMAX,                 // 82
  libexec = ELIBEXEC,               // 83
  ilseq = EILSEQ,                   // 84
  restart = ERESTART,               // 85
  strpipe = ESTRPIPE,               // 86
  users = EUSERS,                   // 87
  notsock = ENOTSOCK,               // 88
  destaddrreq = EDESTADDRREQ,       // 89
  msgsize = EMSGSIZE,               // 90
  prototype = EPROTOTYPE,           // 91
  noprotoopt = ENOPROTOOPT,         // 92
  protonosupport = EPROTONOSUPPORT, // 93
  socktnosupport = ESOCKTNOSUPPORT, // 94
  opnotsupp = EOPNOTSUPP,           // 95
  pfnosupport = EPFNOSUPPORT,       // 96
  afnosupport = EAFNOSUPPORT,       // 97
  addrinuse = EADDRINUSE,           // 98
  addrnotavail = EADDRNOTAVAIL,     // 99
  netdown = ENETDOWN,               // 100
  netunreach = ENETUNREACH,         // 101
  netreset = ENETRESET,             // 102
  connaborted = ECONNABORTED,       // 103
  connreset = ECONNRESET,           // 104
  nobufs = ENOBUFS,                 // 105
  isconn = EISCONN,                 // 106
  notconn = ENOTCONN,               // 107
  shutdown = ESHUTDOWN,             // 108
  toomanyrefs = ETOOMANYREFS,       // 109
  timedout = ETIMEDOUT,             // 110
  connrefused = ECONNREFUSED,       // 111
  hostdown = EHOSTDOWN,             // 112
  hostunreach = EHOSTUNREACH,       // 113
  already = EALREADY,               // 114
  inprogress = EINPROGRESS,         // 115
  stale = ESTALE,                   // 116
  uclean = EUCLEAN,                 // 117
  notnam = ENOTNAM,                 // 118
  navail = ENAVAIL,                 // 119
  isnam = EISNAM,                   // 120
  remoteio = EREMOTEIO,             // 121
  dquot = EDQUOT,                   // 122
  nomedium = ENOMEDIUM,             // 123
  mediumtype = EMEDIUMTYPE,         // 124
  canceled = ECANCELED,             // 125
  nokey = ENOKEY,                   // 126
  keyexpired = EKEYEXPIRED,         // 127
  keyrevoked = EKEYREVOKED,         // 128
  keyrejected = EKEYREJECTED,       // 129
  ownerdead = EOWNERDEAD,           // 130
  notrecoverable = ENOTRECOVERABLE, // 131
  rfkill = ERFKILL,                 // 132
  hwpoison = EHWPOISON,             // 133
};
consteval auto corvid_enum_spec(errno_code*) {
  return corvid::enums::sequence::make_sequence_enum_spec<errno_code,
      "ok,perm,noent,srch,intr,io,nxio,toobig,noexec,badf,child,again,nomem,"
      "acces,fault,notblk,busy,exist,xdev,nodev,notdir,isdir,inval,nfile,"
      "mfile,notty,txtbsy,fbig,nospc,spipe,rofs,mlink,pipe,dom,range,deadlk,"
      "nametoolong,nolck,nosys,notempty,loop,old_wouldblock,nomsg,idrm,chrng,"
      "l2nsync,l3hlt,l3rst,lnrng,unatch,ncsi,l2hlt,bade,badr,exfull,noano,"
      "badrqc,badslt,old_deadlock,bfont,nostr,nodata,time,nosr,nonet,nopkg,"
      "remote,nolink,adv,srmnt,comm,proto,multihop,dotdot,badmsg,overflow,"
      "notuniq,badfd,remchg,libacc,libbad,libscn,libmax,libexec,ilseq,restart,"
      "strpipe,users,notsock,destaddrreq,msgsize,prototype,noprotoopt,"
      "protonosupport,socktnosupport,opnotsupp,pfnosupport,afnosupport,"
      "addrinuse,addrnotavail,netdown,netunreach,netreset,connaborted,"
      "connreset,nobufs,isconn,notconn,shutdown,toomanyrefs,timedout,"
      "connrefused,hostdown,hostunreach,already,inprogress,stale,uclean,"
      "notnam,navail,isnam,remoteio,dquot,nomedium,mediumtype,canceled,nokey,"
      "keyexpired,keyrevoked,keyrejected,ownerdead,notrecoverable,rfkill,"
      "hwpoison">();
}

// Type-safe aliasing for `errno`.
using EC = errno_code;
inline errno_code e_code() { return errno_code{errno}; };
inline bool e_code_is(errno_code code) { return e_code() == code; }

#pragma endregion
#pragma region o_flags

// `O_*` wrapper for `open` flags.
enum class o_flags : int {
  rdonly = O_RDONLY,                     // 0x0000'0000 sequence value
  wronly = O_WRONLY,                     // 0x0000'0001 sequence value
  rdwr = O_RDWR,                         // 0x0000'0002 sequence value
  accmode = O_ACCMODE,                   // 0x0000'0003 mask for above three
  creat = O_CREAT,                       // 0x0000'0040
  excl = O_EXCL,                         // 0x0000'0080
  noctty = O_NOCTTY,                     // 0x0000'0100
  trunc = O_TRUNC,                       // 0x0000'0200
  append = O_APPEND,                     // 0x0000'0400
  nonblock = O_NONBLOCK,                 // 0x0000'0800
  dsync = O_DSYNC,                       // 0x0000'1000
  async = O_ASYNC,                       // 0x0000'2000
  direct = O_DIRECT,                     // 0x0000'4000
  largefile = O_LARGEFILE,               // 0x0000'8000
  directory = O_DIRECTORY,               // 0x0001'0000
  nofollow = O_NOFOLLOW,                 // 0x0002'0000
  noattime = O_NOATIME,                  // 0x0004'0000
  cloexec = O_CLOEXEC,                   // 0x0008'0000
  raw_osync = O_FSYNC & O_DSYNC,         // 0x0010'0000 aka __O_SYNC.
  osync = O_SYNC,                        // 0x0010'1000 raw_osync + dsync
  fsync = O_FSYNC,                       // 0x0010'1000 alias for osync
  rsync = O_RSYNC,                       // 0x0010'1000 alias for osync
  path = O_PATH,                         // 0x0020'0000
  raw_tmpfile = O_TMPFILE & O_DIRECTORY, // 0x0040'0000 aka __O_TMPFILE
  tmpfile = O_TMPFILE,                   // 0x0041'0000 raw_tmpfile + directory
};
consteval auto corvid_enum_spec(o_flags*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<o_flags,
      "raw_tmpfile,path,raw_osync,cloexec,noattime,nofollow,directory,"
      "largefile,direct,async,dsync,nonblock,append,trunc,noctty,excl,creat,,,"
      ",,rdwr,wronly">();
}

#pragma endregion

// TODO: Move out into "mmap.h", which also wraps `::map` and `::madvise` and
// defines a RAII `mmap` wrapper class.

#pragma region mmap_prot

// `PROT_*` wrapper.
enum class mmap_prot : uint32_t {
  none = PROT_NONE,           // 0x00
  read = PROT_READ,           // 0x01
  write = PROT_WRITE,         // 0x02
  exec = PROT_EXEC,           // 0x04
  growsdown = PROT_GROWSDOWN, // 0x01000000
  growsup = PROT_GROWSUP,     // 0x02000000
};

consteval auto corvid_enum_spec(mmap_prot*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<mmap_prot,
      "exec,write,read">();
}

#pragma endregion
#pragma region mmap_mask

// `MAP_*` wrapper.
enum class mmap_mask : uint32_t {
  file = MAP_FILE,                       // 0x00
  shared = MAP_SHARED,                   // 0x01
  map_private = MAP_PRIVATE,             // 0x02
  shared_validate = MAP_SHARED_VALIDATE, // 0x03
  mask_type = MAP_TYPE,                  // 0x0f
  map_huge_mask = MAP_HUGE_MASK,         // 0x3f
  fixed = MAP_FIXED,                     // 0x00010
  anonymous = MAP_ANONYMOUS,             // 0x00020
  growsdown = MAP_GROWSDOWN,             // 0x00100
  denywrite = MAP_DENYWRITE,             // 0x00800
  executable = MAP_EXECUTABLE,           // 0x01000
  locked = MAP_LOCKED,                   // 0x02000
  noreserve = MAP_NORESERVE,             // 0x04000
  populate = MAP_POPULATE,               // 0x08000
  nonblock = MAP_NONBLOCK,               // 0x10000
  stack = MAP_STACK,                     // 0x20000
  hugetlb = MAP_HUGETLB,                 // 0x40000
  sync = MAP_SYNC,                       // 0x80000
  fixed_noreplace = MAP_FIXED_NOREPLACE, // 0x100000
};

consteval auto corvid_enum_spec(mmap_mask*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<mmap_mask,
      "fixed_noreplace,sync,hugetlb,stack,nonblock,populate,noreserve,locked,"
      "executable,denywrite,,,growsdown,,,anonymous,fixed,,,private,shared">();
}

#pragma endregion
#pragma region mmap_advice

// `MADV_*` wrapper.
// NOLINTNEXTLINE(performance-enum-size)
enum class mmap_advice : int32_t {
  normal = MADV_NORMAL,                   // 0
  random = MADV_RANDOM,                   // 1
  sequential = MADV_SEQUENTIAL,           // 2
  willneed = MADV_WILLNEED,               // 3
  dontneed = MADV_DONTNEED,               // 4
  free = MADV_FREE,                       // 8
  remove = MADV_REMOVE,                   // 9
  dontfork = MADV_DONTFORK,               // 10
  dofork = MADV_DOFORK,                   // 11
  mergeable = MADV_MERGEABLE,             // 12
  unmergeable = MADV_UNMERGEABLE,         // 13
  hugepage = MADV_HUGEPAGE,               // 14
  nohugepage = MADV_NOHUGEPAGE,           // 15
  dontdump = MADV_DONTDUMP,               // 16
  dodump = MADV_DODUMP,                   // 17
  wipeonfork = MADV_WIPEONFORK,           // 18
  keeponfork = MADV_KEEPONFORK,           // 19
  cold = MADV_COLD,                       // 20
  pageout = MADV_PAGEOUT,                 // 21
  populate_read = MADV_POPULATE_READ,     // 22
  populate_write = MADV_POPULATE_WRITE,   // 23
  dontneed_locked = MADV_DONTNEED_LOCKED, // 24
  collapse = MADV_COLLAPSE,               // 25
  hwpoison = MADV_HWPOISON,               // 100
};
consteval auto corvid_enum_spec(mmap_advice*) {
  return corvid::enums::sequence::make_sequence_enum_spec<mmap_advice,
      "normal,random,sequential,willneed,dontneed,,,,free,remove,dontfork,"
      "dofork,mergeable,unmergeable,hugepage,nohugepage,dontdump,dodump,"
      "wipeonfork,keeponfork,cold,pageout,populate_read,populate_write,"
      "dontneed_locked,collapse">();
}

#pragma endregion
#pragma region details

namespace details {
// Platform file handle type and invalid-handle sentinel.
using file_handle_t = int;
constexpr file_handle_t invalid_file_handle = -1;
} // namespace details

#pragma endregion
#pragma region os_file

// RAII wrapper around an OS file descriptor or handle.
//
// `os_file` owns a single file and closes it on destruction.
// It is movable and non-copyable. `control` wraps `fcntl`; `get_flags`,
// `set_flags`, and `set_nonblocking` are named helpers for common fd-level
// operations.
//
// Platform-specific code is isolated in a guarded section.
class [[nodiscard]] os_file {
public:
#pragma region Types

  using file_handle_t = details::file_handle_t;
  static constexpr file_handle_t invalid_file_handle =
      details::invalid_file_handle;

#pragma endregion
#pragma region Construction

  // Adopt an existing handle. Defaults to an invalid (closed) file.
  explicit os_file(file_handle_t h = invalid_file_handle) noexcept
      : handle_{h} {}

  os_file(const os_file&) = delete;
  os_file& operator=(const os_file&) = delete;

  os_file(os_file&& other) noexcept : handle_{other.release()} {}

  os_file& operator=(os_file&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = other.release();
    }
    return *this;
  }

  ~os_file() { close(); }

#pragma endregion
#pragma region Accessors

  // True if the handle is valid (i.e., the file is open).
  [[nodiscard]] bool is_open() const noexcept {
    return handle_ != invalid_file_handle;
  }

  [[nodiscard]] explicit operator bool() const noexcept { return is_open(); }
  [[nodiscard]] bool operator!() const noexcept { return !is_open(); }

  // Return the raw platform handle.
  [[nodiscard]] file_handle_t handle() const noexcept { return handle_; }
  [[nodiscard]] file_handle_t operator*() const noexcept { return handle_; }

#pragma endregion
#pragma region Formatting

  // Render as `fd=<handle>` for an open file, or `fd=closed`. As a
  // `self_rendering_formatter` target, it applies the spec's width, fill, and
  // align, plus precision (truncating the rendered text), itself, since it
  // knows its own length. Any dynamic width/precision arrives already
  // resolved.
  template<corvid::CharType CharT, typename OutIt>
  OutIt
  format_to_spec(const corvid::parsed_spec<CharT>& spec, OutIt out) const {
    std::string content{"fd="};
    if (is_open())
      strings::append_num(content, handle_);
    else
      content += "closed";
    std::string_view view{content};
    if (const auto prec = spec.precision) view = view.substr(0, *prec);
    return spec.write_padded(out, view, spec.width);
  }

#pragma endregion
#pragma region Close and release

  // Close the file. Idempotent. Returns true when the file was open and is
  // now closed, false if it could not be closed (likely because it already
  // was). Note that, on failure, the file is left in a closed state to avoid
  // potential reuse of a stale handle.
  bool close() noexcept {
    if (!is_open()) return false;
    const auto old_handle = handle_;
    handle_ = invalid_file_handle;
    return (::close(old_handle) == 0);
  }

  // Release ownership and return the handle without closing it.
  [[nodiscard]] file_handle_t release() noexcept {
    const auto h = handle_;
    handle_ = invalid_file_handle;
    return h;
  }

#pragma endregion
#pragma region I/O

  // Write as much of `data` as possible to the file. On success, removes the
  // written prefix from `data` and returns true. On failure, leaves `data`
  // unchanged and returns false. A "soft" failure (e.g., EAGAIN) is treated
  // as success with no progress. Note that this call can invoke a SIGPIPE on
  // broken pipes/sockets, so use `net_socket::send` with MSG_NOSIGNAL instead.
  [[nodiscard]] bool write(std::string_view& data) const {
    if (data.empty()) return true;

    const ssize_t n = ::write(handle_, data.data(), data.size());
    if (n <= 0) return !is_hard_error();

    data.remove_prefix(static_cast<size_t>(n));
    return true;
  }

  // Read up to `data.size()` bytes from the file into `data`. Use
  // `no_zero::enlarge_to_cap` or `no_zero::enlarge_to` to get the desired
  // size.
  //
  // On success, resizes `data` to the number of bytes read and returns true. A
  // "soft" failure (e.g., EAGAIN) is treated as success with zero bytes read.
  // On EOF/disconnect, leaves `data` unchanged and returns false. On hard
  // failure, clears `data` and returns false.
  [[nodiscard]] bool read(std::string& data) const {
    if (data.empty()) return true;

    // Read up to the current size.
    // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
    const ssize_t n = ::read(handle_, data.data(), data.size());

    // EOF/disconnect. Return false without clearing `data`.
    if (n == 0) return false;

    // Update `data` to the size actually read.
    no_zero::trim_to(data, n);

    // If retriable, treat as a success with nothing read, while a hard error
    // is a failure with `data` cleared.
    if (n < 0) return !is_hard_error();
    return true;
  }

  // Write all of `data` to the file, retrying after partial writes and soft
  // errors (e.g., `EINTR`). Returns true only when all bytes have been
  // written. On hard failure, returns false with an indeterminate prefix of
  // `data` already sent. Intended for blocking I/O; on non-blocking fds, a
  // full kernel buffer causes a busy-loop.
  [[nodiscard]] bool write_all(std::string_view data) const {
    while (!data.empty())
      if (!write(data)) return false;
    return true;
  }

  // Read exactly `data.size()` bytes into `data`, retrying after partial
  // reads and soft errors (e.g., `EINTR`). Size `data` with `data.resize(n)`
  // or `no_zero::enlarge_to(data, n)` before calling.
  //
  // Returns true only when all bytes have been read. On EOF before
  // completion, trims `data` to the bytes received and returns false. On hard
  // failure, clears `data` and returns false. Intended for blocking I/O; on
  // non-blocking fds, an empty kernel buffer causes a busy-loop.
  [[nodiscard]] bool read_exact(std::string& data) const {
    size_t offset = 0;
    const size_t target = data.size();
    while (offset < target) {
      const ssize_t n = ::read(handle_, data.data() + offset, target - offset);
      // On EOF, trim to bytes received and fail.
      if (n == 0) {
        no_zero::trim_to(data, offset);
        return false;
      }
      // On hard error, clear `data` and fail.
      if (n < 0) {
        if (!is_hard_error()) continue;
        data.clear();
        return false;
      }
      offset += static_cast<size_t>(n);
    }
    return true;
  }

#pragma endregion
#pragma region Control

  // Invoke `fcntl(cmd, args...)` on the handle. Returns -1 on failure.
  template<typename... Args>
  [[nodiscard]] int control(fcntl_ops cmd, Args&&... args) const noexcept {
    return ::fcntl(handle_, *cmd, std::forward<Args>(args)...);
  }

  // Return the fd status flags via `fcntl(F_GETFL)`.
  [[nodiscard]] std::optional<o_flags> get_flags() const noexcept {
    auto flags = o_flags{control(fcntl_ops::getfl)};
    if (*flags == -1) return std::nullopt;
    return flags;
  }

  // Set the fd status flags via `fcntl(F_SETFL)`. Returns false on failure.
  // These are the "O_" flags.
  [[nodiscard]] bool set_flags(o_flags flags) const noexcept {
    return control(fcntl_ops::setfl, *flags) == 0;
  }

  // Enable or disable non-blocking I/O via `fcntl(F_SETFL, O_NONBLOCK)`.
  // But consider opening with `O_NONBLOCK` in the first place.
  [[nodiscard]] bool set_nonblocking(bool on = true) const noexcept {
    const auto flags = get_flags();
    if (!flags) return false;
    auto new_flags = bitmask::set_to(*flags, o_flags::nonblock, on);
    return set_flags(new_flags);
  }

#pragma endregion
#pragma region Errors

  // Check whether the last error was a hard error (true) or a soft error
  // (false). A soft error represents flow control conditions that are expected
  // to occur in normal operation and can be retried, while a hard error is an
  // actual failure that should be handled as such.
  //
  // Note that `errno` is only meaningful immediately after a failure
  // return from a system call and is invalidated by the next system call.
  static bool is_hard_error(errno_code err = e_code()) noexcept {
    assert(err);
    return (err != EC::again && err != EC::wouldblock && err != EC::intr);
  }

#pragma endregion
#pragma region Data members
private:
  file_handle_t handle_{invalid_file_handle};

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys

#pragma region formatter

// Format an `os_file` by self-rendering as `fd=<handle>` (or `fd=closed`),
// applying the spec's width/fill/align and precision via `format_to_spec`.
template<corvid::CharType CharT>
struct std::formatter<corvid::filesys::os_file, CharT>
    : corvid::self_rendering_formatter<CharT> {};

#pragma endregion
