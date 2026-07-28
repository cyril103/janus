#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
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

enum {
  JANUS_FS_FILE = 0,
  JANUS_FS_DIRECTORY = 1,
  JANUS_FS_SYMBOLIC_LINK = 2,
  JANUS_FS_OTHER = 3,
};

void janus_system_clear_error(void);
void janus_system_set_error(uint32_t native_code, int32_t portable_category);
int janus_system_valid_utf8(const unsigned char *data, uint64_t size);
uint32_t janus_system_error_code(void);
int32_t janus_system_error_category(void);

#if defined(_WIN32)
void janus_system_capture_windows_error(void);
wchar_t *janus_system_windows_path(const char *path, uint64_t length);
#else
void janus_system_capture_posix_error(void);
char *janus_system_posix_path(const char *path, uint64_t length);
#endif

void janus_fs_free(void *data) { free(data); }
void janus_path_free(void *data) { free(data); }
void janus_file_free(void *data) { free(data); }

uint32_t janus_path_last_error_code(void) { return janus_system_error_code(); }

int32_t janus_path_last_error_category(void) {
  return janus_system_error_category();
}

uint32_t janus_fs_last_error_code(void) { return janus_system_error_code(); }

int32_t janus_fs_last_error_category(void) {
  return janus_system_error_category();
}

static int janus_path_separator(unsigned char value) {
#if defined(_WIN32)
  return value == '/' || value == '\\';
#else
  return value == '/';
#endif
}

static unsigned char janus_path_native_separator(void) {
#if defined(_WIN32)
  return '\\';
#else
  return '/';
#endif
}

static void janus_fs_invalid_input(void) {
#if defined(_WIN32)
  janus_system_set_error(ERROR_INVALID_NAME, JANUS_SYSTEM_INVALID_INPUT);
#else
  janus_system_set_error((uint32_t)EINVAL, JANUS_SYSTEM_INVALID_INPUT);
#endif
}

static void janus_fs_out_of_memory(void) {
#if defined(_WIN32)
  janus_system_set_error(ERROR_OUTOFMEMORY, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
#else
  janus_system_set_error((uint32_t)ENOMEM, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
#endif
}

static int janus_path_valid(const char *path, uint64_t length) {
  if (path == NULL || length == 0 || length > SIZE_MAX ||
      !janus_system_valid_utf8((const unsigned char *)path, length)) {
    janus_fs_invalid_input();
    return 0;
  }
  return 1;
}

typedef struct {
  uint64_t start;
  uint64_t length;
} JanusPathPart;

static int janus_path_is_dot(const char *path, JanusPathPart part) {
  return part.length == 1 && path[part.start] == '.';
}

static int janus_path_is_dot_dot(const char *path, JanusPathPart part) {
  return part.length == 2 && path[part.start] == '.' &&
         path[part.start + 1] == '.';
}

int32_t janus_path_is_absolute(const char *path, uint64_t length) {
  if (!janus_path_valid(path, length))
    return -1;
#if defined(_WIN32)
  if (length >= 3 &&
      ((path[0] >= 'A' && path[0] <= 'Z') ||
       (path[0] >= 'a' && path[0] <= 'z')) &&
      path[1] == ':' && janus_path_separator((unsigned char)path[2]))
    return 1;
  if (length >= 2 && janus_path_separator((unsigned char)path[0]) &&
      janus_path_separator((unsigned char)path[1]))
    return 1;
  return 0;
#else
  return path[0] == '/' ? 1 : 0;
#endif
}

int32_t janus_path_normalize(const char *path, uint64_t length, char **output,
                             uint64_t *output_length) {
  if (output == NULL || output_length == NULL ||
      !janus_path_valid(path, length)) {
    if (output == NULL || output_length == NULL)
      janus_fs_invalid_input();
    return -1;
  }
  *output = NULL;
  *output_length = 0;

  uint64_t cursor = 0;
  uint64_t prefix_length = 0;
  uint64_t protected_parts = 0;
  int absolute = 0;
#if defined(_WIN32)
  int unc = 0;
#endif
#if defined(_WIN32)
  if (length >= 2 &&
      ((path[0] >= 'A' && path[0] <= 'Z') ||
       (path[0] >= 'a' && path[0] <= 'z')) &&
      path[1] == ':') {
    prefix_length = 2;
    cursor = 2;
    if (cursor < length && janus_path_separator((unsigned char)path[cursor])) {
      absolute = 1;
      while (cursor < length &&
             janus_path_separator((unsigned char)path[cursor]))
        ++cursor;
    }
  } else if (length >= 2 && janus_path_separator((unsigned char)path[0]) &&
             janus_path_separator((unsigned char)path[1])) {
    prefix_length = 2;
    cursor = 2;
    absolute = 1;
    unc = 1;
    protected_parts = 2;
    while (cursor < length && janus_path_separator((unsigned char)path[cursor]))
      ++cursor;
  }
#else
  if (path[0] == '/') {
    absolute = 1;
    cursor = 1;
    while (cursor < length && path[cursor] == '/')
      ++cursor;
  }
#endif

  const uint64_t capacity = length / 2 + 2;
  if (capacity > SIZE_MAX / sizeof(JanusPathPart)) {
    janus_fs_out_of_memory();
    return -1;
  }
  JanusPathPart *parts =
      (JanusPathPart *)malloc((size_t)capacity * sizeof(JanusPathPart));
  if (parts == NULL) {
    janus_fs_out_of_memory();
    return -1;
  }
  uint64_t count = 0;
  while (cursor < length) {
    while (cursor < length && janus_path_separator((unsigned char)path[cursor]))
      ++cursor;
    const uint64_t start = cursor;
    while (cursor < length &&
           !janus_path_separator((unsigned char)path[cursor]))
      ++cursor;
    JanusPathPart part = {start, cursor - start};
    if (part.length == 0 || janus_path_is_dot(path, part))
      continue;
    if (janus_path_is_dot_dot(path, part)) {
      if (count > protected_parts &&
          !janus_path_is_dot_dot(path, parts[count - 1])) {
        --count;
        continue;
      }
      if (absolute)
        continue;
    }
    parts[count++] = part;
  }

  uint64_t result_length = prefix_length;
  if (absolute
#if defined(_WIN32)
      && !unc
#endif
  )
    ++result_length;
  if (count == 0 && !absolute && prefix_length == 0)
    result_length = 1;
  for (uint64_t index = 0; index < count; ++index) {
    if (result_length > UINT64_MAX - parts[index].length - 1) {
      free(parts);
      janus_fs_out_of_memory();
      return -1;
    }
    if (index > 0 || (result_length > 0 && !absolute))
      ++result_length;
    result_length += parts[index].length;
  }
  if (result_length > SIZE_MAX - 1) {
    free(parts);
    janus_fs_out_of_memory();
    return -1;
  }
  char *result = (char *)malloc((size_t)result_length + 1);
  if (result == NULL) {
    free(parts);
    janus_fs_out_of_memory();
    return -1;
  }
  uint64_t offset = 0;
  if (prefix_length != 0) {
#if defined(_WIN32)
    if (prefix_length == 2 && path[1] == ':') {
      result[offset++] = path[0];
      result[offset++] = ':';
    } else {
      result[offset++] = '\\';
      result[offset++] = '\\';
    }
#endif
  }
  if (absolute
#if defined(_WIN32)
      && !unc
#endif
  )
    result[offset++] = (char)janus_path_native_separator();
  if (count == 0 && !absolute && prefix_length == 0)
    result[offset++] = '.';
  for (uint64_t index = 0; index < count; ++index) {
    if (index > 0 ||
        (offset > 0 && !absolute && !(prefix_length == 2 && offset == 2)))
      result[offset++] = (char)janus_path_native_separator();
    memcpy(result + offset, path + parts[index].start,
           (size_t)parts[index].length);
    offset += parts[index].length;
  }
  result[offset] = '\0';
  free(parts);
  *output = result;
  *output_length = offset;
  janus_system_clear_error();
  return 0;
}

int32_t janus_path_join(const char *base, uint64_t base_length,
                        const char *child, uint64_t child_length, char **output,
                        uint64_t *output_length) {
  if (!janus_path_valid(base, base_length) ||
      !janus_path_valid(child, child_length) || output == NULL ||
      output_length == NULL) {
    if (output == NULL || output_length == NULL)
      janus_fs_invalid_input();
    return -1;
  }
  const int32_t child_absolute = janus_path_is_absolute(child, child_length);
  if (child_absolute < 0)
    return -1;
  if (child_absolute)
    return janus_path_normalize(child, child_length, output, output_length);
  if (base_length > SIZE_MAX - child_length - 1) {
    janus_fs_out_of_memory();
    return -1;
  }
  const size_t combined_length = (size_t)base_length + 1 + (size_t)child_length;
  char *combined = (char *)malloc(combined_length);
  if (combined == NULL) {
    janus_fs_out_of_memory();
    return -1;
  }
  memcpy(combined, base, (size_t)base_length);
  combined[base_length] = (char)janus_path_native_separator();
  memcpy(combined + base_length + 1, child, (size_t)child_length);
  const int32_t status = janus_path_normalize(
      combined, (uint64_t)combined_length, output, output_length);
  free(combined);
  return status;
}

static int janus_path_normalized_part(const char *path, uint64_t length,
                                      uint32_t wanted, char **output,
                                      uint64_t *output_length, int count_only) {
  char *normalized = NULL;
  uint64_t normalized_length = 0;
  if (janus_path_normalize(path, length, &normalized, &normalized_length) != 0)
    return -1;
  uint64_t cursor = 0;
#if defined(_WIN32)
  if (normalized_length >= 2 && normalized[1] == ':')
    cursor = 2;
  else if (normalized_length >= 2 && normalized[0] == '\\' &&
           normalized[1] == '\\')
    cursor = 2;
#endif
  while (cursor < normalized_length &&
         janus_path_separator((unsigned char)normalized[cursor]))
    ++cursor;
  int count = 0;
  while (cursor < normalized_length) {
    const uint64_t start = cursor;
    while (cursor < normalized_length &&
           !janus_path_separator((unsigned char)normalized[cursor]))
      ++cursor;
    const uint64_t part_length = cursor - start;
    if (part_length != 0 && !(part_length == 1 && normalized[start] == '.')) {
      if (!count_only && (uint32_t)count == wanted) {
        char *part = (char *)malloc((size_t)part_length + 1);
        if (part == NULL) {
          free(normalized);
          janus_fs_out_of_memory();
          return -1;
        }
        memcpy(part, normalized + start, (size_t)part_length);
        part[part_length] = '\0';
        *output = part;
        *output_length = part_length;
        free(normalized);
        janus_system_clear_error();
        return 0;
      }
      ++count;
    }
    while (cursor < normalized_length &&
           janus_path_separator((unsigned char)normalized[cursor]))
      ++cursor;
  }
  free(normalized);
  if (!count_only) {
    janus_fs_invalid_input();
    return -1;
  }
  return count;
}

int32_t janus_path_component_count(const char *path, uint64_t length) {
  return janus_path_normalized_part(path, length, 0, NULL, NULL, 1);
}

int32_t janus_path_component(const char *path, uint64_t length, uint32_t index,
                             char **output, uint64_t *output_length) {
  if (output == NULL || output_length == NULL) {
    janus_fs_invalid_input();
    return -1;
  }
  *output = NULL;
  *output_length = 0;
  return janus_path_normalized_part(path, length, index, output, output_length,
                                    0);
}

#if defined(_WIN32)

typedef struct {
  HANDLE handle;
  WIN32_FIND_DATAW data;
  int pending;
} JanusWindowsDirectory;

static int janus_fs_utf8_from_wide(const wchar_t *wide, char **output,
                                   uint64_t *output_length) {
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide,
                                         -1, NULL, 0, NULL, NULL);
  if (length <= 0) {
    janus_system_capture_windows_error();
    return -1;
  }
  char *data = (char *)malloc((size_t)length);
  if (data == NULL) {
    janus_fs_out_of_memory();
    return -1;
  }
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, data, length,
                          NULL, NULL) != length) {
    free(data);
    janus_system_capture_windows_error();
    return -1;
  }
  *output = data;
  *output_length = (uint64_t)length - 1;
  janus_system_clear_error();
  return 0;
}

int32_t janus_fs_read_file(const char *path, uint64_t length, void **output,
                           uint64_t *output_length) {
  if (output == NULL || output_length == NULL) {
    janus_fs_invalid_input();
    return -1;
  }
  *output = NULL;
  *output_length = 0;
  wchar_t *wide = janus_system_windows_path(path, length);
  if (wide == NULL)
    return -1;
  HANDLE file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  free(wide);
  if (file == INVALID_HANDLE_VALUE) {
    janus_system_capture_windows_error();
    return -1;
  }
  LARGE_INTEGER native_size;
  if (!GetFileSizeEx(file, &native_size)) {
    janus_system_capture_windows_error();
    CloseHandle(file);
    return -1;
  }
  if (native_size.QuadPart < 0 || (uint64_t)native_size.QuadPart > SIZE_MAX) {
    janus_system_set_error(223u, JANUS_SYSTEM_TOO_LARGE);
    CloseHandle(file);
    return -1;
  }
  const size_t size = (size_t)native_size.QuadPart;
  unsigned char *data = (unsigned char *)malloc(size == 0 ? 1 : size);
  if (data == NULL) {
    CloseHandle(file);
    janus_fs_out_of_memory();
    return -1;
  }
  size_t offset = 0;
  while (offset < size) {
    const DWORD request =
        size - offset > UINT32_MAX ? UINT32_MAX : (DWORD)(size - offset);
    DWORD count = 0;
    if (!ReadFile(file, data + offset, request, &count, NULL)) {
      free(data);
      janus_system_capture_windows_error();
      CloseHandle(file);
      return -1;
    }
    if (count == 0)
      break;
    offset += count;
  }
  if (!CloseHandle(file)) {
    free(data);
    janus_system_capture_windows_error();
    return -1;
  }
  *output = data;
  *output_length = (uint64_t)offset;
  janus_system_clear_error();
  return 0;
}

static _Thread_local uint32_t janus_fs_temp_counter;

int32_t janus_fs_write_file_atomic(const char *path, uint64_t length,
                                   const void *data, uint64_t size) {
  if ((data == NULL && size != 0) || size > SIZE_MAX) {
    janus_fs_invalid_input();
    return -1;
  }
  wchar_t *target = janus_system_windows_path(path, length);
  if (target == NULL)
    return -1;
  const size_t target_length = wcslen(target);
  if (target_length > SIZE_MAX - 64) {
    free(target);
    janus_fs_out_of_memory();
    return -1;
  }
  wchar_t *temporary =
      (wchar_t *)malloc((target_length + 64) * sizeof(wchar_t));
  if (temporary == NULL) {
    free(target);
    janus_fs_out_of_memory();
    return -1;
  }
  HANDLE file = INVALID_HANDLE_VALUE;
  for (unsigned attempt = 0; attempt < 128; ++attempt) {
    const uint32_t counter = ++janus_fs_temp_counter;
    (void)swprintf(temporary, target_length + 64, L"%ls.janus-tmp-%lu-%lu",
                   target, (unsigned long)GetCurrentProcessId(),
                   (unsigned long)counter);
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
      break;
    if (GetLastError() != ERROR_FILE_EXISTS &&
        GetLastError() != ERROR_ALREADY_EXISTS)
      break;
  }
  if (file == INVALID_HANDLE_VALUE) {
    free(temporary);
    free(target);
    janus_system_capture_windows_error();
    return -1;
  }
  uint64_t offset = 0;
  while (offset < size) {
    const DWORD request =
        size - offset > UINT32_MAX ? UINT32_MAX : (DWORD)(size - offset);
    DWORD count = 0;
    if (!WriteFile(file, (const unsigned char *)data + offset, request, &count,
                   NULL) ||
        count == 0) {
      janus_system_capture_windows_error();
      CloseHandle(file);
      DeleteFileW(temporary);
      free(temporary);
      free(target);
      return -1;
    }
    offset += count;
  }
  const BOOL flushed = FlushFileBuffers(file);
  const DWORD flush_error = flushed ? ERROR_SUCCESS : GetLastError();
  const BOOL closed = CloseHandle(file);
  const DWORD close_error = closed ? ERROR_SUCCESS : GetLastError();
  if (!flushed || !closed) {
    SetLastError(!flushed ? flush_error : close_error);
    janus_system_capture_windows_error();
    DeleteFileW(temporary);
    free(temporary);
    free(target);
    return -1;
  }
  if (!MoveFileExW(temporary, target,
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    janus_system_capture_windows_error();
    DeleteFileW(temporary);
    free(temporary);
    free(target);
    return -1;
  }
  free(temporary);
  free(target);
  janus_system_clear_error();
  return 0;
}

static int janus_fs_windows_directory_exists(const wchar_t *path) {
  const DWORD attributes = GetFileAttributesW(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

int32_t janus_fs_create_directory(const char *path, uint64_t length,
                                  int32_t recursive) {
  wchar_t *wide = janus_system_windows_path(path, length);
  if (wide == NULL)
    return -1;
  if (recursive) {
    const size_t wide_length = wcslen(wide);
    for (size_t index = 0; index <= wide_length; ++index) {
      if ((wide[index] == L'\\' || wide[index] == L'/' ||
           wide[index] == L'\0') &&
          index > 0 && !(index == 2 && wide[1] == L':')) {
        const wchar_t saved = wide[index];
        wide[index] = L'\0';
        if (!CreateDirectoryW(wide, NULL) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
          janus_system_capture_windows_error();
          free(wide);
          return -1;
        }
        if (!janus_fs_windows_directory_exists(wide)) {
          janus_system_set_error(ERROR_ALREADY_EXISTS,
                                 JANUS_SYSTEM_ALREADY_EXISTS);
          free(wide);
          return -1;
        }
        wide[index] = saved;
      }
    }
  } else if (!CreateDirectoryW(wide, NULL)) {
    if (GetLastError() != ERROR_ALREADY_EXISTS ||
        !janus_fs_windows_directory_exists(wide)) {
      janus_system_capture_windows_error();
      free(wide);
      return -1;
    }
  }
  free(wide);
  janus_system_clear_error();
  return 0;
}

int32_t janus_fs_create_temporary_directory(const char *prefix,
                                            uint64_t prefix_length,
                                            char **output,
                                            uint64_t *output_length) {
  if (output == NULL || output_length == NULL ||
      !janus_path_valid(prefix, prefix_length)) {
    if (output == NULL || output_length == NULL)
      janus_fs_invalid_input();
    return -1;
  }
  for (uint64_t index = 0; index < prefix_length; ++index) {
    if (janus_path_separator((unsigned char)prefix[index])) {
      janus_fs_invalid_input();
      return -1;
    }
  }
  wchar_t temporary_root[MAX_PATH + 1];
  const DWORD root_length = GetTempPathW(MAX_PATH + 1, temporary_root);
  if (root_length == 0 || root_length > MAX_PATH) {
    janus_system_capture_windows_error();
    return -1;
  }
  wchar_t *wide_prefix = janus_system_windows_path(prefix, prefix_length);
  if (wide_prefix == NULL)
    return -1;
  const size_t capacity = (size_t)root_length + wcslen(wide_prefix) + 64;
  wchar_t *candidate = (wchar_t *)malloc(capacity * sizeof(wchar_t));
  if (candidate == NULL) {
    free(wide_prefix);
    janus_fs_out_of_memory();
    return -1;
  }
  for (unsigned attempt = 0; attempt < 256; ++attempt) {
    const uint32_t counter = ++janus_fs_temp_counter;
    (void)swprintf(candidate, capacity, L"%ls%ls-%lu-%lu", temporary_root,
                   wide_prefix, (unsigned long)GetCurrentProcessId(),
                   (unsigned long)counter);
    if (CreateDirectoryW(candidate, NULL)) {
      const int status =
          janus_fs_utf8_from_wide(candidate, output, output_length);
      if (status != 0)
        RemoveDirectoryW(candidate);
      free(candidate);
      free(wide_prefix);
      return status;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS)
      break;
  }
  janus_system_capture_windows_error();
  free(candidate);
  free(wide_prefix);
  return -1;
}

int32_t janus_fs_remove_directory(const char *path, uint64_t length) {
  wchar_t *wide = janus_system_windows_path(path, length);
  if (wide == NULL)
    return -1;
  const BOOL removed = RemoveDirectoryW(wide);
  free(wide);
  if (!removed) {
    janus_system_capture_windows_error();
    return -1;
  }
  janus_system_clear_error();
  return 0;
}

int32_t janus_fs_metadata(const char *path, uint64_t length, int32_t *kind,
                          uint64_t *size) {
  if (kind == NULL || size == NULL) {
    janus_fs_invalid_input();
    return -1;
  }
  wchar_t *wide = janus_system_windows_path(path, length);
  if (wide == NULL)
    return -1;
  WIN32_FIND_DATAW data;
  HANDLE found = FindFirstFileW(wide, &data);
  free(wide);
  if (found == INVALID_HANDLE_VALUE) {
    janus_system_capture_windows_error();
    return -1;
  }
  FindClose(found);
  if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    *kind = JANUS_FS_SYMBOLIC_LINK;
  else if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    *kind = JANUS_FS_DIRECTORY;
  else
    *kind = JANUS_FS_FILE;
  *size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
  janus_system_clear_error();
  return 0;
}

intptr_t janus_fs_directory_open(const char *path, uint64_t length) {
  wchar_t *wide = janus_system_windows_path(path, length);
  if (wide == NULL)
    return -1;
  const size_t wide_length = wcslen(wide);
  wchar_t *pattern = (wchar_t *)malloc((wide_length + 3) * sizeof(wchar_t));
  if (pattern == NULL) {
    free(wide);
    janus_fs_out_of_memory();
    return -1;
  }
  wcscpy(pattern, wide);
  if (wide_length > 0 && wide[wide_length - 1] != L'\\' &&
      wide[wide_length - 1] != L'/')
    wcscat(pattern, L"\\");
  wcscat(pattern, L"*");
  free(wide);
  JanusWindowsDirectory *directory =
      (JanusWindowsDirectory *)malloc(sizeof(JanusWindowsDirectory));
  if (directory == NULL) {
    free(pattern);
    janus_fs_out_of_memory();
    return -1;
  }
  directory->handle = FindFirstFileW(pattern, &directory->data);
  free(pattern);
  if (directory->handle == INVALID_HANDLE_VALUE) {
    free(directory);
    janus_system_capture_windows_error();
    return -1;
  }
  directory->pending = 1;
  janus_system_clear_error();
  return (intptr_t)directory;
}

int32_t janus_fs_directory_next(intptr_t handle, char **output,
                                uint64_t *output_length) {
  if (handle == -1 || output == NULL || output_length == NULL) {
    janus_fs_invalid_input();
    return -1;
  }
  JanusWindowsDirectory *directory = (JanusWindowsDirectory *)handle;
  for (;;) {
    if (!directory->pending) {
      if (!FindNextFileW(directory->handle, &directory->data)) {
        if (GetLastError() == ERROR_NO_MORE_FILES) {
          janus_system_clear_error();
          return 0;
        }
        janus_system_capture_windows_error();
        return -1;
      }
    }
    directory->pending = 0;
    if (wcscmp(directory->data.cFileName, L".") == 0 ||
        wcscmp(directory->data.cFileName, L"..") == 0)
      continue;
    if (janus_fs_utf8_from_wide(directory->data.cFileName, output,
                                output_length) != 0)
      return -1;
    return 1;
  }
}

int32_t janus_fs_directory_close(intptr_t handle) {
  if (handle == -1) {
    janus_fs_invalid_input();
    return -1;
  }
  JanusWindowsDirectory *directory = (JanusWindowsDirectory *)handle;
  const BOOL closed = FindClose(directory->handle);
  free(directory);
  if (!closed) {
    janus_system_capture_windows_error();
    return -1;
  }
  janus_system_clear_error();
  return 0;
}

#else

int32_t janus_fs_read_file(const char *path, uint64_t length, void **output,
                           uint64_t *output_length) {
  if (output == NULL || output_length == NULL) {
    janus_fs_invalid_input();
    return -1;
  }
  *output = NULL;
  *output_length = 0;
  char *native = janus_system_posix_path(path, length);
  if (native == NULL)
    return -1;
  const int file = open(native, O_RDONLY);
  free(native);
  if (file < 0) {
    janus_system_capture_posix_error();
    return -1;
  }
  size_t capacity = 4096;
  unsigned char *data = (unsigned char *)malloc(capacity);
  if (data == NULL) {
    close(file);
    janus_fs_out_of_memory();
    return -1;
  }
  size_t used = 0;
  for (;;) {
    if (used == capacity) {
      if (capacity > SIZE_MAX / 2) {
        free(data);
        close(file);
        janus_system_set_error((uint32_t)EFBIG, JANUS_SYSTEM_TOO_LARGE);
        return -1;
      }
      capacity *= 2;
      unsigned char *resized = (unsigned char *)realloc(data, capacity);
      if (resized == NULL) {
        free(data);
        close(file);
        janus_fs_out_of_memory();
        return -1;
      }
      data = resized;
    }
    const ssize_t count = read(file, data + used, capacity - used);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      free(data);
      janus_system_capture_posix_error();
      close(file);
      return -1;
    }
    if (count == 0)
      break;
    used += (size_t)count;
  }
  if (close(file) != 0) {
    free(data);
    janus_system_capture_posix_error();
    return -1;
  }
  *output = data;
  *output_length = (uint64_t)used;
  janus_system_clear_error();
  return 0;
}

static _Thread_local uint32_t janus_fs_temp_counter;

int32_t janus_fs_write_file_atomic(const char *path, uint64_t length,
                                   const void *data, uint64_t size) {
  if ((data == NULL && size != 0) || size > SIZE_MAX) {
    janus_fs_invalid_input();
    return -1;
  }
  char *target = janus_system_posix_path(path, length);
  if (target == NULL)
    return -1;
  const size_t target_length = strlen(target);
  if (target_length > SIZE_MAX - 64) {
    free(target);
    janus_fs_out_of_memory();
    return -1;
  }
  char *temporary = (char *)malloc(target_length + 64);
  if (temporary == NULL) {
    free(target);
    janus_fs_out_of_memory();
    return -1;
  }
  int file = -1;
  for (unsigned attempt = 0; attempt < 128; ++attempt) {
    const uint32_t counter = ++janus_fs_temp_counter;
    (void)snprintf(temporary, target_length + 64, "%s.janus-tmp-%ld-%u", target,
                   (long)getpid(), counter);
    file = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (file >= 0)
      break;
    if (errno != EEXIST)
      break;
  }
  if (file < 0) {
    janus_system_capture_posix_error();
    free(temporary);
    free(target);
    return -1;
  }
  size_t offset = 0;
  while (offset < (size_t)size) {
    const ssize_t count = write(file, (const unsigned char *)data + offset,
                                (size_t)size - offset);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      janus_system_capture_posix_error();
      close(file);
      unlink(temporary);
      free(temporary);
      free(target);
      return -1;
    }
    if (count == 0) {
      janus_system_set_error((uint32_t)EIO, JANUS_SYSTEM_OTHER);
      close(file);
      unlink(temporary);
      free(temporary);
      free(target);
      return -1;
    }
    offset += (size_t)count;
  }
  const int synchronized = fsync(file);
  const int sync_error = synchronized == 0 ? 0 : errno;
  const int closed = close(file);
  const int close_error = closed == 0 ? 0 : errno;
  if (synchronized != 0 || closed != 0) {
    errno = synchronized != 0 ? sync_error : close_error;
    janus_system_capture_posix_error();
    unlink(temporary);
    free(temporary);
    free(target);
    return -1;
  }
  if (rename(temporary, target) != 0) {
    janus_system_capture_posix_error();
    unlink(temporary);
    free(temporary);
    free(target);
    return -1;
  }
  free(temporary);
  free(target);
  janus_system_clear_error();
  return 0;
}

static int janus_fs_posix_directory_exists(const char *path) {
  struct stat status;
  return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

int32_t janus_fs_create_directory(const char *path, uint64_t length,
                                  int32_t recursive) {
  char *native = janus_system_posix_path(path, length);
  if (native == NULL)
    return -1;
  if (recursive) {
    const size_t native_length = strlen(native);
    for (size_t index = 1; index <= native_length; ++index) {
      if (native[index] == '/' || native[index] == '\0') {
        const char saved = native[index];
        native[index] = '\0';
        if (mkdir(native, 0777) != 0 && errno != EEXIST) {
          janus_system_capture_posix_error();
          free(native);
          return -1;
        }
        if (!janus_fs_posix_directory_exists(native)) {
          janus_system_set_error((uint32_t)EEXIST, JANUS_SYSTEM_ALREADY_EXISTS);
          free(native);
          return -1;
        }
        native[index] = saved;
      }
    }
  } else if (mkdir(native, 0777) != 0 &&
             (errno != EEXIST || !janus_fs_posix_directory_exists(native))) {
    janus_system_capture_posix_error();
    free(native);
    return -1;
  }
  free(native);
  janus_system_clear_error();
  return 0;
}

int32_t janus_fs_create_temporary_directory(const char *prefix,
                                            uint64_t prefix_length,
                                            char **output,
                                            uint64_t *output_length) {
  if (output == NULL || output_length == NULL ||
      !janus_path_valid(prefix, prefix_length) || prefix_length > INT_MAX) {
    if (output == NULL || output_length == NULL)
      janus_fs_invalid_input();
    return -1;
  }
  for (uint64_t index = 0; index < prefix_length; ++index) {
    if (janus_path_separator((unsigned char)prefix[index])) {
      janus_fs_invalid_input();
      return -1;
    }
  }
  const char *root = getenv("TMPDIR");
  if (root == NULL || root[0] == '\0')
    root = "/tmp";
  const size_t root_length = strlen(root);
  if (root_length > SIZE_MAX - (size_t)prefix_length - 16) {
    janus_fs_out_of_memory();
    return -1;
  }
  const size_t capacity = root_length + (size_t)prefix_length + 16;
  char *candidate = (char *)malloc(capacity);
  if (candidate == NULL) {
    janus_fs_out_of_memory();
    return -1;
  }
  (void)snprintf(candidate, capacity, "%s/%.*s-XXXXXX", root,
                 (int)prefix_length, prefix);
  if (mkdtemp(candidate) == NULL) {
    janus_system_capture_posix_error();
    free(candidate);
    return -1;
  }
  const size_t candidate_length = strlen(candidate);
  if (!janus_system_valid_utf8((const unsigned char *)candidate,
                               candidate_length)) {
    rmdir(candidate);
    free(candidate);
    janus_fs_invalid_input();
    return -1;
  }
  *output = candidate;
  *output_length = (uint64_t)candidate_length;
  janus_system_clear_error();
  return 0;
}

int32_t janus_fs_remove_directory(const char *path, uint64_t length) {
  char *native = janus_system_posix_path(path, length);
  if (native == NULL)
    return -1;
  const int removed = rmdir(native);
  free(native);
  if (removed != 0) {
    janus_system_capture_posix_error();
    return -1;
  }
  janus_system_clear_error();
  return 0;
}

int32_t janus_fs_metadata(const char *path, uint64_t length, int32_t *kind,
                          uint64_t *size) {
  if (kind == NULL || size == NULL) {
    janus_fs_invalid_input();
    return -1;
  }
  char *native = janus_system_posix_path(path, length);
  if (native == NULL)
    return -1;
  struct stat status;
  const int result = lstat(native, &status);
  free(native);
  if (result != 0) {
    janus_system_capture_posix_error();
    return -1;
  }
  if (S_ISREG(status.st_mode))
    *kind = JANUS_FS_FILE;
  else if (S_ISDIR(status.st_mode))
    *kind = JANUS_FS_DIRECTORY;
  else if (S_ISLNK(status.st_mode))
    *kind = JANUS_FS_SYMBOLIC_LINK;
  else
    *kind = JANUS_FS_OTHER;
  *size = status.st_size < 0 ? 0 : (uint64_t)status.st_size;
  janus_system_clear_error();
  return 0;
}

intptr_t janus_fs_directory_open(const char *path, uint64_t length) {
  char *native = janus_system_posix_path(path, length);
  if (native == NULL)
    return -1;
  DIR *directory = opendir(native);
  free(native);
  if (directory == NULL) {
    janus_system_capture_posix_error();
    return -1;
  }
  janus_system_clear_error();
  return (intptr_t)directory;
}

int32_t janus_fs_directory_next(intptr_t handle, char **output,
                                uint64_t *output_length) {
  if (handle == -1 || output == NULL || output_length == NULL) {
    janus_fs_invalid_input();
    return -1;
  }
  DIR *directory = (DIR *)handle;
  for (;;) {
    errno = 0;
    struct dirent *entry = readdir(directory);
    if (entry == NULL) {
      if (errno != 0) {
        janus_system_capture_posix_error();
        return -1;
      }
      janus_system_clear_error();
      return 0;
    }
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    const size_t length = strlen(entry->d_name);
    if (!janus_system_valid_utf8((const unsigned char *)entry->d_name,
                                 length)) {
      janus_fs_invalid_input();
      return -1;
    }
    char *name = (char *)malloc(length + 1);
    if (name == NULL) {
      janus_fs_out_of_memory();
      return -1;
    }
    memcpy(name, entry->d_name, length + 1);
    *output = name;
    *output_length = (uint64_t)length;
    janus_system_clear_error();
    return 1;
  }
}

int32_t janus_fs_directory_close(intptr_t handle) {
  if (handle == -1) {
    janus_fs_invalid_input();
    return -1;
  }
  if (closedir((DIR *)handle) != 0) {
    janus_system_capture_posix_error();
    return -1;
  }
  janus_system_clear_error();
  return 0;
}

#endif
