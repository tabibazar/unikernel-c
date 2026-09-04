// inbound_bench.c -- how many inbound requests can this platform actually serve?
//
// This repo carries the claim "inbound serving fails at ~350 requests" in two
// documents, both saying it was "established separately". There is no logged
// run behind it anywhere in the tree. That number currently decides whether any
// request-serving workload can live here at all, so it is worth measuring
// rather than inheriting.
//
// WHAT IS ACTUALLY BEING ASKED
//
// Not "what is the number" but "what KIND of limit is it", because the two
// have opposite consequences:
//
//   a hard cap        something is exhausted permanently and serving is over.
//                     Nothing that accepts requests can run here.
//   a churn limit     a reclaimable resource runs out and comes back after a
//                     pause -- the classic being TCP TIME_WAIT. A server that
//                     keeps connections alive would never hit it, and the
//                     workload becomes viable with a different connection
//                     strategy.
//   a concurrency cap the port's own tables: SOCK_MAX is 16 and ACCEPTQ_MAX
//                     is 8 (port/net_shim.c). Those bound simultaneous
//                     sockets, not lifetime requests, so they cannot by
//                     themselves explain a cumulative failure near 350.
//
// THE STANDING HYPOTHESIS
//
// port/lwip_port/lwipopts.h sets neither MEMP_NUM_TCP_PCB nor
// MEMP_NUM_TCP_PCB_TIME_WAIT, so lwIP's own defaults apply -- five apiece.
// webserver.c answers with "Connection: close" and closes per request, so
// every single request mints a TIME_WAIT PCB. If the ceiling is that pool,
// then KEEPALIVE mode should sail past it and the limit should heal after a
// pause. That is the prediction this program is built to falsify.
//
// HOW IT REPORTS
//
// A failing accept() does NOT end the run. The interesting data is what
// happens after the first failure -- whether it recovers, and how -- and a
// program that exits on the first error can never see that. Every failure is
// printed with its errno and serving continues.
//
// Compile-time configured, because BareMetal passes no argv.
//
//   baremetal: cp inbound_bench.c BareMetal-App/ && ./1-build.sh inbound_bench.c
//   linux:     gcc -O2 -o inbound_bench inbound_bench.c   (same source, control)

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifndef BENCH_PORT
#define BENCH_PORT 8080
#endif
#ifndef BENCH_KEEPALIVE
#define BENCH_KEEPALIVE 0     // 0 = Connection: close  1 = keep the socket open
#endif
#ifndef BENCH_REPORT_EVERY
#define BENCH_REPORT_EVERY 25
#endif
#ifndef BENCH_MAX_REQUESTS
#define BENCH_MAX_REQUESTS 5000
#endif

static char reqbuf[2048];

// One counter per outcome. Printed together so a console capture is
// self-contained -- there is no filesystem to collect afterwards, and the
// serial log is the whole record.
static long n_accept_ok, n_accept_fail, n_served, n_recv_fail, n_send_fail;
static int  first_fail_at = -1, first_fail_errno = 0;
static int  recovered_after_fail = 0;

static void report(const char *why)
{
	printf("BENCH_STAT %s served=%ld accept_ok=%ld accept_fail=%ld "
	       "recv_fail=%ld send_fail=%ld first_fail_at=%d first_errno=%d "
	       "recovered=%d\n",
	       why, n_served, n_accept_ok, n_accept_fail, n_recv_fail,
	       n_send_fail, first_fail_at, first_fail_errno, recovered_after_fail);
	fflush(stdout);
}

// Serve one already-accepted connection. Returns how many requests it handled
// before the peer went away, so keep-alive and close modes share one path and
// only the header differs.
static long serve_conn(int cfd)
{
	long handled = 0;
	for (;;) {
		int n = (int)recv(cfd, reqbuf, sizeof(reqbuf) - 1, 0);
		if (n <= 0) {
			if (n < 0) n_recv_fail++;
			break;
		}
		reqbuf[n] = '\0';

		char body[128];
		int blen = snprintf(body, sizeof body,
		                    "served=%ld\n", n_served + handled + 1);
		char resp[512];
		int rlen = snprintf(resp, sizeof resp,
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: %d\r\n"
			"Connection: %s\r\n"
			"\r\n%s",
			blen, BENCH_KEEPALIVE ? "keep-alive" : "close", body);

		if (send(cfd, resp, (size_t)rlen, 0) < 0) { n_send_fail++; break; }
		handled++;

#if !BENCH_KEEPALIVE
		break;              // one request per connection: the churn case
#endif
	}
	return handled;
}

int main(void)
{
	printf("BENCH_START port=%d keepalive=%d max=%d\n",
	       BENCH_PORT, BENCH_KEEPALIVE, BENCH_MAX_REQUESTS);
	fflush(stdout);

	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0) { printf("BENCH_FATAL socket errno=%d\n", errno); return 1; }

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(BENCH_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		printf("BENCH_FATAL bind errno=%d\n", errno);
		return 1;
	}
	// Backlog deliberately at the port's ACCEPTQ_MAX. Asking for more would
	// silently clamp and make a queue-depth finding unattributable.
	if (listen(lfd, 8) < 0) {
		printf("BENCH_FATAL listen errno=%d\n", errno);
		return 1;
	}
	printf("BENCH_LISTENING\n");
	fflush(stdout);

	while (n_served < BENCH_MAX_REQUESTS) {
		struct sockaddr_in cli;
		socklen_t clilen = sizeof cli;
		int cfd = accept(lfd, (struct sockaddr *)&cli, &clilen);

		if (cfd < 0) {
			n_accept_fail++;
			if (first_fail_at < 0) {
				first_fail_at = (int)n_served;
				first_fail_errno = errno;
				// This line is the headline result. Everything before it is
				// setup; everything after it is the recovery question.
				printf("BENCH_FIRST_FAIL after=%ld errno=%d\n",
				       n_served, errno);
				report("first-failure");
			}
			// Keep going on purpose. If this is TIME_WAIT or a pool that
			// drains, accept() starts working again on its own, and that
			// recovery is the finding -- a program that exited here would
			// report a hard cap that does not exist.
			continue;
		}

		n_accept_ok++;
		if (first_fail_at >= 0 && !recovered_after_fail) {
			recovered_after_fail = 1;
			printf("BENCH_RECOVERED at=%ld after_failures=%ld\n",
			       n_served, n_accept_fail);
			fflush(stdout);
		}

		n_served += serve_conn(cfd);
		close(cfd);

		if (n_served % BENCH_REPORT_EVERY == 0) report("progress");
	}

	report("final");
	printf("BENCH_DONE\n");
	close(lfd);
	return 0;
}
