/* SPDX-License-Identifier: MIT */
#include "rawmstat-source.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SAMPLE_TEXT_MAX 1024u

static char *
xstrdup(const char *s)
{
  char *copy = strdup(s);

  if (!copy) {
    fputs("rawmstat: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  return copy;
}

static const char *
proc_root(void)
{
  const char *root = getenv("RAWMSTAT_PROC_ROOT");
  return root && root[0] ? root : "/proc";
}

static const char *
sys_root(void)
{
  const char *root = getenv("RAWMSTAT_SYS_ROOT");
  return root && root[0] ? root : "/sys";
}

static int
set_sample(char **text, RawmStatusState *state, const char *value,
           RawmStatusState semantic)
{
  size_t length = strlen(value);

  if (!rawm_status_text_valid((const unsigned char *)value, length))
    return -1;
  free(*text);
  *text = xstrdup(value);
  *state = semantic;
  return 0;
}

static int
sample_clock(const RawmstatSourceConfig *config, char **text,
             RawmStatusState *state)
{
  char buffer[SAMPLE_TEXT_MAX];
  struct tm local;
  time_t now = time(NULL);

  if (now == (time_t)-1 || !localtime_r(&now, &local))
    return -1;
  if (!strftime(buffer, sizeof(buffer), config->format, &local))
    return -1;
  return set_sample(text, state, buffer, RAWM_STATUS_NORMAL);
}

static int
read_cpu(uint64_t *total, uint64_t *idle)
{
  char path[4096], line[4096], *save = NULL, *token;
  FILE *fp;
  uint64_t values[10] = {0};
  size_t count = 0;

  snprintf(path, sizeof(path), "%s/stat", proc_root());
  fp = fopen(path, "r");
  if (!fp)
    return -1;
  if (!fgets(line, sizeof(line), fp)) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  token = strtok_r(line, " \t\n", &save);
  if (!token || strcmp(token, "cpu"))
    return -1;
  while (count < sizeof(values) / sizeof(values[0]) &&
         (token = strtok_r(NULL, " \t\n", &save))) {
    char *end;
    unsigned long long value;

    errno = 0;
    value = strtoull(token, &end, 10);
    if (errno || !end || *end)
      return -1;
    values[count++] = (uint64_t)value;
  }
  if (count < 4)
    return -1;
  *total = 0;
  for (size_t i = 0; i < count; ++i)
    *total += values[i];
  *idle = values[3] + (count > 4 ? values[4] : 0);
  return *idle <= *total ? 0 : -1;
}

static int
sample_cpu(RawmstatSource *source, char **text, RawmStatusState *state)
{
  uint64_t total, idle, delta_total, delta_idle, used;
  char buffer[64];
  RawmStatusState semantic = RAWM_STATUS_NORMAL;

  if (read_cpu(&total, &idle) < 0)
    return -1;
  if (!source->cpu_baseline || total <= source->cpu_total || idle < source->cpu_idle) {
    source->cpu_baseline = true;
    source->cpu_total = total;
    source->cpu_idle = idle;
    return 1;
  }
  delta_total = total - source->cpu_total;
  delta_idle = idle - source->cpu_idle;
  source->cpu_total = total;
  source->cpu_idle = idle;
  if (!delta_total || delta_idle > delta_total)
    return -1;
  used = (100u * (delta_total - delta_idle) + delta_total / 2u) / delta_total;
  if (used > 100)
    used = 100;
  if (used >= source->config->critical)
    semantic = RAWM_STATUS_CRITICAL;
  else if (used >= source->config->warning)
    semantic = RAWM_STATUS_WARNING;
  snprintf(buffer, sizeof(buffer), "cpu %llu%%", (unsigned long long)used);
  return set_sample(text, state, buffer, semantic);
}

static int
sample_memory(const RawmstatSourceConfig *config, char **text,
              RawmStatusState *state)
{
  char path[4096], key[128], unit[32], buffer[64];
  FILE *fp;
  unsigned long long value, total = 0, available = 0;
  bool got_total = false, got_available = false;
  unsigned int used;
  RawmStatusState semantic = RAWM_STATUS_NORMAL;

  snprintf(path, sizeof(path), "%s/meminfo", proc_root());
  fp = fopen(path, "r");
  if (!fp)
    return -1;
  while (fscanf(fp, "%127s %llu %31s", key, &value, unit) == 3) {
    if (!strcmp(key, "MemTotal:")) {
      if (got_total || strcmp(unit, "kB"))
        goto invalid;
      total = value;
      got_total = true;
    } else if (!strcmp(key, "MemAvailable:")) {
      if (got_available || strcmp(unit, "kB"))
        goto invalid;
      available = value;
      got_available = true;
    }
  }
  fclose(fp);
  if (!got_total || !got_available || !total || available > total)
    return -1;
  used = (unsigned int)((100u * (total - available) + total / 2u) / total);
  if (used >= config->critical)
    semantic = RAWM_STATUS_CRITICAL;
  else if (used >= config->warning)
    semantic = RAWM_STATUS_WARNING;
  snprintf(buffer, sizeof(buffer), "mem %u%%", used);
  return set_sample(text, state, buffer, semantic);
invalid:
  fclose(fp);
  return -1;
}

static int
sample_filesystem(const RawmstatSourceConfig *config, char **text,
                  RawmStatusState *state)
{
  struct statvfs fs;
  unsigned long long bytes;
  double gib;
  char buffer[64];
  RawmStatusState semantic = RAWM_STATUS_NORMAL;

  if (statvfs(config->path, &fs) < 0)
    return -1;
  bytes = (unsigned long long)fs.f_bavail * (unsigned long long)fs.f_frsize;
  gib = (double)bytes / (1024.0 * 1024.0 * 1024.0);
  if (gib <= (double)config->critical)
    semantic = RAWM_STATUS_CRITICAL;
  else if (gib <= (double)config->warning)
    semantic = RAWM_STATUS_WARNING;
  if (gib < 10.0)
    snprintf(buffer, sizeof(buffer), "disk %.1fG", gib);
  else
    snprintf(buffer, sizeof(buffer), "disk %.0fG", gib);
  return set_sample(text, state, buffer, semantic);
}

static int
read_uint64_file(const char *path, uint64_t *value)
{
  FILE *fp = fopen(path, "r");
  unsigned long long parsed;
  int tail;

  if (!fp)
    return -1;
  if (fscanf(fp, "%llu", &parsed) != 1) {
    fclose(fp);
    return -1;
  }
  do {
    tail = fgetc(fp);
  } while (tail == ' ' || tail == '\t' || tail == '\n' || tail == '\r');
  fclose(fp);
  if (tail != EOF)
    return -1;
  *value = (uint64_t)parsed;
  return 0;
}

static int
find_default_interface(char *buffer, size_t size)
{
  char path[4096], line[4096], iface[256], destination[32], flags[32];
  FILE *fp;

  snprintf(path, sizeof(path), "%s/net/route", proc_root());
  fp = fopen(path, "r");
  if (!fp)
    return -1;
  if (!fgets(line, sizeof(line), fp)) {
    fclose(fp);
    return -1;
  }
  while (fgets(line, sizeof(line), fp)) {
    unsigned long parsed_flags;
    char *end;

    if (sscanf(line, "%255s %31s %*s %31s", iface, destination, flags) != 3)
      continue;
    errno = 0;
    parsed_flags = strtoul(flags, &end, 16);
    if (errno || !end || *end)
      continue;
    if (!strcmp(destination, "00000000") && (parsed_flags & 0x1u)) {
      snprintf(buffer, size, "%s", iface);
      fclose(fp);
      return 0;
    }
  }
  fclose(fp);
  return -1;
}

static void
format_rate(char *buffer, size_t size, uint64_t rate)
{
  if (rate >= 1048576u)
    snprintf(buffer, size, "%.1fM", (double)rate / 1048576.0);
  else if (rate >= 1024u)
    snprintf(buffer, size, "%.0fK", (double)rate / 1024.0);
  else
    snprintf(buffer, size, "%lluB", (unsigned long long)rate);
}

static int
sample_network(RawmstatSource *source, uint64_t now_ms, char **text,
               RawmStatusState *state)
{
  char iface[256], rx_path[4096], tx_path[4096], rx_text[32], tx_text[32];
  char buffer[128];
  uint64_t rx, tx, dt, rx_rate, tx_rate;

  if (!strcmp(source->config->interface, "default")) {
    if (find_default_interface(iface, sizeof(iface)) < 0)
      return -1;
  } else {
    snprintf(iface, sizeof(iface), "%s", source->config->interface);
  }
  snprintf(rx_path, sizeof(rx_path), "%s/class/net/%s/statistics/rx_bytes",
           sys_root(), iface);
  snprintf(tx_path, sizeof(tx_path), "%s/class/net/%s/statistics/tx_bytes",
           sys_root(), iface);
  if (read_uint64_file(rx_path, &rx) < 0 || read_uint64_file(tx_path, &tx) < 0)
    return -1;

  if (!source->network_baseline || !source->network_interface ||
      strcmp(source->network_interface, iface) || now_ms <= source->network_time_ms ||
      rx < source->network_rx || tx < source->network_tx) {
    free(source->network_interface);
    source->network_interface = xstrdup(iface);
    source->network_rx = rx;
    source->network_tx = tx;
    source->network_time_ms = now_ms;
    source->network_baseline = true;
    snprintf(buffer, sizeof(buffer), "net %s", iface);
    return set_sample(text, state, buffer, RAWM_STATUS_NORMAL);
  }

  dt = now_ms - source->network_time_ms;
  rx_rate = (rx - source->network_rx) * 1000u / dt;
  tx_rate = (tx - source->network_tx) * 1000u / dt;
  source->network_rx = rx;
  source->network_tx = tx;
  source->network_time_ms = now_ms;
  format_rate(rx_text, sizeof(rx_text), rx_rate);
  format_rate(tx_text, sizeof(tx_text), tx_rate);
  snprintf(buffer, sizeof(buffer), "net ↓%s ↑%s", rx_text, tx_text);
  return set_sample(text, state, buffer, RAWM_STATUS_NORMAL);
}

static int
sample_battery(const RawmstatSourceConfig *config, char **text,
               RawmStatusState *state)
{
  char base[4096], path[4096], status[64] = "", buffer[64];
  DIR *dir;
  struct dirent *entry;
  uint64_t capacity;

  snprintf(base, sizeof(base), "%s/class/power_supply", sys_root());
  dir = opendir(base);
  if (!dir)
    return -1;
  while ((entry = readdir(dir))) {
    FILE *fp;
    const char *suffix = "";
    RawmStatusState semantic = RAWM_STATUS_NORMAL;

    if (strncmp(entry->d_name, "BAT", 3))
      continue;
    snprintf(path, sizeof(path), "%s/%s/capacity", base, entry->d_name);
    if (read_uint64_file(path, &capacity) < 0 || capacity > 100)
      continue;
    snprintf(path, sizeof(path), "%s/%s/status", base, entry->d_name);
    fp = fopen(path, "r");
    if (fp) {
      if (!fgets(status, sizeof(status), fp))
        status[0] = '\0';
      fclose(fp);
      status[strcspn(status, "\r\n")] = '\0';
    }
    if (!strcmp(status, "Charging"))
      suffix = "+";
    else if (!strcmp(status, "Full"))
      suffix = "=";
    else if (capacity <= config->critical)
      semantic = RAWM_STATUS_CRITICAL;
    else if (capacity <= config->warning)
      semantic = RAWM_STATUS_WARNING;
    snprintf(buffer, sizeof(buffer), "bat %llu%%%s",
             (unsigned long long)capacity, suffix);
    closedir(dir);
    return set_sample(text, state, buffer, semantic);
  }
  closedir(dir);
  return -1;
}

static uint64_t
monotonic_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
    return 0;
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int
parse_exec_line(char *line, char **text, RawmStatusState *state)
{
  char *tab = strchr(line, '\t');
  RawmStatusState semantic = RAWM_STATUS_NORMAL;
  const char *payload = line;

  line[strcspn(line, "\r\n")] = '\0';
  if (!line[0])
    return -1;
  if (tab) {
    *tab = '\0';
    if (!rawm_status_state_parse((const unsigned char *)line, strlen(line),
                                 &semantic) || semantic == RAWM_STATUS_STALE)
      return -1;
    payload = tab + 1;
  }
  return set_sample(text, state, payload, semantic);
}

static int
sample_exec(const RawmstatSourceConfig *config, char **text,
            RawmStatusState *state)
{
  int pipefd[2], status = 0;
  pid_t pid;
  char buffer[SAMPLE_TEXT_MAX + 1];
  size_t used = 0;
  uint64_t deadline;
  bool exited = false, eof = false;

  if (pipe(pipefd) < 0)
    return -1;
  pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  if (pid == 0) {
    int devnull;

    setpgid(0, 0);
    close(pipefd[0]);
    if (dup2(pipefd[1], STDOUT_FILENO) < 0)
      _exit(126);
    close(pipefd[1]);
    devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDERR_FILENO);
      if (devnull != STDERR_FILENO)
        close(devnull);
    }
    execvp(config->argv[0], config->argv);
    _exit(errno == ENOENT ? 127 : 126);
  }
  close(pipefd[1]);
  setpgid(pid, pid);
  fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL, 0) | O_NONBLOCK);
  deadline = monotonic_ms() + config->timeout_ms;

  while (!eof || !exited) {
    struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN | POLLHUP };
    uint64_t now = monotonic_ms();
    int timeout, rc;

    if (now >= deadline) {
      kill(-pid, SIGKILL);
      waitpid(pid, &status, 0);
      close(pipefd[0]);
      return -1;
    }
    timeout = (int)(deadline - now);
    if (timeout > 100)
      timeout = 100;
    rc = poll(&pfd, 1, timeout);
    if (rc < 0 && errno != EINTR) {
      kill(-pid, SIGKILL);
      waitpid(pid, &status, 0);
      close(pipefd[0]);
      return -1;
    }
    if (rc > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
      for (;;) {
        ssize_t n = read(pipefd[0], buffer + used, SAMPLE_TEXT_MAX - used);
        if (n > 0) {
          used += (size_t)n;
          if (memchr(buffer, '\n', used) || used == SAMPLE_TEXT_MAX)
            break;
          continue;
        }
        if (n == 0)
          eof = true;
        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
          eof = true;
        break;
      }
    }
    if (!exited) {
      pid_t done = waitpid(pid, &status, WNOHANG);
      if (done == pid)
        exited = true;
    }
    if (memchr(buffer, '\n', used) && exited)
      eof = true;
  }
  close(pipefd[0]);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || !used)
    return -1;
  if (used == SAMPLE_TEXT_MAX && !memchr(buffer, '\n', used))
    return -1;
  buffer[used] = '\0';
  return parse_exec_line(buffer, text, state);
}

void
rawmstat_source_init(RawmstatSource *source,
                     const RawmstatSourceConfig *config)
{
  memset(source, 0, sizeof(*source));
  source->config = config;
}

void
rawmstat_source_destroy(RawmstatSource *source)
{
  free(source->text);
  free(source->network_interface);
  memset(source, 0, sizeof(*source));
}

int
rawmstat_source_refresh(RawmstatSource *source, uint64_t now_ms, FILE *err)
{
  char *text = NULL;
  RawmStatusState state = RAWM_STATUS_NORMAL;
  int rc = -1;

  switch (source->config->provider) {
  case RAWMSTAT_PROVIDER_BATTERY:
    rc = sample_battery(source->config, &text, &state);
    break;
  case RAWMSTAT_PROVIDER_CLOCK:
    rc = sample_clock(source->config, &text, &state);
    break;
  case RAWMSTAT_PROVIDER_CPU:
    rc = sample_cpu(source, &text, &state);
    break;
  case RAWMSTAT_PROVIDER_EXEC:
    rc = sample_exec(source->config, &text, &state);
    break;
  case RAWMSTAT_PROVIDER_FILESYSTEM:
    rc = sample_filesystem(source->config, &text, &state);
    break;
  case RAWMSTAT_PROVIDER_MEMORY:
    rc = sample_memory(source->config, &text, &state);
    break;
  case RAWMSTAT_PROVIDER_NETWORK:
    rc = sample_network(source, now_ms, &text, &state);
    break;
  }

  source->next_due_ms = now_ms + source->config->period_ms;
  if (rc == 0) {
    free(source->text);
    source->text = text;
    source->state = state;
    source->has_value = true;
    return 1;
  }
  free(text);
  if (rc == 1)
    return 0;
  if (source->has_value && source->state != RAWM_STATUS_STALE) {
    source->state = RAWM_STATUS_STALE;
    fprintf(err, "rawmstat: source '%s' refresh failed; marking stale\n",
            source->config->id);
    return 1;
  }
  if (!source->has_value)
    fprintf(err, "rawmstat: source '%s' refresh failed\n", source->config->id);
  return 0;
}
