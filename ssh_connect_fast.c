/*
 * ssh_connect_fast.c: ssh(1) trampoline for faster connection setup
 * by pts@fazekas.hu at Mon Oct  9 15:08:14 CEST 2017
 *
 * ssh_connect_fast is a small C trampoline tool which helps speeding up SSH
 * connection setup on the client side, by selecting a faster ssh-agent (if
 * available and already started) and by ignoring system-level options (in
 * /etc/ssh/ssh_config) for some server hosts.
 *
 * Compilation (tested with GCC 4.8.4):
 *
 *   gcc -s -O2 -W -Wall -Wextra -Werror -Werror=missing-declarations ssh_connect_fast.c -o ssh_connect_fast
 *
 * Compilation with xtiny (for a tiny Linux i386 executable):
 *
 *   xtiny gcc  -W -Wall -Wextra -Werror -Werror=missing-declarations ssh_connect_fast.c -o ssh_connect_fast
 *
 * Use ssh_connect_fast instead of ssh, or copy the ssh_connect_fast
 * executable to somewhere on your $PATH earlier than the real ssh
 * executable.
 *
 * License: GNU GPL v2 or newer.
 *
 * See README.txt for more information.
 */

#ifdef __XTINY__
#include <xtiny.h>
#else
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#endif

static char *my_getenv_eq(const char *name_eq, char **envp) {
  char *p;
  size_t name_eq_size = strlen(name_eq);
  for (; (p = *envp); ++envp) {
    if (0 == strncmp(name_eq, p, name_eq_size)) {
      return p + name_eq_size;
    }
  }
  return NULL;
}

/* We assume that prog doesn't contain a '/'.
 * Also it changes argv[0].
 */
static void my_execvp(const char *prog, const char *skip_filename,
                      char **argv, char **envp) {
  static char filename[4096];
  const char *path = my_getenv_eq("PATH=", envp);
  char const *p, *q;
  char *r;
  size_t dir_size, prog_size = strlen(prog), sum_size;
  if (path == NULL) path = "/bin:/usr/bin";  /* libc6 default. */
  argv[0] = filename;
  /* We don't handle the case when prog contains '/', not needed here. */
  for (p = path; *p != '\0'; ) {
    for (q = p; *q != '\0' && *q != ':'; ++q) {}
    dir_size = q - p;
    if ((sum_size = dir_size + prog_size) < dir_size ||
        sum_size > sizeof(filename) - 2) {  /* Check for overflow. */
      p = q + (*q == ':');
      continue;
    }
    memcpy(filename, p, dir_size);
    r = filename + dir_size;
    *r++ = '/';
    strcpy(r, prog);
    p = q + (*q == ':');
    if (skip_filename != NULL && 0 == strcmp(filename, skip_filename)) continue;
    execve(filename, argv, envp);
    /* Continue with the next candidate, if execve failed.
     * It looks like libc6 and uClibc execvp do continue, no matter the errno
     * of execve.
     */
  }
}

static const char source_env_prefix[] = "SSH_AUTH_SOCK_FAST=";

/* Renames environment variable SSH_AUTH_SOCK_FAST to SSH_AUTH_SOCK */
static void rename_from_ssh_auth_sock_fast(char **envp) {
  /* Must not be longer than source_env_prefix. */
  static const char target_env_prefix[] = "SSH_AUTH_SOCK=";
  if (my_getenv_eq(source_env_prefix, envp) != NULL) {
    char *p;
    char **outp = envp;
    while ((p = *envp) != NULL) {
      if (0 == strncmp(p, source_env_prefix, sizeof(source_env_prefix) - 1)) {
        char *value = p + sizeof(source_env_prefix) - 1;
        char *valuet = value - sizeof(target_env_prefix) + 1;
        memcpy(*outp++ = valuet, target_env_prefix,
               sizeof(target_env_prefix) - 1);
        ++envp;
      } else if (0 == strncmp(
          p, target_env_prefix, sizeof(target_env_prefix) - 1)) {
        ++envp;  /* Copy the old value of the source. */
      } else {
        *outp++ = *envp++;
      }
    }
    *outp = NULL;
  }
}

static char is_fast_host(char *ssh_config_filename, char **args) {
  /* OpenSSH 7.3 and OpenSSH 8.2, from `ssh -h'. */
  static const char ssh_flags_with_arg[] = "DEFIJLOQRSWbceilmopw";
  /* Must be large enough to fit hostpath_prefix. Should
   * ne large enough to be efficient to avoid system call overhead with
   * read(2). Should be large enough to fit the entire line of hostpath_prefix
   * (if not, some host names will be ignored in the end).
   */
  static char buf[16384];
  int fd;
  char *p, *q, c;
  while ((p = *args++) != NULL) {
    if (*p != '-') break;
    if (*++p == '-') { p = *args; break; }
    while ((c = *p++) != '\0') {
      if (0 != strchr(ssh_flags_with_arg, c)) {
        if (*p == '\0') {
          if (*args != NULL) ++args;  /* Skip argument of flag c. */
        }
        break;
      }
    }
  }
  /* Now p is either NULL or points to the user@hostname arg within args. */
  if (!p) return 0;
  q = p;
  while ((c = *q++) != '\0') {
    if (c == '@') {
      p = q;
      break;
    }
  }
  /* Now p points to the hostname within args.
   *
   * We try to find a line starting with "Host .fast " and containing the
   * hostname (as an item in the whitespace-separated list) in
   * ssh_config_filename.
   */
  fd = open(ssh_config_filename, O_RDONLY, 0);
  if (fd >= 0) {
    static const char hostfast_prefix[] = "Host .fast ";
    int got;
    /* Number of chars of hostfast_prefix at the end of previous buf, or -1
     * if in the middle of a non-matching line.
     */
    int prevc = 0;
    char *bufend;
    while ((got = read(fd, buf, sizeof(buf))) > 0) {
      bufend = buf + got;
      q = buf;
      if (prevc < 0) goto skip_current_line;
      if (0 == memcmp(q, hostfast_prefix + prevc,
                      sizeof(hostfast_prefix) - 1 - prevc)) {
        q += sizeof(hostfast_prefix) - 1 - prevc;
        goto find_hostname_in_host_line;
      }
      for (;;) {
        for (; q != bufend && (
            (c = *q) == ' ' || c == '\n'); ++q) {}
        prevc = 0;
        for (; q != bufend && *q == hostfast_prefix[prevc]; ++q, ++prevc) {}
        if (prevc != sizeof(hostfast_prefix) - 1) {  /* No match. */
          if (q == bufend) break;  /* Use prevc in the next buf. */
         skip_current_line:
          for (; q != bufend && *q != '\n'; ++q) {}
          if (q == bufend) {  /* Line continues in the next buf. */
            prevc = -1;
            break;
          }
          continue;  /* Next iteration will skip the '\n' in *q. */
        }
       find_hostname_in_host_line:  /* Starting with q. */
        for (;;) {
          for (; q != bufend && *q == ' '; ++q) {}
          prevc = 0;  /* Reusing prevc here. It's harmless. */
          for (; q != bufend && (c = *q) != '\0' && c != '\n' && c != ' ' &&
              c == p[prevc]; ++q, ++prevc) {}
          if (q == bufend) break;  /* Line too long: ignore last word. */
          /* Hostname matches. */
          if (p[prevc] == '\0' && (c == ' ' || c == '\n')) {
            close(fd);
            return 1;
          }
          for (; q != bufend && (c = *q) != ' ' && c != '\n'; ++q) {}
          if (c == '\n') break;  /* Next iteration will skip the '\n'. */
        }
      }
    }
    close(fd);
  }
  return 0;
}

int main(int argc, char **argv, char **envp) {
  static char exec_error[] = "fatal: ssh not found\n";
  static char sshfarg[256];
  static char *xargv[256];
  char **argp = argv, **argq;
  char *p, *q;
  char *home = my_getenv_eq("HOME=", envp);
  static char ssh_config_suffix[] = "/.ssh/config";
  for (argq = argv; *argq != NULL; ++argq) {}
  argc = argq - argv;  /* Recompute to be sure. */
  /* Detect shortcut if called in a chain. */
  if (argv[0] != NULL && (p = argv[1]) != NULL &&
      p[0] == '-' && p[1] == 'F' && p[2] != '\0') {
    if (my_getenv_eq(source_env_prefix, envp) != NULL &&
        is_fast_host(p + 2, argv + 1)) {
      rename_from_ssh_auth_sock_fast(envp);
    }
  } else if (/* -1 because sshfarg must fit, and -1 because NULL must fit. */
      argc + 0U > sizeof(xargv) / sizeof(xargv[0]) - 1 - 1 ||
      !home ||
      strlen(home) + 2 + sizeof(ssh_config_suffix) > sizeof(sshfarg)) {
    /* Input too complicated (long), do no changes. */
  } else {
    p = sshfarg;
    *p++ = '-';
    *p++ = 'F';
    q = home;
    for (q = home; (*p = *q++) != '\0'; ++p) {}
    for (q = ssh_config_suffix; (*p++ = *q++) != '\0'; ) {}
    /* Now sshfarg is NUL-terminated. */
    if (is_fast_host(sshfarg + 2, argv + 1)) {
      argp = xargv;
      *argp++ = argv[0];
      *argp++ = sshfarg;
      for (argq = argv + 1; *argq != NULL; ) {
        *argp++ = *argq++;  /* Overflow is checked by `argc + 0U >' above. */
      }
      *argp++ = NULL;
      rename_from_ssh_auth_sock_fast(envp);
      argp = xargv;
    }
  }
  my_execvp("ssh", argv[0], argp, envp);
  (void)!write(2, exec_error, sizeof(exec_error) - 1);
  return 121;
}
