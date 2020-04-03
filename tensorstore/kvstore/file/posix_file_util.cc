// Copyright 2020 The TensorStore Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _WIN32

#include "tensorstore/kvstore/file/posix_file_util.h"

namespace tensorstore {
namespace internal_file_util {

// There are two versions of the ::strerror_r function:
//
// XSI-compliant:
//
//     int strerror_r(int errnum, char* buf, size_t buflen);
//
//   Always writes message to supplied buffer.
//
// GNU-specific:
//
//     char *strerror_r(int errnum, char* buf, size_t buflen);
//
//   Either writes message to supplied buffer, or returns a static string.
//
// The following overloads are used to detect the return type and return the
// appropriate result.
namespace {
// GNU version
[[maybe_unused]] const char* GetStrerrorResult(const char* buf,
                                               const char* result) {
  return result;
}
// XSI-compliant version
[[maybe_unused]] const char* GetStrerrorResult(const char* buf, int result) {
  return buf;
}
}  // namespace

std::string GetOsErrorMessage(OsErrorCode error) {
  char buf[4096];
  buf[0] = 0;
  return GetStrerrorResult(buf, ::strerror_r(error, buf, std::size(buf)));
}

absl::StatusCode GetOsErrorStatusCode(OsErrorCode error) {
  switch (error) {
    case ENOENT:
      return absl::StatusCode::kNotFound;
    case EEXIST:
      return absl::StatusCode::kAlreadyExists;
    case ENOSPC:
    case ENOMEM:
      return absl::StatusCode::kResourceExhausted;
    case EACCES:
    case EPERM:
      return absl::StatusCode::kPermissionDenied;
    default:
      return absl::StatusCode::kFailedPrecondition;
  }
}

UniqueFileDescriptor OpenFileForWriting(const std::string& path) {
  UniqueFileDescriptor fd;
  const auto attempt_open = [&] {
    fd.reset(::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0666));
  };
#ifndef __APPLE__
  attempt_open();
#else
  // Maximum number of attempts to open the file.  On macOS, `open` may return
  // `ENOENT` or `EPERM` if an existing file is deleted concurrently with the
  // call to `open`.  We mitigate this race condition by retrying, but we limit
  // the number of retries to avoid getting stuck in an infinite loop if the
  // error is due to a parent directory having been deleted.
  constexpr int kMaxAttempts = 100;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    attempt_open();
    if (fd.valid() || (errno != ENOENT && errno != EPERM)) break;
  }
#endif
  return fd;
}

bool FileLock::Acquire(FileDescriptor fd) {
  while (true) {
    // This blocks until the lock is acquired.  If any signal is received by
    // the current thread, `fcntl` returns `EINTR`.
    //
    // Note: If we wanted to support cancellation while waiting on the lock,
    // we could pass the Promise to this function and register an
    // ExecuteWhenNotNeeded function that uses `pthread_kill` to send a signal
    // to this thread to abort the `fcntl` call (taking care to do it in a
    // race-free way).
#ifdef __linux__
    // Use Linux 3.15+ open file descriptor lock.
    struct ::flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    lock.l_pid = 0;
    if (::fcntl(fd, 38 /*F_OFD_SETLKW*/, &lock) == 0) return true;
#else
    // Use `flock` on BSD/Mac OS.
    if (::flock(fd, LOCK_EX) == 0) return true;
#endif
    if (errno == EINTR) continue;
    return false;
  }
}

bool DirectoryIterator::Next() {
  e = ::readdir(dir.get());
  return e != nullptr;
}

/// IsDirectoryAt returns whethere the `name` at the given directory fd is a
/// directory or not by using fstatat.
bool IsDirectoryAt(int dir_fd, const char* name) {
  struct ::stat statbuf;
  if (::fstatat(dir_fd, name, &statbuf, AT_SYMLINK_NOFOLLOW) != 0) {
    // Error stating file, assume it's not a directory.
    return false;
  }
  return S_ISDIR(statbuf.st_mode);
}

bool DirectoryIterator::is_directory() const {
  const auto d_type = e->d_type;
  return (d_type == DT_DIR || (d_type == DT_UNKNOWN &&
                               IsDirectoryAt(::dirfd(dir.get()), e->d_name)));
}

DirectoryIterator::Entry DirectoryIterator::GetEntry() const {
  return {::dirfd(dir.get()), e->d_name};
}

bool DirectoryIterator::Make(Entry entry,
                             std::unique_ptr<DirectoryIterator>* new_iterator) {
  UniqueFileDescriptor new_dir(
      ::openat(entry.dir_fd, entry.name,
               O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                   (entry.dir_fd == AT_FDCWD ? 0 : O_NOFOLLOW)));
  if (!new_dir.valid()) {
    switch (errno) {
      case ENOENT:
        return true;
      default:
        return false;
    }
  }
  UniqueDir dir(::fdopendir(new_dir.release()));
  if (!dir) {
    return false;
  }
  auto* it = new DirectoryIterator;
  it->dir = std::move(dir);
  it->e = nullptr;
  new_iterator->reset(it);
  return true;
}

}  // namespace internal_file_util
}  // namespace tensorstore

#endif
