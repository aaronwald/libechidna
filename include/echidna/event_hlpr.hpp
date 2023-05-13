#pragma once

#include <unordered_map>
#include <functional>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include <linux/io_uring.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <linux/io_uring.h>
#include <sys/syscall.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define read_barrier() __asm__ __volatile__("" :: \
																								: "memory")

#define write_barrier() __asm__ __volatile__("" :: \
																								 : "memory")

int io_uring_setup(unsigned entries, struct io_uring_params *p);
int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete, unsigned flags, sigset_t *sig);
int io_uring_register(int fd, unsigned opcode, const void *arg, unsigned nr_args);
namespace coypu::event
{

	class EPollHelper
	{
	public:
		static int Create(int flags = 0);
		static int Add(int efd, int fd, struct epoll_event *event);
		static int Modify(int efd, int fd, struct epoll_event *event);
		static int Delete(int efd, int fd);

	private:
		EPollHelper() = delete;
	};

	struct coypu_submit_ring
	{
		unsigned *head;
		unsigned *tail;
		unsigned *ring_mask;
		unsigned *ring_entries;
		unsigned *flags;
		unsigned *array;
	};

	struct coypu_completion_ring
	{
		unsigned *head;
		unsigned *tail;
		unsigned *ring_mask;
		unsigned *ring_entries;
		struct io_uring_cqe *cqes;
	};
	struct coypu_io_uring
	{
		int _fd;
		struct coypu_submit_ring _sq_ring;
		struct io_uring_sqe *_sqes;
		struct coypu_completion_ring _cq_ring;
	};

	class IOURingHelper
	{
	public:
		static int Create(coypu_io_uring &ring, uint32_t entries = 16, int32_t cpu = -1);

		static inline int SubmitNop(coypu_io_uring &ring, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));								// memset might be slow
			sqe.opcode = IORING_OP_NOP;										// https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.user_data = (unsigned long long)userdata; // user data
			return add_to_sqe(ring, &sqe);
		}

		static inline int SubmitEPollAdd(coypu_io_uring &ring, int efd, int fd, struct epoll_event *event, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.opcode = IORING_OP_EPOLL_CTL; // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.fd = efd;
			sqe.off = fd;
			sqe.len = EPOLL_CTL_ADD;
			sqe.addr = (uint64_t)event;
			sqe.user_data = (unsigned long long)userdata; // user data
			return add_to_sqe(ring, &sqe);
		}

		static inline int SubmitEPollModify(coypu_io_uring &ring, int efd, int fd, struct epoll_event *event, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.opcode = IORING_OP_EPOLL_CTL; // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.fd = efd;
			sqe.off = fd;
			sqe.len = EPOLL_CTL_MOD;
			sqe.addr = (uint64_t)event;
			sqe.user_data = (unsigned long long)userdata; // user data
			return add_to_sqe(ring, &sqe);
		}

		static inline int SubmitEPollDelete(coypu_io_uring &ring, int efd, int fd, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.opcode = IORING_OP_EPOLL_CTL; // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.fd = efd;
			sqe.off = fd;
			sqe.len = EPOLL_CTL_DEL;
			sqe.user_data = (unsigned long long)userdata; // user data
			return add_to_sqe(ring, &sqe);
		}

		static inline int SubmitTimeout(coypu_io_uring &ring, struct timespec *ts, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.opcode = IORING_OP_TIMEOUT; // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.addr = (unsigned long long)ts;
			sqe.len = 1;
			sqe.user_data = (unsigned long long)userdata; // user data
			sqe.timeout_flags = 0;												// relative
			return add_to_sqe(ring, &sqe);
		}

		// io_vecs cant go away
		static inline int SubmitReadv(coypu_io_uring &ring, int file_fd, struct iovec *iovecs, uint32_t len, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.fd = file_fd;
			sqe.opcode = IORING_OP_READV; // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.addr = (unsigned long long)iovecs;
			sqe.len = len;
			sqe.user_data = (unsigned long long)userdata; // user data
			return add_to_sqe(ring, &sqe);
		}

		static inline int SubmitsEND(coypu_io_uring &ring, int file_fd, char *buf, uint32_t len, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.fd = file_fd;
			sqe.opcode = IORING_OP_SEND; // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.addr = (unsigned long long)buf;
			sqe.len = len;
			sqe.user_data = (unsigned long long)userdata; // user data
			return add_to_sqe(ring, &sqe);
		}

		static inline int SubmitRecvMulti(coypu_io_uring &ring, int file_fd, char *buf, uint32_t len, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.fd = file_fd;
			sqe.opcode = IORING_OP_RECV; // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.addr = (unsigned long long)buf;
			sqe.len = len;
			sqe.user_data = (unsigned long long)userdata; // user data
			sqe.ioprio |= IORING_RECV_MULTISHOT;
			return add_to_sqe(ring, &sqe);
		}

		// io_vecs cant go away
		static inline int SubmitWritev(coypu_io_uring &ring, int file_fd, struct iovec *iovecs, uint32_t len, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.fd = file_fd;
			sqe.opcode = IORING_OP_WRITEV; // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.addr = (unsigned long long)iovecs;
			sqe.len = len;
			sqe.user_data = (unsigned long long)userdata; // user data
			return add_to_sqe(ring, &sqe);
		}

		static inline int SubmitClose(coypu_io_uring &ring, int fd, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.fd = fd;
			sqe.opcode = IORING_OP_CLOSE;									// https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.user_data = (unsigned long long)userdata; // user data
			return add_to_sqe(ring, &sqe);
		}

		static inline int SubmitConnectIPV4(coypu_io_uring &ring, int sockFD, struct sockaddr_in *serv_addr, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.fd = sockFD;
			sqe.flags = 0;
			sqe.opcode = IORING_OP_CONNECT; // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.addr = (unsigned long long)serv_addr;
			sqe.len = sizeof(struct sockaddr_in);
			sqe.user_data = (unsigned long long)userdata; // user data
			sqe.timeout_flags = 0;												// relative
			return add_to_sqe(ring, &sqe);
		}

		static inline int SubmitAcceptNonBlockMulti(coypu_io_uring &ring, int sockFD, struct sockaddr *addr, socklen_t *addrlen, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.fd = sockFD;
			sqe.opcode = IORING_OP_ACCEPT; // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.addr = (unsigned long long)addr;
			sqe.addr2 = (unsigned long long)addrlen;
			sqe.accept_flags = SOCK_NONBLOCK;
			sqe.user_data = (unsigned long long)userdata; // user data
			sqe.ioprio |= IORING_ACCEPT_MULTISHOT;
			return add_to_sqe(ring, &sqe);
		}

		static inline int SubmitSocket(coypu_io_uring &ring, int domain, int type, int protocol, uint64_t userdata)
		{
			struct io_uring_sqe sqe;
			::memset(&sqe, 0, sizeof(sqe));
			sqe.opcode = IORING_OP_SOCKET; // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
			sqe.fd = domain;
			sqe.off = type;
			sqe.len = protocol;
			sqe.user_data = (unsigned long long)userdata; // user data
			return add_to_sqe(ring, &sqe);
		}

		// res, userdata
		static void Drain(coypu_io_uring &ring, const std::function<void(int, uint64_t)> &cb);

	private:
		IOURingHelper() = delete;

		static int add_to_sqe(coypu_io_uring &ring, struct io_uring_sqe *in_sqe)
		{
			unsigned index = 0, tail = 0, next_tail = 0;

			/* Add our submission queue entry to the tail of the SQE ring buffer */
			next_tail = tail = *ring._sq_ring.tail;
			next_tail++;
			read_barrier();
			index = tail & *ring._sq_ring.ring_mask;

			struct io_uring_sqe *sqe = &ring._sqes[index];
			*sqe = *in_sqe;
			ring._sq_ring.array[index] = index;
			tail = next_tail;

			/* Update the tail so the kernel can see it. */
			if (*ring._sq_ring.tail != tail)
			{
				*ring._sq_ring.tail = tail;
				write_barrier();
			}

			unsigned flags = __atomic_load_n(ring._sq_ring.flags, __ATOMIC_RELAXED);
			if (flags & IORING_SQ_NEED_WAKEUP)
			{
				int ret = io_uring_enter(ring._fd, 1, 0, IORING_ENTER_SQ_WAKEUP, nullptr /*sig*/);
				if (ret < 0)
				{
					perror("io_uring_enter");
					return 1;
				}
			}

			return 0;
		}
	};

	class TimerFDHelper
	{
	public:
		static int CreateRealtimeNonBlock(int flags = 0);
		static int CreateMonotonicNonBlock(int flags = 0);
		static int SetRelativeRepeating(int fd, time_t sec, long nsec);

	private:
		TimerFDHelper() = delete;
	};

	class EventFDHelper
	{
	public:
		static int CreateNonBlockEventFD(int flags);

	private:
		EventFDHelper() = delete;
	};

	class SignalFDHelper
	{
	public:
		static int BlockAllSignals();
		static int CreateNonBlockSignalFD(const sigset_t *mask, int flags = 0);

	private:
		SignalFDHelper() = delete;
	};
} //  coypu::event
