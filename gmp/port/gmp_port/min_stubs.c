/* Stubs for the storage and network shims a compute-only app does not want.
 *
 * These are not optional and they are not dead code. BareMetal-AppPort's
 * syscall dispatcher, __bmos_syscall() in port/posix_shim.c, is a single
 * function -- so it is a single --gc-sections section, and it is always
 * live because every musl syscall routes through it. Its switch references
 * ext4_shim_* and net_shim_* unconditionally, including on the plain
 * write() path that printf uses (posix_shim.c checks ext4_shim_is_fd and
 * net_shim_is_fd before deciding an fd is the console). So the references
 * survive garbage collection whatever the app does, and something has to
 * define them.
 *
 * The alternative is linking the real ext4_shim.o and net_shim.o, which
 * drags in lwext4 and lwIP -- megabytes of image for an app that opens no
 * file and no socket. That matters more here than it looks: the heap is a
 * bump allocator running from __bss_stop to __image_base + VM RAM, so every
 * byte of image is a byte GMP does not get for big-integer temporaries.
 *
 * The headers are included rather than the prototypes retyped, so a
 * signature drift upstream becomes a compile error here instead of a
 * silently mismatched call through the dispatcher.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/socket.h>

#include "ext4_shim.h"
#include "net_shim.h"
#include "net_glue.h"

/* No filesystem. is_fd says "not mine" so the dispatcher falls through to
 * the console path for fds 1 and 2, which is the only I/O this app does. */
int  ext4_shim_is_fd(long fd)                                  { (void)fd; return 0; }
long ext4_shim_open(long d, const char *p, int f, int m)        { (void)d; (void)p; (void)f; (void)m; return -ENOENT; }
long ext4_shim_read(long fd, void *b, size_t n)                 { (void)fd; (void)b; (void)n; return -EBADF; }
long ext4_shim_write(long fd, const void *b, size_t n)          { (void)fd; (void)b; (void)n; return -EBADF; }
long ext4_shim_close(long fd)                                   { (void)fd; return -EBADF; }
long ext4_shim_lseek(long fd, long o, int w)                    { (void)fd; (void)o; (void)w; return -EBADF; }
long ext4_shim_fstat_fd(long fd, void *s)                       { (void)fd; (void)s; return -EBADF; }
long ext4_shim_unlink(long d, const char *p)                    { (void)d; (void)p; return -ENOENT; }
long ext4_shim_truncate(long fd, size_t l)                      { (void)fd; (void)l; return -EBADF; }
long ext4_shim_fstatat(long d, const char *p, void *k, int fo)  { (void)d; (void)p; (void)k; (void)fo; return -ENOENT; }
long ext4_shim_symlink(const char *t, long d, const char *p)    { (void)t; (void)d; (void)p; return -EROFS; }
long ext4_shim_readlink(long d, const char *p, char *b, size_t n) { (void)d; (void)p; (void)b; (void)n; return -ENOENT; }
long ext4_shim_chdir(const char *p)                             { (void)p; return -ENOENT; }
long ext4_shim_fchdir(long fd)                                  { (void)fd; return -EBADF; }
long ext4_shim_mkdir(long d, const char *p)                     { (void)d; (void)p; return -EROFS; }
long ext4_shim_rmdir(long d, const char *p)                     { (void)d; (void)p; return -ENOENT; }
long ext4_shim_getdents(long fd, void *b, size_t n)             { (void)fd; (void)b; (void)n; return -EBADF; }
void ext4_shim_sync(void)                                       { }

/* getcwd is the one stub that answers rather than refuses: musl's exit path
 * and some library initialisation call it, and a failure there is noisier
 * than simply reporting the root. */
long ext4_shim_getcwd(char *buf, size_t size)
{
	if (size < 2) return -ERANGE;
	buf[0] = '/';
	buf[1] = '\0';
	return 2;
}

/* No network. */
int  net_shim_is_fd(long fd)                                    { (void)fd; return 0; }
long net_shim_socket(long d, long t, long p)                    { (void)d; (void)t; (void)p; return -EAFNOSUPPORT; }
long net_shim_bind(long f, const void *a, long l)               { (void)f; (void)a; (void)l; return -ENOTSOCK; }
long net_shim_listen(long f, long b)                            { (void)f; (void)b; return -ENOTSOCK; }
long net_shim_accept(long f, void *a, socklen_t *l)             { (void)f; (void)a; (void)l; return -ENOTSOCK; }
long net_shim_connect(long f, const void *a, long l)            { (void)f; (void)a; (void)l; return -ENOTSOCK; }
long net_shim_getsockname(long f, void *a, socklen_t *l)        { (void)f; (void)a; (void)l; return -ENOTSOCK; }
long net_shim_getpeername(long f, void *a, socklen_t *l)        { (void)f; (void)a; (void)l; return -ENOTSOCK; }
long net_shim_send(long f, const void *b, size_t n, long fl)    { (void)f; (void)b; (void)n; (void)fl; return -ENOTSOCK; }
long net_shim_recv(long f, void *b, size_t n, long fl)          { (void)f; (void)b; (void)n; (void)fl; return -ENOTSOCK; }
long net_shim_sendto(long f, const void *b, size_t n, long fl, const void *a, long al)
                                                                { (void)f; (void)b; (void)n; (void)fl; (void)a; (void)al; return -ENOTSOCK; }
long net_shim_recvfrom(long f, void *b, size_t n, long fl, void *a, socklen_t *al)
                                                                { (void)f; (void)b; (void)n; (void)fl; (void)a; (void)al; return -ENOTSOCK; }
long net_shim_close(long f)                                     { (void)f; return -ENOTSOCK; }
long net_shim_setsockopt(long f, long l, long o, const void *v, long n)  { (void)f; (void)l; (void)o; (void)v; (void)n; return -ENOTSOCK; }
long net_shim_getsockopt(long f, long l, long o, void *v, socklen_t *n)  { (void)f; (void)l; (void)o; (void)v; (void)n; return -ENOTSOCK; }

/* posix_shim.c's sleep_until_ns() pumps the network stack while waiting.
 * With no stack to pump this is empty -- but it must exist, because that
 * call site is on the nanosleep path, not behind any socket check. */
void net_poll(void)                                             { }
