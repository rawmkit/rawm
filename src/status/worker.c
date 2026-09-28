/* SPDX-License-Identifier: MIT */
#include "worker.h"
#include "source.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define RAWM_STATUS_IPC_SNAPSHOT 'S'
#define RAWM_STATUS_IPC_MAX (RAWM_STATUS_SNAPSHOT_MAX + 1u)

static volatile sig_atomic_t terminate_requested;

static void
handle_signal(int signum)
{
  (void)signum;
  terminate_requested = 1;
}

static int
install_signals(void)
{
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  sigemptyset(&action.sa_mask);
  action.sa_handler = handle_signal;
  if (sigaction(SIGINT, &action, NULL) < 0 ||
      sigaction(SIGTERM, &action, NULL) < 0 ||
      sigaction(SIGHUP, &action, NULL) < 0)
    return -1;
  action.sa_handler = SIG_DFL;
  if (sigaction(SIGCHLD, &action, NULL) < 0)
    return -1;
  return 0;
}

static uint64_t
monotonic_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    return 0;
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static RawmStatusSource *
create_sources(const RawmStatusConfig *config)
{
  RawmStatusSource *sources;
  size_t i;

  if (!config->source_count)
    return NULL;
  sources = calloc(config->source_count, sizeof(*sources));
  if (!sources)
    return NULL;
  for (i = 0; i < config->source_count; ++i)
    rawm_status_source_init(&sources[i], &config->sources[i]);
  return sources;
}

static void
destroy_sources(RawmStatusSource *sources, size_t count)
{
  size_t i;

  for (i = 0; i < count; ++i)
    rawm_status_source_destroy(&sources[i]);
  free(sources);
}

static RawmStatusResult
encode_snapshot(const RawmStatusSource *sources, size_t count, char **value,
                size_t *length)
{
  RawmStatusView *views;
  size_t i, n = 0;
  RawmStatusResult result;

  if (!count)
    return rawm_status_snapshot_encode(NULL, 0, value, length);
  views = calloc(count, sizeof(*views));
  if (!views)
    return RAWM_STATUS_NO_MEMORY;
  for (i = 0; i < count; ++i) {
    if (!sources[i].config->enabled || !sources[i].has_value)
      continue;
    views[n].id = sources[i].config->id;
    views[n].text = sources[i].text;
    views[n].state = sources[i].state;
    ++n;
  }
  result = rawm_status_snapshot_encode(views, n, value, length);
  free(views);
  return result;
}

static int
publish_socket(int fd, const char *value, size_t length, FILE *err)
{
  unsigned char frame[RAWM_STATUS_IPC_MAX];
  ssize_t n;

  if (length > RAWM_STATUS_SNAPSHOT_MAX)
    return -1;
  frame[0] = RAWM_STATUS_IPC_SNAPSHOT;
  if (length)
    memcpy(frame + 1, value, length);
  do {
    n = send(fd, frame, length + 1, MSG_NOSIGNAL);
  } while (n < 0 && errno == EINTR && !terminate_requested);
  if (n < 0) {
    if (errno != EPIPE && errno != ECONNRESET)
      fprintf(err, "rawm: status worker: send: %s\n", strerror(errno));
    return -1;
  }
  if ((size_t)n != length + 1) {
    fputs("rawm: status worker: short IPC write\n", err);
    return -1;
  }
  return 0;
}

static int
publish_stdout(FILE *out, const char *value, size_t length)
{
  if (length && fwrite(value, 1, length, out) != length)
    return -1;
  if (fputc('\n', out) == EOF || fflush(out) == EOF)
    return -1;
  return 0;
}

static int
publish_snapshot(const RawmStatusSource *sources, size_t count, int fd,
                 FILE *out, FILE *err)
{
  char *value = NULL;
  size_t length = 0;
  RawmStatusResult result;
  int rc;

  result = encode_snapshot(sources, count, &value, &length);
  if (result == RAWM_STATUS_NO_MEMORY) {
    fputs("rawm: status worker: out of memory\n", err);
    return -1;
  }
  if (result == RAWM_STATUS_TOO_LARGE) {
    fprintf(err, "rawm: status worker: snapshot exceeds %u bytes\n",
            RAWM_STATUS_SNAPSHOT_MAX);
    return 0;
  }
  if (result != RAWM_STATUS_OK) {
    fputs("rawm: status worker: invalid internal snapshot\n", err);
    return -1;
  }
  rc = out ? publish_stdout(out, value, length)
           : publish_socket(fd, value, length, err);
  free(value);
  return rc;
}

static int
worker_loop(const RawmStatusConfig *config, int fd, FILE *out, FILE *err)
{
  RawmStatusSource *sources = create_sources(config);
  bool publish = true;
  int rc = EXIT_SUCCESS;

  terminate_requested = 0;
  if (config->source_count && !sources) {
    fputs("rawm: status worker: out of memory\n", err);
    return EXIT_FAILURE;
  }
  if (install_signals() < 0) {
    fprintf(err, "rawm: status worker: sigaction: %s\n", strerror(errno));
    destroy_sources(sources, config->source_count);
    return EXIT_FAILURE;
  }

  while (!terminate_requested) {
    uint64_t now = monotonic_ms();
    uint64_t next = now + 1000u;
    size_t i;

    if (!now) {
      fputs("rawm: status worker: monotonic clock unavailable\n", err);
      rc = EXIT_FAILURE;
      break;
    }

    for (i = 0; i < config->source_count; ++i) {
      if (!config->sources[i].enabled)
        continue;
      if (!sources[i].next_due_ms || now >= sources[i].next_due_ms) {
        if (rawm_status_source_refresh(&sources[i], now,
                                       &terminate_requested, err) > 0)
          publish = true;
        if (terminate_requested)
          break;
      }
      if (sources[i].next_due_ms && sources[i].next_due_ms < next)
        next = sources[i].next_due_ms;
    }

    if (terminate_requested)
      break;

    if (publish) {
      if (publish_snapshot(sources, config->source_count, fd, out, err) < 0) {
        rc = EXIT_FAILURE;
        break;
      }
      publish = false;
    }

    if (!terminate_requested) {
      int timeout;
      struct pollfd pfd;
      int poll_result;

      now = monotonic_ms();
      timeout = next <= now ? 0 : (int)(next - now);
      if (timeout > 1000)
        timeout = 1000;
      if (out) {
        poll_result = poll(NULL, 0, timeout);
      } else {
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = fd;
        pfd.events = POLLIN | POLLHUP;
        poll_result = poll(&pfd, 1, timeout);
        if (poll_result > 0 &&
            (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)))
          break;
        if (poll_result > 0 && (pfd.revents & POLLIN)) {
          char byte;
          ssize_t n = recv(fd, &byte, 1, MSG_DONTWAIT);
          if (n == 0)
            break;
        }
      }
      if (poll_result < 0 && errno != EINTR) {
        fprintf(err, "rawm: status worker: poll: %s\n", strerror(errno));
        rc = EXIT_FAILURE;
        break;
      }
    }
  }

  destroy_sources(sources, config->source_count);
  return rc;
}

void
rawm_status_worker_init(RawmStatusWorker *worker)
{
  worker->fd = -1;
  worker->pid = -1;
}

int
rawm_status_worker_start(RawmStatusWorker *worker,
                         const RawmStatusConfig *config, FILE *err)
{
  int sockets[2];
  pid_t pid;

  if (worker->fd >= 0 || worker->pid > 0) {
    fputs("rawm: status worker already running\n", err);
    return -1;
  }
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) < 0) {
    fprintf(err, "rawm: status worker socketpair: %s\n", strerror(errno));
    return -1;
  }
  pid = fork();
  if (pid < 0) {
    fprintf(err, "rawm: status worker fork: %s\n", strerror(errno));
    close(sockets[0]);
    close(sockets[1]);
    return -1;
  }
  if (pid == 0) {
    long maxfd;
    int result;

    close(sockets[0]);
    maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 65536)
      maxfd = 65536;
    for (int fd = 3; fd < maxfd; ++fd)
      if (fd != sockets[1])
        close(fd);
    result = worker_loop(config, sockets[1], NULL, stderr);
    close(sockets[1]);
    _exit(result);
  }

  close(sockets[1]);
  worker->fd = sockets[0];
  worker->pid = pid;
  return 0;
}

void
rawm_status_worker_stop(RawmStatusWorker *worker, FILE *err)
{
  int status;

  if (worker->fd >= 0) {
    close(worker->fd);
    worker->fd = -1;
  }
  if (worker->pid <= 0)
    return;
  if (kill(worker->pid, SIGTERM) < 0 && errno != ESRCH)
    fprintf(err, "rawm: status worker kill: %s\n", strerror(errno));
  while (waitpid(worker->pid, &status, 0) < 0) {
    if (errno == EINTR)
      continue;
    if (errno != ECHILD)
      fprintf(err, "rawm: status worker waitpid: %s\n", strerror(errno));
    break;
  }
  worker->pid = -1;
}

int
rawm_status_worker_receive(RawmStatusWorker *worker, RawmStatusData *status,
                           FILE *err)
{
  unsigned char frame[RAWM_STATUS_IPC_MAX];
  ssize_t n;
  RawmStatusData parsed = {0};
  RawmStatusResult result;

  if (worker->fd < 0)
    return -1;
  n = recv(worker->fd, frame, sizeof(frame), MSG_DONTWAIT | MSG_TRUNC);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    fprintf(err, "rawm: status worker receive: %s\n", strerror(errno));
    return -1;
  }
  if (n == 0)
    return -1;
  if ((size_t)n > sizeof(frame) || frame[0] != RAWM_STATUS_IPC_SNAPSHOT) {
    fputs("rawm: status worker sent invalid IPC frame\n", err);
    return -1;
  }
  result = rawm_status_snapshot_parse(frame + 1, (size_t)n - 1, &parsed);
  if (result == RAWM_STATUS_NO_MEMORY) {
    fputs("rawm: out of memory while receiving status snapshot\n", err);
    return -1;
  }
  if (result != RAWM_STATUS_OK) {
    fputs("rawm: status worker sent invalid snapshot\n", err);
    return -1;
  }
  rawm_status_data_destroy(status);
  *status = parsed;
  return 1;
}

int
rawm_status_worker_debug(const RawmStatusConfig *config, FILE *out, FILE *err)
{
  return worker_loop(config, -1, out, err);
}
