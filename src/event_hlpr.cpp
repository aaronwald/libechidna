
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/mman.h>
#include <string.h>
#include <iostream>

#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>

#include "echidna/event_hlpr.hpp"
int io_uring_setup(unsigned entries, struct io_uring_params *p)
{
  return syscall(__NR_io_uring_setup, entries, p);
}

int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete, unsigned flags, sigset_t *sig)
{
  return syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, sig);
}

int io_uring_register(int fd, unsigned opcode, const void *arg, unsigned nr_args)
{
  return syscall(__NR_io_uring_register, fd, opcode, arg, nr_args);
}
using namespace coypu::event;

int EPollHelper::Create(int flags)
{
  // size is deprecated
  return ::epoll_create1(flags);
}

int EPollHelper::Add(int efd, int fd, struct epoll_event *event)
{
  return ::epoll_ctl(efd, EPOLL_CTL_ADD, fd, event);
}

int EPollHelper::Modify(int efd, int fd, struct epoll_event *event)
{
  return ::epoll_ctl(efd, EPOLL_CTL_MOD, fd, event);
}

int EPollHelper::Delete(int efd, int fd)
{
  //  std::cout << "Delete " << fd  << std::endl;
  return ::epoll_ctl(efd, EPOLL_CTL_DEL, fd, nullptr);
}

int EventFDHelper::CreateNonBlockEventFD(int flags)
{
  return ::eventfd(0, flags | EFD_NONBLOCK);
}

int TimerFDHelper::CreateRealtimeNonBlock(int flags)
{
  return ::timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | flags);
}

int TimerFDHelper::CreateMonotonicNonBlock(int flags)
{
  return ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | flags);
}

int TimerFDHelper::SetRelativeRepeating(int fd, time_t sec, long nsec)
{
  struct itimerspec ts;
  ::memset(&ts, 0, sizeof(struct itimerspec));
  ts.it_value.tv_sec = sec;
  ts.it_value.tv_nsec = nsec;
  ts.it_interval.tv_sec = sec;
  ts.it_interval.tv_nsec = nsec;
  return ::timerfd_settime(fd, 0, &ts, NULL);
}

int SignalFDHelper::CreateNonBlockSignalFD(const sigset_t *mask, int flags)
{
  return ::signalfd(-1, mask, flags | SFD_NONBLOCK);
}

int SignalFDHelper::BlockAllSignals()
{
  sigset_t mask;
  ::sigfillset(&mask);
  return ::sigprocmask(SIG_SETMASK, &mask, NULL); // set mask blocks these signals
}

// https://unixism.net/loti/low_level.html
int IOURingHelper::Create(coypu_io_uring &ring, uint32_t entries, int32_t cpu)
{
  struct io_uring_params params;
  memset(&params, 0, sizeof(params));
  // params.sq_thread_idle - idle milliseconds

#ifdef IORING_SETUP_SINGLE_ISSUER
  params.flags |= IORING_SETUP_SINGLE_ISSUER;
#endif

#ifdef IORING_SETUP_SUBMIT_ALL
  params.flags |= IORING_SETUP_SUBMIT_ALL;
#endif

  // #ifdef IORING_SETUP_COOP_TASKRUN
  //   params.flags |= IORING_SETUP_COOP_TASKRUN;
  // #endif

#ifdef IORING_SETUP_SQPOLL
  params.flags |= IORING_SETUP_SQPOLL;
#endif

#ifdef IORING_SETUP_SQ_AFF
  if (cpu >= 0)
  {
    params.flags |= IORING_SETUP_SQ_AFF;
    params.sq_thread_cpu = cpu;
  }
#endif

  ring._fd = io_uring_setup(entries, &params);
  if (ring._fd < 0)
  {
    perror("Failed to create io_uring_setup");
    return -1;
  }

  // submission queue size
  int sring_sz = params.sq_off.array + params.sq_entries * sizeof(unsigned);

  // completion queue size
  int cring_sz = params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);

  // IORING_FEAT_SINGLE_MMAP  combine mmaps
  if (params.features & IORING_FEAT_SINGLE_MMAP)
  {
    if (cring_sz > sring_sz)
    {
      sring_sz = cring_sz;
    }
    cring_sz = sring_sz;
  }

  // mmap some memory for submission and completion queues
  void *cq_ptr = nullptr;
  void *sq_ptr = mmap(0, sring_sz, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE,
                      ring._fd, IORING_OFF_SQ_RING);
  if (sq_ptr == MAP_FAILED)
  {
    perror("mmap");
    close(ring._fd);
    return -2;
  }

  if (params.features & IORING_FEAT_SINGLE_MMAP)
  {
    cq_ptr = sq_ptr;
  }
  else
  {
    /* Map in the completion queue ring buffer in older kernels separately */
    cq_ptr = mmap(0, cring_sz, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_POPULATE,
                  ring._fd, IORING_OFF_CQ_RING);
    if (cq_ptr == MAP_FAILED)
    {
      perror("mmap");
      close(ring._fd);
      return -3;
    }
  }

  ring._sq_ring.head = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(sq_ptr) + params.sq_off.head);
  ring._sq_ring.tail = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(sq_ptr) + params.sq_off.tail);
  ring._sq_ring.ring_mask = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(sq_ptr) + params.sq_off.ring_mask);
  ring._sq_ring.ring_entries = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(sq_ptr) + params.sq_off.ring_entries);
  ring._sq_ring.flags = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(sq_ptr) + params.sq_off.flags);
  ring._sq_ring.array = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(sq_ptr) + params.sq_off.array);

  // Map in the submission queue entries array
  ring._sqes = reinterpret_cast<struct io_uring_sqe *>(mmap(0, params.sq_entries * sizeof(struct io_uring_sqe),
                                                            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                                                            ring._fd, IORING_OFF_SQES));
  if (ring._sqes == MAP_FAILED)
  {
    perror("mmap");
    close(ring._fd);
    return -4;
  }

  ring._cq_ring.head = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(cq_ptr) + params.cq_off.head);
  ring._cq_ring.tail = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(cq_ptr) + params.cq_off.tail);
  ring._cq_ring.ring_mask = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(cq_ptr) + params.cq_off.ring_mask);
  ring._cq_ring.ring_entries = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(cq_ptr) + params.cq_off.ring_entries);
  ring._cq_ring.cqes = reinterpret_cast<struct io_uring_cqe *>(reinterpret_cast<char *>(cq_ptr) + params.cq_off.cqes);

  return 0;
}

void IOURingHelper::Drain(coypu_io_uring &ring, const std::function<void(int, uint64_t, int flags)> &cb)
{
  struct io_uring_cqe *cqe;
  unsigned head;

  head = *ring._cq_ring.head;

  do
  {
    read_barrier();

    if (head == *ring._cq_ring.tail)
      break;

    cqe = &ring._cq_ring.cqes[head & *ring._cq_ring.ring_mask];

    cb(cqe->res, cqe->user_data, cqe->flags);

    head++;
  } while (1);

  *ring._cq_ring.head = head;
  write_barrier();
}
