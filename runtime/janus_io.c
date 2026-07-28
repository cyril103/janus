#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

enum {
  JANUS_SYSTEM_INVALID_INPUT = 3,
};

void janus_system_clear_error(void);
void janus_system_set_error(uint32_t native_code, int32_t portable_category);
uint32_t janus_system_error_code(void);
int32_t janus_system_error_category(void);
intptr_t janus_system_open(const char *path, uint64_t length, int32_t mode);
intptr_t janus_system_read(intptr_t handle, void *data, uint64_t capacity);
intptr_t janus_system_write(intptr_t handle, const void *data, uint64_t size);
int32_t janus_system_close(intptr_t handle);

#if defined(_WIN32)
void janus_system_capture_windows_error(void);
#else
void janus_system_capture_posix_error(void);
#endif

static void janus_io_invalid_input(void) {
#if defined(_WIN32)
  janus_system_set_error(ERROR_INVALID_HANDLE, JANUS_SYSTEM_INVALID_INPUT);
#else
  janus_system_set_error((uint32_t)EBADF, JANUS_SYSTEM_INVALID_INPUT);
#endif
}

uint32_t janus_io_last_error_code(void) { return janus_system_error_code(); }

int32_t janus_io_last_error_category(void) {
  return janus_system_error_category();
}

intptr_t janus_io_open(const char *path, uint64_t length, int32_t mode) {
  return janus_system_open(path, length, mode);
}

intptr_t janus_io_read(intptr_t handle, void *data, uint64_t capacity) {
  return janus_system_read(handle, data, capacity);
}

intptr_t janus_io_write(intptr_t handle, const void *data, uint64_t offset,
                        uint64_t size) {
  if (data == NULL && (offset != 0 || size != 0)) {
    janus_io_invalid_input();
    return -1;
  }
  const void *start = data;
  if (offset != 0)
    start = (const unsigned char *)data + offset;
  return janus_system_write(handle, start, size);
}

int32_t janus_io_close(intptr_t handle) { return janus_system_close(handle); }

intptr_t janus_io_standard_handle(int32_t stream) {
  if (stream < 0 || stream > 2) {
    janus_io_invalid_input();
    return -1;
  }
#if defined(_WIN32)
  if (stream == 1)
    (void)fflush(stdout);
  else if (stream == 2)
    (void)fflush(stderr);
  const DWORD identifiers[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE,
                               STD_ERROR_HANDLE};
  const HANDLE handle = GetStdHandle(identifiers[stream]);
  if (handle == NULL) {
    janus_io_invalid_input();
    return -1;
  }
  if (handle == INVALID_HANDLE_VALUE) {
    janus_system_capture_windows_error();
    return -1;
  }
  janus_system_clear_error();
  return (intptr_t)handle;
#else
  const int handles[] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
  struct stat status;
  if (fstat(handles[stream], &status) != 0) {
    janus_system_capture_posix_error();
    return -1;
  }
  if (stream == 1)
    (void)fflush(stdout);
  else if (stream == 2)
    (void)fflush(stderr);
  janus_system_clear_error();
  return (intptr_t)handles[stream];
#endif
}

int32_t janus_io_flush(intptr_t handle) {
#if defined(_WIN32)
  if (handle == -1) {
    janus_io_invalid_input();
    return -1;
  }
  SetLastError(ERROR_SUCCESS);
  const DWORD type = GetFileType((HANDLE)handle);
  if (type == FILE_TYPE_UNKNOWN && GetLastError() != ERROR_SUCCESS) {
    janus_system_capture_windows_error();
    return -1;
  }
  if (type == FILE_TYPE_DISK && !FlushFileBuffers((HANDLE)handle)) {
    janus_system_capture_windows_error();
    return -1;
  }
#else
  if (handle < 0 || handle > INT32_MAX) {
    janus_io_invalid_input();
    return -1;
  }
  struct stat status;
  if (fstat((int)handle, &status) != 0) {
    janus_system_capture_posix_error();
    return -1;
  }
  if (S_ISREG(status.st_mode)) {
    while (fsync((int)handle) != 0) {
      if (errno == EINTR)
        continue;
      janus_system_capture_posix_error();
      return -1;
    }
  }
#endif
  janus_system_clear_error();
  return 0;
}

int32_t janus_io_valid_utf8(const void *raw_data, uint64_t size) {
  if (raw_data == NULL)
    return size == 0 ? 1 : 0;
  const unsigned char *data = (const unsigned char *)raw_data;
  uint64_t index = 0;
  while (index < size) {
    const unsigned char first = data[index++];
    if (first <= 0x7f)
      continue;
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
        (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff)
      return 0;
  }
  return 1;
}
