#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

enum {
  JANUS_SYSTEM_INVALID_INPUT = 3,
  JANUS_SYSTEM_TOO_LARGE = 7,
  JANUS_SYSTEM_RESOURCE_EXHAUSTED = 9,
  JANUS_SYSTEM_OTHER = 10,
};

void janus_system_clear_error(void);
void janus_system_set_error(uint32_t native_code, int32_t portable_category);
uint32_t janus_system_error_code(void);
int32_t janus_system_error_category(void);
int janus_system_valid_utf8(const unsigned char *data, uint64_t size);
#if defined(_WIN32)
void janus_system_capture_windows_error(void);
wchar_t *janus_system_windows_path(const char *path, uint64_t length);
#else
void janus_system_capture_posix_error(void);
char *janus_system_posix_path(const char *path, uint64_t length);
#endif

typedef struct {
  char *data;
  uint64_t size;
  uint64_t capacity;
} janus_process_buffer;

typedef struct {
  char *data;
  uint64_t size;
} janus_process_text;

typedef struct {
  int32_t exit_code;
  janus_process_buffer standard_output;
  janus_process_buffer standard_error;
} janus_process_result;

uint32_t janus_process_last_error_code(void) {
  return janus_system_error_code();
}

int32_t janus_process_last_error_category(void) {
  return janus_system_error_category();
}

static int janus_process_buffer_append(janus_process_buffer *buffer,
                                       const void *data, size_t size) {
  if (size == 0)
    return 1;
  if ((uint64_t)size > UINT64_MAX - buffer->size) {
    janus_system_set_error(0, JANUS_SYSTEM_TOO_LARGE);
    return 0;
  }
  const uint64_t required = buffer->size + (uint64_t)size;
  if (required > buffer->capacity) {
    uint64_t next = buffer->capacity == 0 ? 4096 : buffer->capacity;
    while (next < required) {
      if (next > UINT64_MAX / 2) {
        next = required;
        break;
      }
      next *= 2;
    }
    if (next > SIZE_MAX) {
      janus_system_set_error(0, JANUS_SYSTEM_TOO_LARGE);
      return 0;
    }
    char *resized = (char *)realloc(buffer->data, (size_t)next);
    if (resized == NULL) {
      janus_system_set_error(0, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
      return 0;
    }
    buffer->data = resized;
    buffer->capacity = next;
  }
  memcpy(buffer->data + buffer->size, data, size);
  buffer->size = required;
  return 1;
}

static void janus_process_result_free(janus_process_result *result) {
  if (result == NULL)
    return;
  free(result->standard_output.data);
  free(result->standard_error.data);
  free(result);
}

static int janus_process_valid_text(const char *data, uint64_t size,
                                    int allow_empty) {
  return data != NULL && (allow_empty || size != 0) &&
         janus_system_valid_utf8((const unsigned char *)data, size);
}

static char **janus_process_arguments;
static uint64_t *janus_process_argument_lengths;
static uint64_t janus_process_arguments_count;

static void janus_process_clear_arguments(void) {
  if (janus_process_arguments != NULL) {
    for (uint64_t index = 0; index < janus_process_arguments_count; ++index)
      free(janus_process_arguments[index]);
  }
  free(janus_process_arguments);
  free(janus_process_argument_lengths);
  janus_process_arguments = NULL;
  janus_process_argument_lengths = NULL;
  janus_process_arguments_count = 0;
}

static int janus_process_store_argument(uint64_t index, const char *data,
                                        size_t size) {
  char *copy = (char *)malloc(size + 1);
  if (copy == NULL)
    return 0;
  memcpy(copy, data, size);
  copy[size] = '\0';
  janus_process_arguments[index] = copy;
  janus_process_argument_lengths[index] = (uint64_t)size;
  return 1;
}

#if defined(_WIN32)

typedef wchar_t **(WINAPI *janus_command_line_to_argv_w)(LPCWSTR, int *);

static char *janus_process_utf8_from_wide(const wchar_t *wide, int length,
                                          uint64_t *output_size) {
  const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide,
                                           length, NULL, 0, NULL, NULL);
  if (required < 0 || (required == 0 && length != 0)) {
    janus_system_capture_windows_error();
    return NULL;
  }
  char *result = (char *)malloc((size_t)required + 1);
  if (result == NULL) {
    janus_system_set_error(ERROR_OUTOFMEMORY, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
    return NULL;
  }
  if (required != 0 &&
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, length, result,
                          required, NULL, NULL) != required) {
    janus_system_capture_windows_error();
    free(result);
    return NULL;
  }
  result[required] = '\0';
  *output_size = (uint64_t)required;
  return result;
}

static wchar_t *janus_process_wide_text(const char *data, uint64_t size,
                                        int allow_empty) {
  if (!janus_process_valid_text(data, size, allow_empty) || size > INT_MAX) {
    janus_system_set_error(ERROR_INVALID_PARAMETER,
                           size > INT_MAX ? JANUS_SYSTEM_TOO_LARGE
                                          : JANUS_SYSTEM_INVALID_INPUT);
    return NULL;
  }
  const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data,
                                           (int)size, NULL, 0);
  if (required <= 0 && size != 0) {
    janus_system_capture_windows_error();
    return NULL;
  }
  wchar_t *wide = (wchar_t *)malloc(((size_t)required + 1) * sizeof(wchar_t));
  if (wide == NULL) {
    janus_system_set_error(ERROR_OUTOFMEMORY, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
    return NULL;
  }
  if (required != 0 &&
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, (int)size, wide,
                          required) != required) {
    janus_system_capture_windows_error();
    free(wide);
    return NULL;
  }
  wide[required] = L'\0';
  return wide;
}

void janus_process_initialize(int argc, char **argv) {
  (void)argc;
  (void)argv;
  janus_process_clear_arguments();
  HMODULE shell = LoadLibraryW(L"shell32.dll");
  if (shell == NULL)
    return;
  janus_command_line_to_argv_w split =
      (janus_command_line_to_argv_w)(void *)GetProcAddress(
          shell, "CommandLineToArgvW");
  if (split == NULL) {
    FreeLibrary(shell);
    return;
  }
  int count = 0;
  wchar_t **wide_arguments = split(GetCommandLineW(), &count);
  if (wide_arguments == NULL || count < 0) {
    FreeLibrary(shell);
    return;
  }
  janus_process_arguments = (char **)calloc((size_t)count, sizeof(char *));
  janus_process_argument_lengths =
      (uint64_t *)calloc((size_t)count, sizeof(uint64_t));
  if (janus_process_arguments == NULL ||
      janus_process_argument_lengths == NULL) {
    LocalFree(wide_arguments);
    FreeLibrary(shell);
    janus_process_clear_arguments();
    return;
  }
  janus_process_arguments_count = (uint64_t)count;
  for (int index = 0; index < count; ++index) {
    uint64_t size = 0;
    char *converted = janus_process_utf8_from_wide(
        wide_arguments[index], (int)wcslen(wide_arguments[index]), &size);
    if (converted == NULL) {
      janus_process_clear_arguments();
      break;
    }
    janus_process_arguments[index] = converted;
    janus_process_argument_lengths[index] = size;
  }
  LocalFree(wide_arguments);
  FreeLibrary(shell);
  janus_system_clear_error();
}

#else

static char *janus_process_posix_text(const char *data, uint64_t size,
                                      int allow_empty) {
  if (!janus_process_valid_text(data, size, allow_empty) ||
      size > SIZE_MAX - 1) {
    janus_system_set_error((uint32_t)EINVAL, size > SIZE_MAX - 1
                                                 ? JANUS_SYSTEM_TOO_LARGE
                                                 : JANUS_SYSTEM_INVALID_INPUT);
    return NULL;
  }
  char *copy = (char *)malloc((size_t)size + 1);
  if (copy == NULL) {
    janus_system_set_error((uint32_t)ENOMEM, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
    return NULL;
  }
  memcpy(copy, data, (size_t)size);
  copy[size] = '\0';
  return copy;
}

void janus_process_initialize(int argc, char **argv) {
  janus_process_clear_arguments();
  if (argc <= 0 || argv == NULL)
    return;
  janus_process_arguments = (char **)calloc((size_t)argc, sizeof(char *));
  janus_process_argument_lengths =
      (uint64_t *)calloc((size_t)argc, sizeof(uint64_t));
  if (janus_process_arguments == NULL ||
      janus_process_argument_lengths == NULL) {
    janus_process_clear_arguments();
    return;
  }
  janus_process_arguments_count = (uint64_t)argc;
  for (int index = 0; index < argc; ++index) {
    const size_t size = strlen(argv[index]);
    if (!janus_system_valid_utf8((const unsigned char *)argv[index],
                                 (uint64_t)size) ||
        !janus_process_store_argument((uint64_t)index, argv[index], size)) {
      janus_process_clear_arguments();
      return;
    }
  }
  janus_system_clear_error();
}

#endif

uint64_t janus_process_argument_count(void) {
  return janus_process_arguments_count;
}

const char *janus_process_argument_data(uint64_t index) {
  return index < janus_process_arguments_count ? janus_process_arguments[index]
                                               : NULL;
}

uint64_t janus_process_argument_size(uint64_t index) {
  return index < janus_process_arguments_count
             ? janus_process_argument_lengths[index]
             : 0;
}

intptr_t janus_process_environment(const char *name, uint64_t length) {
  if (!janus_process_valid_text(name, length, 0) ||
      memchr(name, '=', (size_t)length) != NULL) {
#if defined(_WIN32)
    janus_system_set_error(ERROR_INVALID_PARAMETER, JANUS_SYSTEM_INVALID_INPUT);
#else
    janus_system_set_error((uint32_t)EINVAL, JANUS_SYSTEM_INVALID_INPUT);
#endif
    return -1;
  }
  janus_process_text *text =
      (janus_process_text *)calloc(1, sizeof(janus_process_text));
  if (text == NULL) {
#if defined(_WIN32)
    janus_system_set_error(ERROR_OUTOFMEMORY, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
#else
    janus_system_set_error((uint32_t)ENOMEM, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
#endif
    return -1;
  }
#if defined(_WIN32)
  wchar_t *wide_name = janus_process_wide_text(name, length, 0);
  if (wide_name == NULL) {
    free(text);
    return -1;
  }
  SetLastError(ERROR_SUCCESS);
  DWORD required = GetEnvironmentVariableW(wide_name, NULL, 0);
  if (required == 0) {
    const DWORD code = GetLastError();
    free(wide_name);
    if (code == ERROR_ENVVAR_NOT_FOUND) {
      free(text);
      janus_system_clear_error();
      return 0;
    }
    if (code == ERROR_SUCCESS) {
      text->data = (char *)calloc(1, 1);
      if (text->data == NULL) {
        free(text);
        janus_system_set_error(ERROR_OUTOFMEMORY,
                               JANUS_SYSTEM_RESOURCE_EXHAUSTED);
        return -1;
      }
      janus_system_clear_error();
      return (intptr_t)text;
    }
    free(text);
    SetLastError(code);
    janus_system_capture_windows_error();
    return -1;
  }
  wchar_t *wide_value = (wchar_t *)malloc((size_t)required * sizeof(wchar_t));
  if (wide_value == NULL) {
    free(wide_name);
    free(text);
    janus_system_set_error(ERROR_OUTOFMEMORY, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
    return -1;
  }
  const DWORD written =
      GetEnvironmentVariableW(wide_name, wide_value, required);
  free(wide_name);
  if (written >= required) {
    free(wide_value);
    free(text);
    janus_system_set_error(ERROR_MORE_DATA, JANUS_SYSTEM_TOO_LARGE);
    return -1;
  }
  text->data =
      janus_process_utf8_from_wide(wide_value, (int)written, &text->size);
  free(wide_value);
  if (text->data == NULL) {
    free(text);
    return -1;
  }
#else
  char *key = janus_system_posix_path(name, length);
  if (key == NULL) {
    free(text);
    return -1;
  }
  const char *value = getenv(key);
  free(key);
  if (value == NULL) {
    free(text);
    janus_system_clear_error();
    return 0;
  }
  const size_t size = strlen(value);
  if (!janus_system_valid_utf8((const unsigned char *)value, (uint64_t)size)) {
    free(text);
    janus_system_set_error((uint32_t)EILSEQ, JANUS_SYSTEM_INVALID_INPUT);
    return -1;
  }
  text->data = (char *)malloc(size + 1);
  if (text->data == NULL) {
    free(text);
    janus_system_set_error((uint32_t)ENOMEM, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
    return -1;
  }
  memcpy(text->data, value, size + 1);
  text->size = (uint64_t)size;
#endif
  janus_system_clear_error();
  return (intptr_t)text;
}

const char *janus_process_text_data(intptr_t text) {
  return text > 0 ? ((janus_process_text *)text)->data : NULL;
}

uint64_t janus_process_text_size(intptr_t text) {
  return text > 0 ? ((janus_process_text *)text)->size : 0;
}

void janus_process_text_destroy(intptr_t text) {
  if (text <= 0)
    return;
  janus_process_text *value = (janus_process_text *)text;
  free(value->data);
  free(value);
}

#if !defined(_WIN32)

static void janus_process_close_fd(int *descriptor) {
  if (*descriptor >= 0) {
    (void)close(*descriptor);
    *descriptor = -1;
  }
}

static int janus_process_pipe(int descriptors[2]) {
  if (pipe(descriptors) != 0)
    return 0;
  if (fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) == -1 ||
      fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) == -1) {
    const int saved = errno;
    close(descriptors[0]);
    close(descriptors[1]);
    errno = saved;
    return 0;
  }
  return 1;
}

intptr_t janus_process_run(const char *executable, uint64_t executable_length,
                           const char *const *arguments,
                           const uint64_t *argument_lengths,
                           uint64_t argument_count,
                           const char *working_directory,
                           uint64_t working_directory_length,
                           int32_t capture_stdout, int32_t capture_stderr) {
  if (!janus_process_valid_text(executable, executable_length, 0) ||
      (argument_count != 0 &&
       (arguments == NULL || argument_lengths == NULL)) ||
      (working_directory_length != 0 &&
       !janus_process_valid_text(working_directory, working_directory_length,
                                 0)) ||
      argument_count > SIZE_MAX / sizeof(char *) - 2) {
    janus_system_set_error((uint32_t)EINVAL, JANUS_SYSTEM_INVALID_INPUT);
    return -1;
  }
  char *program = janus_system_posix_path(executable, executable_length);
  if (program == NULL)
    return -1;
  char *directory = NULL;
  if (working_directory_length != 0) {
    directory =
        janus_system_posix_path(working_directory, working_directory_length);
    if (directory == NULL) {
      free(program);
      return -1;
    }
  }
  char **argv = (char **)calloc((size_t)argument_count + 2, sizeof(char *));
  if (argv == NULL) {
    free(program);
    free(directory);
    janus_system_set_error((uint32_t)ENOMEM, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
    return -1;
  }
  argv[0] = program;
  for (uint64_t index = 0; index < argument_count; ++index) {
    argv[index + 1] =
        janus_process_posix_text(arguments[index], argument_lengths[index], 1);
    if (argv[index + 1] == NULL) {
      for (uint64_t cleanup = 1; cleanup <= index; ++cleanup)
        free(argv[cleanup]);
      free(argv);
      free(program);
      free(directory);
      return -1;
    }
  }

  int output_pipe[2] = {-1, -1};
  int error_pipe[2] = {-1, -1};
  int launch_pipe[2] = {-1, -1};
  if ((capture_stdout && !janus_process_pipe(output_pipe)) ||
      (capture_stderr && !janus_process_pipe(error_pipe)) ||
      !janus_process_pipe(launch_pipe)) {
    janus_system_capture_posix_error();
    goto fail_before_fork;
  }
  const pid_t child = fork();
  if (child < 0) {
    janus_system_capture_posix_error();
    goto fail_before_fork;
  }
  if (child == 0) {
    close(launch_pipe[0]);
    if (capture_stdout) {
      close(output_pipe[0]);
      if (dup2(output_pipe[1], STDOUT_FILENO) < 0)
        goto child_error;
    }
    if (capture_stderr) {
      close(error_pipe[0]);
      if (dup2(error_pipe[1], STDERR_FILENO) < 0)
        goto child_error;
    }
    janus_process_close_fd(&output_pipe[1]);
    janus_process_close_fd(&error_pipe[1]);
    if (directory != NULL && chdir(directory) != 0)
      goto child_error;
    execvp(program, argv);
  child_error: {
    const int child_errno = errno;
    ssize_t written;
    do {
      written = write(launch_pipe[1], &child_errno, sizeof(child_errno));
    } while (written < 0 && errno == EINTR);
    (void)written;
    _exit(127);
  }
  }

  janus_process_close_fd(&output_pipe[1]);
  janus_process_close_fd(&error_pipe[1]);
  janus_process_close_fd(&launch_pipe[1]);
  (void)fcntl(output_pipe[0], F_SETFL, O_NONBLOCK);
  (void)fcntl(error_pipe[0], F_SETFL, O_NONBLOCK);
  (void)fcntl(launch_pipe[0], F_SETFL, O_NONBLOCK);

  janus_process_result *result =
      (janus_process_result *)calloc(1, sizeof(janus_process_result));
  if (result == NULL) {
    (void)kill(child, SIGKILL);
    (void)waitpid(child, NULL, 0);
    janus_system_set_error((uint32_t)ENOMEM, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
    goto fail_after_fork;
  }
  int status = 0;
  int child_finished = 0;
  int child_error_code = 0;
  int launch_open = 1;
  while (!child_finished || output_pipe[0] >= 0 || error_pipe[0] >= 0 ||
         launch_open) {
    struct pollfd poll_descriptors[3];
    int *sources[3] = {&output_pipe[0], &error_pipe[0], &launch_pipe[0]};
    int count = 0;
    for (int index = 0; index < 3; ++index) {
      if (*sources[index] >= 0) {
        poll_descriptors[count].fd = *sources[index];
        poll_descriptors[count].events = POLLIN | POLLHUP;
        poll_descriptors[count].revents = 0;
        ++count;
      }
    }
    if (count != 0 && poll(poll_descriptors, (nfds_t)count, 20) < 0 &&
        errno != EINTR) {
      janus_system_capture_posix_error();
      (void)kill(child, SIGKILL);
      (void)waitpid(child, NULL, 0);
      janus_process_result_free(result);
      goto fail_after_fork;
    }
    for (int index = 0; index < count; ++index) {
      if ((poll_descriptors[index].revents & (POLLERR | POLLNVAL)) != 0) {
        janus_system_set_error((uint32_t)EIO, JANUS_SYSTEM_OTHER);
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
        janus_process_result_free(result);
        goto fail_after_fork;
      }
      if ((poll_descriptors[index].revents & (POLLIN | POLLHUP)) == 0)
        continue;
      char chunk[4096];
      const ssize_t read_count =
          read(poll_descriptors[index].fd, chunk, sizeof(chunk));
      if (read_count > 0) {
        if (poll_descriptors[index].fd == launch_pipe[0]) {
          memcpy(&child_error_code, chunk,
                 (size_t)read_count < sizeof(child_error_code)
                     ? (size_t)read_count
                     : sizeof(child_error_code));
        } else {
          janus_process_buffer *target =
              poll_descriptors[index].fd == output_pipe[0]
                  ? &result->standard_output
                  : &result->standard_error;
          if (!janus_process_buffer_append(target, chunk, (size_t)read_count)) {
            (void)kill(child, SIGKILL);
            (void)waitpid(child, NULL, 0);
            janus_process_result_free(result);
            goto fail_after_fork;
          }
        }
      } else if (read_count == 0) {
        for (int source = 0; source < 3; ++source) {
          if (*sources[source] == poll_descriptors[index].fd) {
            janus_process_close_fd(sources[source]);
            if (source == 2)
              launch_open = 0;
          }
        }
      } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        janus_system_capture_posix_error();
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
        janus_process_result_free(result);
        goto fail_after_fork;
      }
    }
    if (!child_finished) {
      const pid_t waited = waitpid(child, &status, WNOHANG);
      if (waited == child)
        child_finished = 1;
      else if (waited < 0 && errno != EINTR) {
        janus_system_capture_posix_error();
        janus_process_result_free(result);
        goto fail_after_fork;
      }
    }
  }
  if (child_error_code != 0) {
    errno = child_error_code;
    janus_system_capture_posix_error();
    janus_process_result_free(result);
    goto fail_after_fork;
  }
  result->exit_code =
      WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  for (uint64_t index = 1; index <= argument_count; ++index)
    free(argv[index]);
  free(argv);
  free(program);
  free(directory);
  janus_system_clear_error();
  return (intptr_t)result;

fail_after_fork:
  janus_process_close_fd(&output_pipe[0]);
  janus_process_close_fd(&error_pipe[0]);
  janus_process_close_fd(&launch_pipe[0]);
fail_before_fork:
  janus_process_close_fd(&output_pipe[0]);
  janus_process_close_fd(&output_pipe[1]);
  janus_process_close_fd(&error_pipe[0]);
  janus_process_close_fd(&error_pipe[1]);
  janus_process_close_fd(&launch_pipe[0]);
  janus_process_close_fd(&launch_pipe[1]);
  for (uint64_t index = 1; index <= argument_count; ++index)
    free(argv[index]);
  free(argv);
  free(program);
  free(directory);
  return -1;
}

#else

static int janus_process_quote_argument(janus_process_buffer *command,
                                        const wchar_t *argument) {
  const wchar_t quote = L'"';
  if (!janus_process_buffer_append(command, &quote, sizeof(quote)))
    return 0;
  size_t slashes = 0;
  for (const wchar_t *cursor = argument;; ++cursor) {
    if (*cursor == L'\\') {
      ++slashes;
      continue;
    }
    if (*cursor == L'"' || *cursor == L'\0')
      slashes = slashes * 2 + (*cursor == L'"' ? 1 : 0);
    while (slashes-- != 0) {
      const wchar_t slash = L'\\';
      if (!janus_process_buffer_append(command, &slash, sizeof(slash)))
        return 0;
    }
    slashes = 0;
    if (*cursor == L'\0')
      break;
    if (!janus_process_buffer_append(command, cursor, sizeof(*cursor)))
      return 0;
  }
  return janus_process_buffer_append(command, &quote, sizeof(quote));
}

intptr_t janus_process_run(const char *executable, uint64_t executable_length,
                           const char *const *arguments,
                           const uint64_t *argument_lengths,
                           uint64_t argument_count,
                           const char *working_directory,
                           uint64_t working_directory_length,
                           int32_t capture_stdout, int32_t capture_stderr) {
  if (!janus_process_valid_text(executable, executable_length, 0) ||
      (argument_count != 0 &&
       (arguments == NULL || argument_lengths == NULL))) {
    janus_system_set_error(ERROR_INVALID_PARAMETER, JANUS_SYSTEM_INVALID_INPUT);
    return -1;
  }
  wchar_t *program = janus_process_wide_text(executable, executable_length, 0);
  wchar_t *directory =
      working_directory_length == 0
          ? NULL
          : janus_process_wide_text(working_directory, working_directory_length,
                                    0);
  if (program == NULL || (working_directory_length != 0 && directory == NULL)) {
    free(program);
    free(directory);
    return -1;
  }
  janus_process_buffer command = {0};
  if (!janus_process_quote_argument(&command, program))
    goto windows_fail;
  for (uint64_t index = 0; index < argument_count; ++index) {
    wchar_t *argument =
        janus_process_wide_text(arguments[index], argument_lengths[index], 1);
    const wchar_t space = L' ';
    if (argument == NULL ||
        !janus_process_buffer_append(&command, &space, sizeof(space)) ||
        !janus_process_quote_argument(&command, argument)) {
      free(argument);
      goto windows_fail;
    }
    free(argument);
  }
  const wchar_t terminator = L'\0';
  if (!janus_process_buffer_append(&command, &terminator, sizeof(terminator)))
    goto windows_fail;

  SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
  HANDLE output_read = NULL, output_write = NULL;
  HANDLE error_read = NULL, error_write = NULL;
  if ((capture_stdout &&
       (!CreatePipe(&output_read, &output_write, &security, 0) ||
        !SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0))) ||
      (capture_stderr &&
       (!CreatePipe(&error_read, &error_write, &security, 0) ||
        !SetHandleInformation(error_read, HANDLE_FLAG_INHERIT, 0)))) {
    janus_system_capture_windows_error();
    goto windows_handles;
  }
  STARTUPINFOW startup = {0};
  startup.cb = sizeof(startup);
  if (capture_stdout || capture_stderr) {
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput =
        capture_stdout ? output_write : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError =
        capture_stderr ? error_write : GetStdHandle(STD_ERROR_HANDLE);
  }
  PROCESS_INFORMATION process = {0};
  if (!CreateProcessW(program, (wchar_t *)command.data, NULL, NULL, TRUE, 0,
                      NULL, directory, &startup, &process)) {
    janus_system_capture_windows_error();
    goto windows_handles;
  }
  if (output_write != NULL) {
    CloseHandle(output_write);
    output_write = NULL;
  }
  if (error_write != NULL) {
    CloseHandle(error_write);
    error_write = NULL;
  }
  janus_process_result *result =
      (janus_process_result *)calloc(1, sizeof(janus_process_result));
  if (result == NULL) {
    janus_system_set_error(ERROR_OUTOFMEMORY, JANUS_SYSTEM_RESOURCE_EXHAUSTED);
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    goto windows_handles;
  }
  int finished = 0;
  while (!finished || output_read != NULL || error_read != NULL) {
    HANDLE handles[2] = {output_read, error_read};
    for (int index = 0; index < 2; ++index) {
      if (handles[index] == NULL)
        continue;
      DWORD available = 0;
      if (!PeekNamedPipe(handles[index], NULL, 0, NULL, &available, NULL)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
          CloseHandle(handles[index]);
          if (index == 0)
            output_read = NULL;
          else
            error_read = NULL;
          continue;
        }
        janus_system_capture_windows_error();
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        janus_process_result_free(result);
        goto windows_handles;
      }
      if (available != 0) {
        char chunk[4096];
        DWORD read_count = 0;
        if (!ReadFile(handles[index], chunk,
                      available < sizeof(chunk) ? available : sizeof(chunk),
                      &read_count, NULL)) {
          janus_system_capture_windows_error();
          TerminateProcess(process.hProcess, 1);
          WaitForSingleObject(process.hProcess, INFINITE);
          CloseHandle(process.hThread);
          CloseHandle(process.hProcess);
          janus_process_result_free(result);
          goto windows_handles;
        }
        if (!janus_process_buffer_append(index == 0 ? &result->standard_output
                                                    : &result->standard_error,
                                         chunk, read_count)) {
          TerminateProcess(process.hProcess, 1);
          WaitForSingleObject(process.hProcess, INFINITE);
          CloseHandle(process.hThread);
          CloseHandle(process.hProcess);
          janus_process_result_free(result);
          goto windows_handles;
        }
      }
    }
    if (!finished)
      finished = WaitForSingleObject(process.hProcess, 5) == WAIT_OBJECT_0;
    if (finished && output_read == NULL && error_read == NULL)
      break;
  }
  DWORD exit_code = 0;
  if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
    janus_system_capture_windows_error();
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    janus_process_result_free(result);
    goto windows_handles;
  }
  result->exit_code = (int32_t)exit_code;
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  free(command.data);
  free(program);
  free(directory);
  janus_system_clear_error();
  return (intptr_t)result;

windows_handles:
  if (output_read != NULL)
    CloseHandle(output_read);
  if (output_write != NULL)
    CloseHandle(output_write);
  if (error_read != NULL)
    CloseHandle(error_read);
  if (error_write != NULL)
    CloseHandle(error_write);
windows_fail:
  free(command.data);
  free(program);
  free(directory);
  return -1;
}

#endif

int32_t janus_process_result_exit_code(intptr_t result) {
  return result > 0 ? ((janus_process_result *)result)->exit_code : -1;
}

const char *janus_process_result_stdout_data(intptr_t result) {
  return result > 0 ? ((janus_process_result *)result)->standard_output.data
                    : NULL;
}

uint64_t janus_process_result_stdout_size(intptr_t result) {
  return result > 0 ? ((janus_process_result *)result)->standard_output.size
                    : 0;
}

const char *janus_process_result_stderr_data(intptr_t result) {
  return result > 0 ? ((janus_process_result *)result)->standard_error.data
                    : NULL;
}

uint64_t janus_process_result_stderr_size(intptr_t result) {
  return result > 0 ? ((janus_process_result *)result)->standard_error.size : 0;
}

void janus_process_result_destroy(intptr_t result) {
  if (result > 0)
    janus_process_result_free((janus_process_result *)result);
}
