#include "rawmsession-config.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef VERSION
#define VERSION "unknown"
#endif

#define SIDECAR_RESTART_DELAY_MS 250U
#define SIDECAR_RESTART_LIMIT 5U
#define SIDECAR_RESTART_WINDOW_MS 10000U
#define SHUTDOWN_GRACE_MS 2000U

typedef struct {
  const RawmSessionCommand *command;
  pid_t pid;
  bool disabled;
  bool restart_pending;
  unsigned int restart_count;
  uint64_t restart_window_start;
  uint64_t restart_at;
} ManagedSidecar;

static int signal_pipe[2] = { -1, -1 };
static volatile sig_atomic_t child_changed = 0;
static volatile sig_atomic_t terminate_signal = 0;
static volatile sig_atomic_t restart_requested = 0;

static void
usage(FILE *stream)
{
  fputs("usage: rawmsession [-C] [-c path] [-v]\n", stream);
}

static uint64_t
monotonic_msec(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
    perror("rawmsession: clock_gettime");
    exit(EXIT_FAILURE);
  }
  return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

static int
set_fd_flags(int fd)
{
  int flags;

  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    return -1;
  flags = fcntl(fd, F_GETFD, 0);
  if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
    return -1;
  return 0;
}

static void
signal_handler(int signum)
{
  unsigned char byte = 0;
  int saved_errno = errno;

  if (signum == SIGCHLD)
    child_changed = 1;
  else if (signum == SIGHUP)
    restart_requested = 1;
  else if (terminate_signal == 0)
    terminate_signal = signum;

  if (signal_pipe[1] >= 0)
    (void)write(signal_pipe[1], &byte, 1);
  errno = saved_errno;
}

static int
install_signal_handler(int signum)
{
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_handler = signal_handler;
  sigemptyset(&action.sa_mask);
  return sigaction(signum, &action, NULL);
}

static int
setup_signals(void)
{
  if (pipe(signal_pipe) < 0 ||
      set_fd_flags(signal_pipe[0]) < 0 ||
      set_fd_flags(signal_pipe[1]) < 0) {
    perror("rawmsession: signal pipe");
    return -1;
  }
  if (install_signal_handler(SIGCHLD) < 0 ||
      install_signal_handler(SIGINT) < 0 ||
      install_signal_handler(SIGTERM) < 0 ||
      install_signal_handler(SIGHUP) < 0) {
    perror("rawmsession: sigaction");
    return -1;
  }
  return 0;
}

static void
close_signal_pipe(void)
{
  if (signal_pipe[0] >= 0)
    close(signal_pipe[0]);
  if (signal_pipe[1] >= 0)
    close(signal_pipe[1]);
  signal_pipe[0] = signal_pipe[1] = -1;
}

static pid_t
spawn_command(const RawmSessionCommand *command)
{
  pid_t pid = fork();

  if (pid < 0)
    return -1;
  if (pid == 0) {
    close_signal_pipe();
    (void)setpgid(0, 0);
    execvp(command->argv[0], command->argv);
    fprintf(stderr, "rawmsession: cannot execute %s: %s\n",
            command->argv[0], strerror(errno));
    _exit(127);
  }
  if (setpgid(pid, pid) < 0 && errno != EACCES && errno != ESRCH)
    fprintf(stderr, "rawmsession: setpgid %ld: %s\n", (long)pid,
            strerror(errno));
  return pid;
}

static int
wait_status_code(int status)
{
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return EXIT_FAILURE;
}

static void
print_wait_status(const char *prefix, const char *name, int status)
{
  if (WIFEXITED(status))
    fprintf(stderr, "rawmsession: %s '%s' exited with status %d",
            prefix, name, WEXITSTATUS(status));
  else if (WIFSIGNALED(status))
    fprintf(stderr, "rawmsession: %s '%s' was terminated by signal %d",
            prefix, name, WTERMSIG(status));
  else
    fprintf(stderr, "rawmsession: %s '%s' changed state unexpectedly",
            prefix, name);
}

static void
schedule_sidecar_restart(ManagedSidecar *sidecar, int status, uint64_t now)
{
  if (sidecar->restart_window_start == 0 ||
      now - sidecar->restart_window_start > SIDECAR_RESTART_WINDOW_MS) {
    sidecar->restart_window_start = now;
    sidecar->restart_count = 0;
  }

  print_wait_status("sidecar", sidecar->command->name, status);
  if (sidecar->restart_count >= SIDECAR_RESTART_LIMIT) {
    fprintf(stderr,
            "; disabling after %u restarts within %u ms\n",
            SIDECAR_RESTART_LIMIT, SIDECAR_RESTART_WINDOW_MS);
    sidecar->disabled = true;
    sidecar->restart_pending = false;
    return;
  }

  ++sidecar->restart_count;
  sidecar->restart_pending = true;
  sidecar->restart_at = now + SIDECAR_RESTART_DELAY_MS;
  fprintf(stderr, "; restarting in %u ms\n", SIDECAR_RESTART_DELAY_MS);
}

static void
schedule_spawn_failure(ManagedSidecar *sidecar, uint64_t now)
{
  if (sidecar->restart_window_start == 0 ||
      now - sidecar->restart_window_start > SIDECAR_RESTART_WINDOW_MS) {
    sidecar->restart_window_start = now;
    sidecar->restart_count = 0;
  }

  fprintf(stderr, "rawmsession: cannot start sidecar '%s': %s",
          sidecar->command->name, strerror(errno));
  if (sidecar->restart_count >= SIDECAR_RESTART_LIMIT) {
    fprintf(stderr,
            "; disabling after %u restarts within %u ms\n",
            SIDECAR_RESTART_LIMIT, SIDECAR_RESTART_WINDOW_MS);
    sidecar->disabled = true;
    sidecar->restart_pending = false;
    return;
  }
  ++sidecar->restart_count;
  sidecar->restart_pending = true;
  sidecar->restart_at = now + SIDECAR_RESTART_DELAY_MS;
  fprintf(stderr, "; retrying in %u ms\n", SIDECAR_RESTART_DELAY_MS);
}

static void
start_sidecar(ManagedSidecar *sidecar, uint64_t now)
{
  pid_t pid;

  if (sidecar->disabled || sidecar->pid > 0)
    return;
  pid = spawn_command(sidecar->command);
  if (pid < 0) {
    schedule_spawn_failure(sidecar, now);
    return;
  }
  sidecar->pid = pid;
  sidecar->restart_pending = false;
}

static int
next_restart_timeout(const ManagedSidecar *sidecars, size_t count,
                     uint64_t now)
{
  uint64_t earliest = 0;
  size_t i;

  for (i = 0; i < count; ++i) {
    if (!sidecars[i].restart_pending || sidecars[i].disabled)
      continue;
    if (sidecars[i].restart_at <= now)
      return 0;
    if (earliest == 0 || sidecars[i].restart_at < earliest)
      earliest = sidecars[i].restart_at;
  }
  if (earliest == 0)
    return -1;
  if (earliest - now > 2147483647U)
    return 2147483647;
  return (int)(earliest - now);
}

static void
start_due_sidecars(ManagedSidecar *sidecars, size_t count, uint64_t now)
{
  size_t i;

  for (i = 0; i < count; ++i)
    if (sidecars[i].restart_pending && !sidecars[i].disabled &&
        sidecars[i].restart_at <= now)
      start_sidecar(&sidecars[i], now);
}

static ManagedSidecar *
find_sidecar(ManagedSidecar *sidecars, size_t count, pid_t pid)
{
  size_t i;

  for (i = 0; i < count; ++i)
    if (sidecars[i].pid == pid)
      return &sidecars[i];
  return NULL;
}

static void signal_child_group(pid_t pid, int signum);

static void
reap_children(pid_t *primary_pid, bool *primary_done, int *primary_status,
              ManagedSidecar *sidecars, size_t count, bool stopping)
{
  int status;
  pid_t pid;
  uint64_t now = monotonic_msec();

  for (;;) {
    siginfo_t info = { 0 };

    if (waitid(P_ALL, 0, &info, WEXITED | WNOHANG | WNOWAIT) < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    pid = info.si_pid;
    if (pid == 0)
      break;

    /* Keep the exited leader unreaped until its whole sidecar group is gone.
     * Otherwise its PID could be reused before the group is signaled. */
    if (find_sidecar(sidecars, count, pid))
      signal_child_group(pid, SIGKILL);
    if (waitpid(pid, &status, 0) < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (pid == *primary_pid) {
      *primary_pid = 0;
      *primary_done = true;
      *primary_status = status;
      continue;
    }
    {
      ManagedSidecar *sidecar = find_sidecar(sidecars, count, pid);

      if (!sidecar)
        continue;
      sidecar->pid = 0;
      if (!stopping)
        schedule_sidecar_restart(sidecar, status, now);
    }
  }
}

static void
signal_child_group(pid_t pid, int signum)
{
  if (pid <= 0)
    return;
  if (kill(-pid, signum) < 0 && errno != ESRCH)
    (void)kill(pid, signum);
}

static bool
children_running(pid_t primary_pid, const ManagedSidecar *sidecars,
                 size_t count)
{
  size_t i;

  if (primary_pid > 0)
    return true;
  for (i = 0; i < count; ++i)
    if (sidecars[i].pid > 0)
      return true;
  return false;
}

static void
shutdown_children(pid_t *primary_pid, ManagedSidecar *sidecars, size_t count)
{
  uint64_t deadline = monotonic_msec() + SHUTDOWN_GRACE_MS;
  bool ignored_primary_done = false;
  int ignored_status = 0;
  struct timespec pause = { 0, 20 * 1000 * 1000 };
  size_t i;

  signal_child_group(*primary_pid, SIGTERM);
  for (i = 0; i < count; ++i)
    signal_child_group(sidecars[i].pid, SIGTERM);

  while (children_running(*primary_pid, sidecars, count) &&
         monotonic_msec() < deadline) {
    reap_children(primary_pid, &ignored_primary_done, &ignored_status,
                  sidecars, count, true);
    if (children_running(*primary_pid, sidecars, count))
      (void)nanosleep(&pause, NULL);
  }

  signal_child_group(*primary_pid, SIGKILL);
  for (i = 0; i < count; ++i)
    signal_child_group(sidecars[i].pid, SIGKILL);

  while (children_running(*primary_pid, sidecars, count)) {
    int status;
    pid_t pid = waitpid(-1, &status, 0);
    ManagedSidecar *sidecar;

    if (pid < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (pid == *primary_pid) {
      *primary_pid = 0;
      continue;
    }
    sidecar = find_sidecar(sidecars, count, pid);
    if (sidecar)
      sidecar->pid = 0;
  }
}

static int
run_session(const RawmSessionConfig *config, bool *restart)
{
  ManagedSidecar *sidecars;
  pid_t primary_pid;
  bool primary_done = false;
  int primary_status = 0;
  size_t i;

  *restart = false;
  child_changed = 0;
  terminate_signal = 0;
  restart_requested = 0;

  sidecars = config->sidecar_count > 0
               ? calloc(config->sidecar_count, sizeof(*sidecars))
               : NULL;
  if (config->sidecar_count > 0 && !sidecars) {
    fputs("rawmsession: out of memory\n", stderr);
    return EXIT_FAILURE;
  }
  for (i = 0; i < config->sidecar_count; ++i)
    sidecars[i].command = &config->sidecars[i];

  primary_pid = spawn_command(&config->primary);
  if (primary_pid < 0) {
    fprintf(stderr, "rawmsession: cannot start primary '%s': %s\n",
            config->primary.argv[0], strerror(errno));
    free(sidecars);
    return EXIT_FAILURE;
  }

  for (i = 0; i < config->sidecar_count; ++i)
    start_sidecar(&sidecars[i], monotonic_msec());

  while (!primary_done && terminate_signal == 0 && !restart_requested) {
    struct pollfd pfd;
    int timeout;
    int rc;
    uint64_t now = monotonic_msec();

    start_due_sidecars(sidecars, config->sidecar_count, now);
    timeout = next_restart_timeout(sidecars, config->sidecar_count, now);
    pfd.fd = signal_pipe[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    rc = poll(&pfd, 1, timeout);
    if (rc < 0) {
      if (errno == EINTR)
        continue;
      perror("rawmsession: poll");
      terminate_signal = SIGTERM;
      break;
    }
    if (rc > 0 && (pfd.revents & POLLIN)) {
      unsigned char bytes[64];

      while (read(signal_pipe[0], bytes, sizeof(bytes)) > 0)
        ;
    }
    if (child_changed) {
      child_changed = 0;
      reap_children(&primary_pid, &primary_done, &primary_status,
                    sidecars, config->sidecar_count, false);
    }
  }

  shutdown_children(&primary_pid, sidecars, config->sidecar_count);
  free(sidecars);

  if (restart_requested) {
    *restart = true;
    return EXIT_SUCCESS;
  }
  if (terminate_signal)
    return 128 + terminate_signal;
  if (primary_done)
    return wait_status_code(primary_status);
  return EXIT_FAILURE;
}

int
main(int argc, char **argv)
{
  RawmSessionConfig config;
  const char *config_path = NULL;
  bool check_config = false;
  bool restart = false;
  int i;
  int rc = EXIT_FAILURE;

  for (i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config")) {
      if (++i >= argc) {
        usage(stderr);
        return EXIT_FAILURE;
      }
      config_path = argv[i];
    } else if (!strcmp(argv[i], "-C") || !strcmp(argv[i], "--check-config")) {
      check_config = true;
    } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
      printf("rawmsession %s\n", VERSION);
      return EXIT_SUCCESS;
    } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      usage(stdout);
      return EXIT_SUCCESS;
    } else {
      usage(stderr);
      return EXIT_FAILURE;
    }
  }

  rawmsession_config_init(&config);
  if (rawmsession_config_load(&config, config_path, stderr) < 0)
    goto out;
  if (check_config) {
    rc = EXIT_SUCCESS;
    goto out;
  }
  if (setup_signals() < 0)
    goto out;

  rc = run_session(&config, &restart);
  close_signal_pipe();

out:
  rawmsession_config_destroy(&config);
  if (restart && rc == EXIT_SUCCESS) {
    execvp(argv[0], argv);
    fprintf(stderr, "rawmsession: cannot restart %s: %s\n", argv[0],
            strerror(errno));
    return EXIT_FAILURE;
  }
  return rc;
}
