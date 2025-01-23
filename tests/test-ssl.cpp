#include "echidna/event_hlpr.hpp"
#include "echidna/event_mgr.hpp"
#include "echidna/openssl_mgr.hpp"
#include "echidna/mem.hpp"
#include "echidna/tcp.hpp"
#include "echidna/string-util.hpp"
#include <string>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <unistd.h>

#include <sys/uio.h>
#include <sys/utsname.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <arpa/inet.h>

using namespace std;
using namespace coypu::event;
using namespace coypu::net::ssl;
using namespace coypu::tcp;
using namespace coypu::mem;
using namespace coypu::util;

typedef std::shared_ptr<spdlog::logger> LogType;
typedef OpenSSLManager<LogType> SSLType;

constexpr int LISTEN_PORT = 9988;
constexpr int probe_size = sizeof(io_uring_probe) + (sizeof(io_uring_probe_op) * IORING_OP_LAST);

int main(int argc, char **argv)
{
  static_assert(sizeof(IOCallback) == sizeof(uint64_t), "IOCallback size must be 64 bytes");
  spdlog::set_level(spdlog::level::debug);
  auto consoleLogger = spdlog::stdout_color_mt("console");

  struct utsname u;
  uname(&u);
  printf("You are running kernel version: %s\n", u.release);

  coypu_io_uring ring = {};
  int r = IOURingHelper::Create(ring);
  if (r < 0)
  {
    fprintf(stderr, "Failed to create io_uring\n");
    return EXIT_FAILURE;
  }

  // IORING_OP_LAST
  char probe_buf[probe_size];
  ::memset(probe_buf, 0, probe_size);
  r = io_uring_register(ring._fd, IORING_REGISTER_PROBE, &probe_buf, IORING_OP_LAST);
  if (r < 0)
  {
    perror("io_uring_register");
    return EXIT_FAILURE;
  }

  struct io_uring_probe *probe = reinterpret_cast<struct io_uring_probe *>(probe_buf);
  for (int i = 0; i < IORING_OP_LAST; ++i)
  {
    consoleLogger->debug("{} supported {}", i, probe->ops[i].flags & IO_URING_OP_SUPPORTED ? "true" : "false");
  }

  std::function<int(int)> set_write_ws = [&ring](int fd)
  {
    // with open ssl this is a no op.
    // we check pending to see when to schedule a writev
    return 0;
  };

  auto ssl_mgr = std::make_shared<SSLType>(consoleLogger, set_write_ws, "/etc/ssl/certs/");

  ssl_mgr->Init();
  ssl_mgr->SetCertificates("certificate.crt", "private.key");

  int sockFD = socket(AF_INET, SOCK_STREAM, 0);
  assert(sockFD > 0);

  consoleLogger->debug("SockFD[{}]", sockFD);

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(LISTEN_PORT);

  TCPHelper::SetReusePort(sockFD);
  TCPHelper::SetReuseAddr(sockFD);

  r = TCPHelper::BindIPV4(sockFD, &serv_addr);
  if (r < 0)
  {
    perror("bind");
    return EXIT_FAILURE;
  }
  r = TCPHelper::Listen(sockFD, 10);
  if (r < 0)
  {
    perror("listen");
    return EXIT_FAILURE;
  }
  consoleLogger->info("Listen:{}", LISTEN_PORT);

  struct sockaddr accept_addr;
  socklen_t accept_addr_len;
  struct IOCallback cb_accept(sockFD, IORING_OP_ACCEPT);
  r = IOURingHelper::SubmitAcceptNonBlockMulti(ring, sockFD, &accept_addr, &accept_addr_len, *reinterpret_cast<uint64_t *>(&cb_accept));
  if (r < 0)
  {
    perror("accept");
    return EXIT_FAILURE;
  }

  // IO Manager
  IOCallbackManager iom;
  IOBufManager iobuf_mgr(13, 4);
  iobuf_mgr.Init();

  // Submit buffers
  struct IOCallback cb_buffers(ring._fd, IORING_OP_PROVIDE_BUFFERS);
  r = iobuf_mgr.SubmitBuffers(ring, cb_buffers);
  if (r < 0)
  {
    perror("provide buffers");
    return EXIT_FAILURE;
  }

  // Callbacks
  IOCallbacks::cb_func_t onAccept = [&iom, &accept_addr, &ring, ssl_mgr, consoleLogger, &iobuf_mgr](int fd, int res, int flags)
  {
    consoleLogger->info("Accept fd={} res={} {}", fd, res, inet_ntoa(((struct sockaddr_in *)&accept_addr)->sin_addr));

    ssl_mgr->RegisterWithMemBIO(res, "localhost", false /* set to accept state for server*/);

    IOCallbacks::cb_func_t onRecv = [&iom, &accept_addr, &ring, ssl_mgr, consoleLogger, &iobuf_mgr](int fd, int res, int flags)
    {
      consoleLogger->debug("Readv res={0}", res);

      if (res > 0)
      {
        uint16_t buf_id = flags >> IORING_CQE_BUFFER_SHIFT;
        if (flags & IORING_CQE_F_BUFFER)
        {
          consoleLogger->debug("Group:{0} Buffer:{1}", iobuf_mgr.GetGroupID(), buf_id);
        }
        else
        {
          printf("Expecting buffer flag");
          return;
        }

        iobuf_mgr.IncUsedCount();
        if (iobuf_mgr.IsFull())
        {
          consoleLogger->info("Used all buffers");
          iobuf_mgr.Reset();

          struct IOCallback cb_buffers(ring._fd, IORING_OP_PROVIDE_BUFFERS);
          int r = iobuf_mgr.SubmitBuffers(ring, cb_buffers);
          if (r < 0)
          {
            perror("SubmitProvideBuffers");
          }

          // start read again
          struct IOCallback cb_recv(fd, IORING_OP_RECV);
          r = IOURingHelper::SubmitRecvMulti(ring, fd, iobuf_mgr.GetGroupID(), *reinterpret_cast<uint64_t *>(&cb_recv));
          if (r < 0)
          {
            consoleLogger->error("SubmitRecvMulti");
          }
        }

        char *offset = iobuf_mgr.GetBuffers() + (iobuf_mgr.GetBufSize() * buf_id);
        int r = ssl_mgr->PushReadBIO(fd, offset, res);

        if (!ssl_mgr->IsInitFinished(fd))
        {
          int r = ssl_mgr->DoHandshake(fd);
          if (r != 0 && r != 1)
          {
            consoleLogger->error("Handshake error {0}", r);
          }
        }
        else if (ssl_mgr->PendingRead(fd) > 0)
        {
          char read_buf[1025] = {0};
          struct iovec v[1];
          v[0].iov_base = read_buf;
          v[0].iov_len = 1024;

          int r = ssl_mgr->ReadvNonBlock(fd, &v[0], 1);
          if (r > 0)
          {
            StringUtil::Hexdump(read_buf, r);

            // echo back
            v[0].iov_len = r;
            ssl_mgr->WritevNonBlock(fd, &v[0], 1);
          }
          else
          {
            consoleLogger->error("Read error {0}", r);
          }
        }

        if (ssl_mgr->PendingWrite(fd) > 0)
        {
          std::shared_ptr<struct iovec> outv = iom.GetWriteCache(fd);

          outv->iov_len = IOCallbackManager::IOV_CACHE_BUF;
          int r = ssl_mgr->DrainWriteBIO(fd, outv.get(), 1);
          if (r > 0)
          {
            outv->iov_len = r;
            struct IOCallback cb_writev(fd, IORING_OP_WRITEV);
            IOURingHelper::SubmitWritev(ring, fd, outv.get(), 1, *reinterpret_cast<uint64_t *>(&cb_writev));
          }
          else
          {
            consoleLogger->error("DrainWriteBIO {0}", r);
          }
        }

        // only submit if this is no longer set, but with multi shot it should be
        if (!(flags & IORING_CQE_F_MORE))
        {
          struct IOCallback cb_recv(fd, IORING_RECV_MULTISHOT);
          int r = IOURingHelper::SubmitRecvMulti(ring, fd, iobuf_mgr.GetGroupID(), *reinterpret_cast<uint64_t *>(&cb_recv));
          if (r < 0)
          {
            consoleLogger->error("Failed to submit recv");
          }
        }
      }
      else if (res == 0)
      {
        // closed?
      }
      else
      {
        if (-res == ENOBUFS)
        {
        }
        // error
        consoleLogger->error("Readv res={0} {1}", -res, strerror(-res));
      }
    };

    IOCallbacks::cb_func_t onWritev = [consoleLogger, ssl_mgr, &iom, &ring](int fd, int res, int flags)
    {
      consoleLogger->debug("Writev res={0}", res);
      if (ssl_mgr->PendingWrite(fd) > 0)
      {
        std::shared_ptr<struct iovec> outv = iom.GetWriteCache(fd);

        outv->iov_len = IOCallbackManager::IOV_CACHE_BUF;
        int r = ssl_mgr->DrainWriteBIO(fd, outv.get(), 1);
        if (r > 0)
        {
          outv->iov_len = r;
          struct IOCallback cb_writev(fd, IORING_OP_WRITEV);
          IOURingHelper::SubmitWritev(ring, fd, outv.get(), 1, *reinterpret_cast<uint64_t *>(&cb_writev));
        }
        else
        {
          consoleLogger->error("DrainWriteBIO {0}", r);
        }
      }
    };

    // set our first read request (should be multishot but not supported yet)
    struct IOCallback cb_recv(res, IORING_OP_RECV);
    iom.Register(res);
    iom.SetCallback(res, IORING_OP_RECV, onRecv);
    iom.SetCallback(res, IORING_OP_WRITEV, onWritev);
    int r = IOURingHelper::SubmitRecvMulti(ring, res, iobuf_mgr.GetGroupID(), *reinterpret_cast<uint64_t *>(&cb_recv));
    if (r < 0)
    {
      consoleLogger->error("Failed to submit recv");
    }
  };

  IOCallbacks::cb_func_t onBuffers = [consoleLogger](int, int, int)
  {
    consoleLogger->debug("Buffers provided");
  };

  // accept on the socket
  iom.Register(sockFD);
  iom.SetCallback(sockFD, IORING_OP_ACCEPT, onAccept);

  // buffers are on the ring
  iom.Register(ring._fd);
  iom.SetCallback(ring._fd, IORING_OP_PROVIDE_BUFFERS, onBuffers);

  auto process_ring_completions = [consoleLogger, &iom](int res, uint64_t userdata, int flags)
  {
    struct IOCallback cb = *(struct IOCallback *)&userdata;
    consoleLogger->debug("Completion fd={} res={} flags={}", cb._fd, res, flags);
    iom.Fire(cb._fd, static_cast<io_uring_op>(cb._cb), res, flags);
  };

  bool done = false;
  while (!done)
    IOURingHelper::Drain(ring, process_ring_completions);

  close(ring._fd);

  return EXIT_SUCCESS;
}