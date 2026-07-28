#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

enum {
  JANUS_SYSTEM_NOT_FOUND = 0,
  JANUS_SYSTEM_PERMISSION_DENIED = 1,
  JANUS_SYSTEM_ALREADY_EXISTS = 2,
  JANUS_SYSTEM_INVALID_INPUT = 3,
  JANUS_SYSTEM_INTERRUPTED = 4,
  JANUS_SYSTEM_WOULD_BLOCK = 5,
  JANUS_SYSTEM_OUT_OF_SPACE = 6,
  JANUS_SYSTEM_TOO_LARGE = 7,
  JANUS_SYSTEM_UNSUPPORTED = 8,
  JANUS_SYSTEM_RESOURCE_EXHAUSTED = 9,
  JANUS_SYSTEM_OTHER = 10,
};

static _Thread_local uint32_t janus_system_native_error;
static _Thread_local int32_t janus_system_portable_error = JANUS_SYSTEM_OTHER;

static void janus_system_clear_error(void) {
  janus_system_native_error = 0;
  janus_system_portable_error = JANUS_SYSTEM_OTHER;
}

static void janus_system_set_error(uint32_t native_code,
                                   int32_t portable_category) {
  janus_system_native_error = native_code;
  janus_system_portable_error = portable_category;
}

uint32_t janus_system_error_code(void) { return janus_system_native_error; }

int32_t janus_system_error_category(void) {
  return janus_system_portable_error;
}

static int janus_system_valid_utf8(const unsigned char *data, uint64_t size) {
  uint64_t index = 0;
  while (index < size) {
    const unsigned char first = data[index++];
    if (first <= 0x7f) {
      if (first == 0)
        return 0;
      continue;
    }
    uint32_t codepoint;
    unsigned continuation_count;
    if (first >= 0xc2 && first <= 0xdf) {
      codepoint = first & 0x1f;
      continuation_count = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      codepoint = first & 0x0f;
      continuation_count = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      codepoint = first & 0x07;
      continuation_count = 3;
    } else {
      return 0;
    }
    if (size - index < continuation_count)
      return 0;
    for (unsigned offset = 0; offset < continuation_count; ++offset) {
      const unsigned char next = data[index++];
      if ((next & 0xc0) != 0x80)
        return 0;
      codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((continuation_count == 2 && codepoint < 0x800) ||
        (continuation_count == 3 && codepoint < 0x10000) ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
        codepoint > 0x10ffff)
      return 0;
  }
  return 1;
}

#if defined(_WIN32)

static int32_t janus_system_windows_category(DWORD code) {
  switch (code) {
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
  case ERROR_INVALID_DRIVE:
    return JANUS_SYSTEM_NOT_FOUND;
  case ERROR_ACCESS_DENIED:
  case ERROR_SHARING_VIOLATION:
  case ERROR_LOCK_VIOLATION:
    return JANUS_SYSTEM_PERMISSION_DENIED;
  case ERROR_FILE_EXISTS:
  case ERROR_ALREADY_EXISTS:
    return JANUS_SYSTEM_ALREADY_EXISTS;
  case ERROR_INVALID_HANDLE:
  case ERROR_INVALID_NAME:
  case ERROR_INVALID_PARAMETER:
  case ERROR_NO_UNICODE_TRANSLATION:
    return JANUS_SYSTEM_INVALID_INPUT;
  case ERROR_OPERATION_ABORTED:
    return JANUS_SYSTEM_INTERRUPTED;
  case ERROR_DISK_FULL:
  case ERROR_HANDLE_DISK_FULL:
    return JANUS_SYSTEM_OUT_OF_SPACE;
  case ERROR_FILENAME_EXCED_RANGE:
    return JANUS_SYSTEM_TOO_LARGE;
  case ERROR_NOT_SUPPORTED:
  case ERROR_CALL_NOT_IMPLEMENTED:
    return JANUS_SYSTEM_UNSUPPORTED;
  case ERROR_TOO_MANY_OPEN_FILES:
  case ERROR_NOT_ENOUGH_MEMORY:
  case ERROR_OUTOFMEMORY:
    return JANUS_SYSTEM_RESOURCE_EXHAUSTED;
  default:
    return JANUS_SYSTEM_OTHER;
  }
}

static void janus_system_capture_windows_error(void) {
  const DWORD code = GetLastError();
  janus_system_set_error((uint32_t)code,
                         janus_system_windows_category(code));
}

static wchar_t *janus_system_windows_path(const char *path, uint64_t length) {
  if (path == NULL || length == 0 || length > INT_MAX ||
      !janus_system_valid_utf8((const unsigned char *)path, length)) {
    janus_system_set_error(ERROR_INVALID_NAME, JANUS_SYSTEM_INVALID_INPUT);
    return NULL;
  }
  const int wide_length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, (int)length,
                          NULL, 0);
  if (wide_length <= 0) {
    janus_system_capture_windows_error();
    return NULL;
  }
  if ((size_t)wide_length > (SIZE_MAX / sizeof(wchar_t)) - 1) {
    janus_system_set_error(ERROR_FILENAME_EXCED_RANGE,
                           JANUS_SYSTEM_TOO_LARGE);
    return NULL;
  }
  wchar_t *wide =
      (wchar_t *)malloc(((size_t)wide_length + 1) * sizeof(wchar_t));
  if (wide == NULL) {
    janus_system_set_error(ERROR_OUTOFMEMORY,
                           JANUS_SYSTEM_RESOURCE_EXHAUSTED);
    return NULL;
  }
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, (int)length,
                          wide, wide_length) != wide_length) {
    janus_system_capture_windows_error();
    free(wide);
    return NULL;
  }
  wide[wide_length] = L'\0';
  return wide;
}

intptr_t janus_system_open(const char *path, uint64_t length, int32_t mode) {
  if (mode < 0 || mode > 2) {
    janus_system_set_error(ERROR_INVALID_PARAMETER,
                           JANUS_SYSTEM_INVALID_INPUT);
    return -1;
  }
  wchar_t *wide = janus_system_windows_path(path, length);
  if (wide == NULL)
    return -1;
  const DWORD access =
      mode == 0 ? GENERIC_READ : (mode == 2 ? FILE_APPEND_DATA : GENERIC_WRITE);
  const DWORD creation = mode == 0 ? OPEN_EXISTING
                                   : (mode == 1 ? CREATE_ALWAYS : OPEN_ALWAYS);
  HANDLE handle = CreateFileW(wide, access,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL);
  free(wide);
  if (handle == INVALID_HANDLE_VALUE) {
    janus_system_capture_windows_error();
    return -1;
  }
  janus_system_clear_error();
  return (intptr_t)handle;
}

intptr_t janus_system_read(intptr_t handle, void *data, uint64_t capacity) {
  if (handle == -1 || (data == NULL && capacity != 0)) {
    janus_system_set_error(ERROR_INVALID_HANDLE, JANUS_SYSTEM_INVALID_INPUT);
    return -1;
  }
  const DWORD requested =
      capacity > UINT32_MAX ? UINT32_MAX : (DWORD)capacity;
  DWORD read_count = 0;
  if (!ReadFile((HANDLE)handle, data, requested, &read_count, NULL)) {
    janus_system_capture_windows_error();
    return -1;
  }
  janus_system_clear_error();
  return (intptr_t)read_count;
}

intptr_t janus_system_write(intptr_t handle, const void *data, uint64_t size) {
  if (handle == -1 || (data == NULL && size != 0)) {
    janus_system_set_error(ERROR_INVALID_HANDLE, JANUS_SYSTEM_INVALID_INPUT);
    return -1;
  }
  const DWORD requested = size > UINT32_MAX ? UINT32_MAX : (DWORD)size;
  DWORD written = 0;
  if (!WriteFile((HANDLE)handle, data, requested, &written, NULL)) {
    janus_system_capture_windows_error();
    return -1;
  }
  janus_system_clear_error();
  return (intptr_t)written;
}

int32_t janus_system_close(intptr_t handle) {
  if (handle == -1 || !CloseHandle((HANDLE)handle)) {
    if (handle == -1)
      janus_system_set_error(ERROR_INVALID_HANDLE, JANUS_SYSTEM_INVALID_INPUT);
    else
      janus_system_capture_windows_error();
    return -1;
  }
  janus_system_clear_error();
  return 0;
}

int32_t janus_system_remove(const char *path, uint64_t length) {
  wchar_t *wide = janus_system_windows_path(path, length);
  if (wide == NULL)
    return -1;
  const BOOL removed = DeleteFileW(wide);
  free(wide);
  if (!removed) {
    janus_system_capture_windows_error();
    return -1;
  }
  janus_system_clear_error();
  return 0;
}

#else

static int32_t janus_system_posix_category(int code) {
  switch (code) {
  case ENOENT:
  case ENOTDIR:
    return JANUS_SYSTEM_NOT_FOUND;
  case EACCES:
  case EPERM:
    return JANUS_SYSTEM_PERMISSION_DENIED;
  case EEXIST:
    return JANUS_SYSTEM_ALREADY_EXISTS;
  case EINVAL:
  case EBADF:
  case EILSEQ:
    return JANUS_SYSTEM_INVALID_INPUT;
  case EINTR:
    return JANUS_SYSTEM_INTERRUPTED;
  case EAGAIN:
#if EWOULDBLOCK != EAGAIN
  case EWOULDBLOCK:
#endif
    return JANUS_SYSTEM_WOULD_BLOCK;
  case ENOSPC:
  case EDQUOT:
    return JANUS_SYSTEM_OUT_OF_SPACE;
  case EFBIG:
  case EOVERFLOW:
  case ENAMETOOLONG:
    return JANUS_SYSTEM_TOO_LARGE;
  case ENOSYS:
  case ENOTSUP:
#if EOPNOTSUPP != ENOTSUP
  case EOPNOTSUPP:
#endif
    return JANUS_SYSTEM_UNSUPPORTED;
  case EMFILE:
  case ENFILE:
  case ENOMEM:
    return JANUS_SYSTEM_RESOURCE_EXHAUSTED;
  default:
    return JANUS_SYSTEM_OTHER;
  }
}

static void janus_system_capture_posix_error(void) {
  const int code = errno;
  janus_system_set_error((uint32_t)code, janus_system_posix_category(code));
}

static char *janus_system_posix_path(const char *path, uint64_t length) {
  if (path == NULL || length == 0 || length > SIZE_MAX - 1 ||
      !janus_system_valid_utf8((const unsigned char *)path, length)) {
    janus_system_set_error((uint32_t)EINVAL, JANUS_SYSTEM_INVALID_INPUT);
    return NULL;
  }
  char *copy = (char *)malloc((size_t)length + 1);
  if (copy == NULL) {
    janus_system_set_error((uint32_t)ENOMEM,
                           JANUS_SYSTEM_RESOURCE_EXHAUSTED);
    return NULL;
  }
  memcpy(copy, path, (size_t)length);
  copy[length] = '\0';
  return copy;
}

intptr_t janus_system_open(const char *path, uint64_t length, int32_t mode) {
  if (mode < 0 || mode > 2) {
    janus_system_set_error((uint32_t)EINVAL, JANUS_SYSTEM_INVALID_INPUT);
    return -1;
  }
  char *copy = janus_system_posix_path(path, length);
  if (copy == NULL)
    return -1;
  int flags = O_RDONLY;
  if (mode == 1)
    flags = O_WRONLY | O_CREAT | O_TRUNC;
  else if (mode == 2)
    flags = O_WRONLY | O_CREAT | O_APPEND;
  const int handle = open(copy, flags, 0666);
  free(copy);
  if (handle < 0) {
    janus_system_capture_posix_error();
    return -1;
  }
  janus_system_clear_error();
  return (intptr_t)handle;
}

intptr_t janus_system_read(intptr_t handle, void *data, uint64_t capacity) {
  if (handle < 0 || handle > INT_MAX || (data == NULL && capacity != 0)) {
    janus_system_set_error((uint32_t)EBADF, JANUS_SYSTEM_INVALID_INPUT);
    return -1;
  }
  const size_t requested =
      capacity > (uint64_t)SSIZE_MAX ? (size_t)SSIZE_MAX : (size_t)capacity;
  const ssize_t count = read((int)handle, data, requested);
  if (count < 0) {
    janus_system_capture_posix_error();
    return -1;
  }
  janus_system_clear_error();
  return (intptr_t)count;
}

intptr_t janus_system_write(intptr_t handle, const void *data, uint64_t size) {
  if (handle < 0 || handle > INT_MAX || (data == NULL && size != 0)) {
    janus_system_set_error((uint32_t)EBADF, JANUS_SYSTEM_INVALID_INPUT);
    return -1;
  }
  const size_t requested =
      size > (uint64_t)SSIZE_MAX ? (size_t)SSIZE_MAX : (size_t)size;
  const ssize_t count = write((int)handle, data, requested);
  if (count < 0) {
    janus_system_capture_posix_error();
    return -1;
  }
  janus_system_clear_error();
  return (intptr_t)count;
}

int32_t janus_system_close(intptr_t handle) {
  if (handle < 0 || handle > INT_MAX || close((int)handle) != 0) {
    if (handle < 0 || handle > INT_MAX)
      janus_system_set_error((uint32_t)EBADF, JANUS_SYSTEM_INVALID_INPUT);
    else
      janus_system_capture_posix_error();
    return -1;
  }
  janus_system_clear_error();
  return 0;
}

int32_t janus_system_remove(const char *path, uint64_t length) {
  char *copy = janus_system_posix_path(path, length);
  if (copy == NULL)
    return -1;
  const int result = unlink(copy);
  free(copy);
  if (result != 0) {
    janus_system_capture_posix_error();
    return -1;
  }
  janus_system_clear_error();
  return 0;
}

#endif
