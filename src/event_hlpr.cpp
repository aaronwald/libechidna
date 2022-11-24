
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/mman.h>
#include <string.h>
#include <iostream>
#include <linux/io_uring.h>

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>

#include "echidna/event_hlpr.hpp"

int io_uring_setup(unsigned int entries, struct io_uring_params *p)
{
  return syscall(__NR_io_uring_setup, entries, p);
}

int io_uring_enter(int ring_fd, unsigned int to_submit,
                   unsigned int min_complete, unsigned int flags)
{
  return (int)syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete,
                      flags, NULL, 0);
}

#define read_barrier() __asm__ __volatile__("" :: \
                                                : "memory")
#define write_barrier() __asm__ __volatile__("" :: \
                                                 : "memory")

using namespace coypu::event;

int EPollHelper::Create(int flags)
{
  // size is deprecated
  return ::epoll_create1(flags);
}

int EPollHelper::Add(int efd, int fd, struct epoll_event *event)
{
  //  std::cout << "Add " << fd << std::endl;
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
int IOURingHelper::Create(coypu_io_uring &ring, uint32_t entries)
{
  struct io_uring_params params;
  memset(&params, 0, sizeof(params));
  // params.sq_thread_cpu - set cpu
  // params.sq_thread_idle - idle milliseconds

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

  ring._sq_ring.head = static_cast<unsigned int *>(sq_ptr) + params.sq_off.head;
  ring._sq_ring.tail = static_cast<unsigned int *>(sq_ptr) + params.sq_off.tail;
  ring._sq_ring.ring_mask = static_cast<unsigned int *>(sq_ptr) + params.sq_off.ring_mask;
  ring._sq_ring.ring_entries = static_cast<unsigned int *>(sq_ptr) + params.sq_off.ring_entries;
  ring._sq_ring.flags = static_cast<unsigned int *>(sq_ptr) + params.sq_off.flags;
  ring._sq_ring.array = static_cast<unsigned int *>(sq_ptr) + params.sq_off.array;

  /* Map in the submission queue entries array */
  ring._sqes = static_cast<struct io_uring_sqe *>(mmap(0, params.sq_entries * sizeof(struct io_uring_sqe),
                                                       PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                                                       ring._fd, IORING_OFF_SQES));
  if (ring._sqes == MAP_FAILED)
  {
    perror("mmap");
    return -4;
  }

  ring._cq_ring.head = static_cast<unsigned int *>(cq_ptr) + params.cq_off.head;
  ring._cq_ring.tail = static_cast<unsigned int *>(cq_ptr) + params.cq_off.tail;
  ring._cq_ring.ring_mask = static_cast<unsigned int *>(cq_ptr) + params.cq_off.ring_mask;
  ring._cq_ring.ring_entries = static_cast<unsigned int *>(cq_ptr) + params.cq_off.ring_entries;
  ring._cq_ring.cqes = static_cast<io_uring_cqe *>(cq_ptr) + params.cq_off.cqes;

  return 0;
}

int IOURingHelper::Submit(coypu_io_uring &ring, int file_fd, char op_code, struct iovec *iovecs, uint32_t len, void *userdata)
{
  unsigned next_tail, tail, index;
  next_tail = tail = *ring._sq_ring.tail;
  next_tail++;
  read_barrier();
  index = tail & *ring._sq_ring.ring_mask;

  // add a submittion request
  struct io_uring_sqe *sqe = &ring._sqes[index];
  sqe->fd = file_fd;
  sqe->flags = 0;
  sqe->opcode = op_code;
  sqe->addr = (unsigned long)iovecs;
  sqe->len = len;
  sqe->off = 0;
  sqe->user_data = (unsigned long long)userdata;
  ring._sq_ring.array[index] = index;
  tail = next_tail;

  /* Update the tail so the kernel can see it. */
  if (*ring._sq_ring.tail != tail)
  {
    *ring._sq_ring.tail = tail;
    write_barrier();
  }

  /*
   * Tell the kernel we have submitted events with the io_uring_enter() system
   * call. We also pass in the IOURING_ENTER_GETEVENTS flag which causes the
   * io_uring_enter() call to wait until min_complete events (the 3rd param)
   * complete.
   * */
  int ret = io_uring_enter(ring._fd, 1, 1,
                           IORING_ENTER_GETEVENTS);
  if (ret < 0)
  {
    perror("io_uring_enter");
    return -1;
  }

  return 0;
}

// io_vecs cant go away
int IOURingHelper::SubmitReadv(coypu_io_uring &ring, int file_fd, struct iovec *iovecs, uint32_t len, void *userdata)
{
  return Submit(ring, file_fd, IORING_OP_READV, iovecs, len, userdata);
}

// io_vecs cant go away
int IOURingHelper::SubmitWritev(coypu_io_uring &ring, int file_fd, struct iovec *iovecs, uint32_t len, void *userdata)
{
  return Submit(ring, file_fd, IORING_OP_WRITEV, iovecs, len, userdata);
}

int SubmitZoo(coypu_io_uring &ring)
{
  (void)ring;
  return 0;
}

void IOURingHelper::ReadCompletion(coypu_io_uring &ring)
{
  struct io_uring_cqe *cqe __attribute__((unused));
  unsigned head;

  head = *ring._cq_ring.head;

  do
  {
    read_barrier();
    /*
     * Remember, this is a ring buffer. If head == tail, it means that the
     * buffer is empty.
     * */
    if (head == *ring._cq_ring.tail)
      break;

    /* Get the entry */
    cqe = &ring._cq_ring.cqes[head & *ring._cq_ring.ring_mask];
    // cqe->user_data
    // lambda? on_cqe(user_data);

    head++;
  } while (1);

  *ring._cq_ring.head = head;
  write_barrier();
}