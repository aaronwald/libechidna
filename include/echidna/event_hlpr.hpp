#ifndef __COYPU_EVENT_HLPR_H
#define __COYPU_EVENT_HLPR_H

#include <unordered_map>
#include <functional>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include <linux/io_uring.h>

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
		static int Create(coypu_io_uring &ring, uint32_t entries = 1024);
		static int SubmitReadv(coypu_io_uring &ring, int file_fd, struct iovec *iovecs, uint32_t len, void *userdata);
		static int SubmitWritev(coypu_io_uring &ring, int file_fd, struct iovec *iovecs, uint32_t len, void *userdata);

		static void ReadCompletion(coypu_io_uring &ring);

		static int Submit(coypu_io_uring &ring, int file_fd, char op_code, struct iovec *iovecs, uint32_t len, void *userdata);

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