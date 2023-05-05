
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
int IOURingHelper::Create(coypu_io_uring &ring, uint32_t entries, int32_t cpu)
{
  struct io_uring_params params;
  memset(&params, 0, sizeof(params));
  // params.sq_thread_cpu - set cpu
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

  // // compute offsets for later
  // ring._sq_ring.head = reinterpret_cast<unsigned *>(((char *)sq_ptr) + params.sq_off.head);
  // ring._sq_ring.tail = reinterpret_cast<unsigned int *>(((char *)sq_ptr) + params.sq_off.tail);
  // ring._sq_ring.ring_mask = reinterpret_cast<unsigned int *>(((char *)sq_ptr) + params.sq_off.ring_mask);
  // ring._sq_ring.ring_entries = reinterpret_cast<unsigned int *>(((char *)sq_ptr) + params.sq_off.ring_entries);
  // ring._sq_ring.flags = reinterpret_cast<unsigned int *>(((char *)sq_ptr) + params.sq_off.flags);
  // ring._sq_ring.array = reinterpret_cast<unsigned int *>(((char *)sq_ptr) + params.sq_off.array);

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

  // // compute offsets for later
  // ring._cq_ring.head = reinterpret_cast<unsigned int *>(((char *)cq_ptr) + params.cq_off.head);
  // ring._cq_ring.tail = reinterpret_cast<unsigned int *>(((char *)cq_ptr) + params.cq_off.tail);
  // ring._cq_ring.ring_mask = reinterpret_cast<unsigned int *>(((char *)cq_ptr) + params.cq_off.ring_mask);
  // ring._cq_ring.ring_entries = reinterpret_cast<unsigned int *>(((char *)cq_ptr) + params.cq_off.ring_entries);
  // ring._cq_ring.cqes = reinterpret_cast<io_uring_cqe *>(((char *)cq_ptr) + params.cq_off.cqes);

  ring._cq_ring.head = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(cq_ptr) + params.cq_off.head);
  ring._cq_ring.tail = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(cq_ptr) + params.cq_off.tail);
  ring._cq_ring.ring_mask = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(cq_ptr) + params.cq_off.ring_mask);
  ring._cq_ring.ring_entries = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(cq_ptr) + params.cq_off.ring_entries);
  ring._cq_ring.cqes = reinterpret_cast<struct io_uring_cqe *>(reinterpret_cast<char *>(cq_ptr) + params.cq_off.cqes);

  return 0;
}

int add_to_sqe(coypu_io_uring &ring, struct io_uring_sqe *in_sqe)
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
    std::cout << "Enter wakerup" << std::endl;
    int ret = io_uring_enter(ring._fd, 1, 0, IORING_ENTER_SQ_WAKEUP, nullptr /*sig*/);
    if (ret < 0)
    {
      perror("io_uring_enter");
      return 1;
    }
  }

  return 0;
}

int IOURingHelper::Submit(coypu_io_uring &ring, int file_fd, char op_code, void *addr, uint32_t len, uint64_t userdata)
{
  unsigned next_tail, tail, index;
  next_tail = tail = *ring._sq_ring.tail;
  next_tail++;
  read_barrier();
  index = tail & *ring._sq_ring.ring_mask;

  // TODO check there is space?

  // add a submittion request
  struct io_uring_sqe *sqe = &ring._sqes[index];
  sqe->fd = file_fd;
  sqe->flags = 0;
  sqe->opcode = op_code;
  sqe->addr = (unsigned long)addr;
  sqe->len = len;
  sqe->off = 0;
  sqe->user_data = userdata;
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
  int to_submit = 1;
  int min_complete = 0; // dont complete and get events,

  unsigned flags = __atomic_load_n(ring._sq_ring.flags, __ATOMIC_RELAXED);
  if (flags & IORING_SQ_NEED_WAKEUP)
  {
    // if this returns ok, then it's safe to assume
    int consumed = io_uring_enter(ring._fd, to_submit, min_complete, 0, nullptr /* sig*/);
    printf("io_uring_enter consumed %d\n", consumed);
    if (consumed < 0)
    {
      perror("io_uring_enter");
      return -1;
    }
  }

  return 0;
}

// for our bip buf we can submit a readv on our underlying bipbuf code
int IOURingHelper::SubmitNop(coypu_io_uring &ring, uint64_t userdata)
{
  struct io_uring_sqe sqe;
  ::memset(&sqe, 0, sizeof(sqe));
  sqe.opcode = IORING_OP_NOP;                   // https://manpages.debian.org/unstable/liburing-dev/io_uring_enter.2.en.html
  sqe.user_data = (unsigned long long)userdata; // user data
  return add_to_sqe(ring, &sqe);
}

int IOURingHelper::SubmitTimeout(coypu_io_uring &ring, struct timespec *ts, uint64_t userdata)
{
  return Submit(ring, 0, IORING_OP_TIMEOUT, ts, 1, userdata);
}

// io_vecs cant go away
int IOURingHelper::SubmitReadv(coypu_io_uring &ring, int file_fd, struct iovec *iovecs, uint32_t len, uint64_t userdata)
{
  return IOURingHelper::Submit(ring, file_fd, IORING_OP_READV, iovecs, len, userdata);
}

// io_vecs cant go away
int IOURingHelper::SubmitWritev(coypu_io_uring &ring, int file_fd, struct iovec *iovecs, uint32_t len, uint64_t userdata)
{
  return IOURingHelper::Submit(ring, file_fd, IORING_OP_WRITEV, iovecs, len, userdata);
}

void IOURingHelper::DrainCompletion(coypu_io_uring &ring, const std::function<void(uint64_t)> &cb)
{
  struct io_uring_cqe *cqe;
  unsigned head;

  head = *ring._cq_ring.head;

  do
  {
    read_barrier();

    // empty
    if (head == *ring._cq_ring.tail)
      break;

    /* Get the entry */
    cqe = &ring._cq_ring.cqes[head & *ring._cq_ring.ring_mask];
    // TODO store the cb in the user data
    cb(cqe->user_data);

    head++;
  } while (1);

  *ring._cq_ring.head = head;
  write_barrier();
}