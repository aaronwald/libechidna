#ifndef __COYPU_EVENT_HLPR_H
#define __COYPU_EVENT_HLPR_H

#include <unordered_map>
#include <functional>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include <linux/io_uring.h>
#include <sys/uio.h>
#include <netinet/in.h>

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

		static int SubmitReadv(coypu_io_uring &ring, int file_fd, struct iovec *iovecs, uint32_t len, uint64_t userdata);
		static int SubmitWritev(coypu_io_uring &ring, int file_fd, struct iovec *iovecs, uint32_t len, uint64_t userdata);
		static int SubmitNop(coypu_io_uring &ring, uint64_t userdata);
		static int SubmitTimeout(coypu_io_uring &ring, struct timespec *ts, uint64_t userdata);
		static int SubmitEPollAdd(coypu_io_uring &ring, int efd, int fd, struct epoll_event *event, uint64_t userdata);
		static int SubmitEPollModify(coypu_io_uring &ring, int efd, int fd, struct epoll_event *event, uint64_t userdata);
		static int SubmitEPollDelete(coypu_io_uring &ring, int efd, int fd, uint64_t userdata);

		static int SubmitSocket(coypu_io_uring &ring, int domain, int type, int protocol, uint64_t userdata);
		static int SubmtiAcceptNonBlock(coypu_io_uring &ring, int sockFD, struct sockaddr *addr, socklen_t *addrlen, uint64_t userdata, bool multi = false);
		static int SubmitConnectIPV4(coypu_io_uring &ring, int sockFD, struct sockaddr_in *serv_addr, uint64_t userdata);
		static int SubmitClose(coypu_io_uring &ring, int fd, uint64_t userdata);

		static void Drain(coypu_io_uring &ring, const std::function<void(uint64_t)> &cb);

	private:
		IOURingHelper() = delete;
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

#endif