#include "echidna/event_hlpr.hpp"
#include "echidna/openssl_mgr.hpp"
#include "echidna/mem.hpp"
#include "echidna/tcp.hpp"
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

typedef std::shared_ptr<spdlog::logger> LogType;
typedef OpenSSLManager<LogType> SSLType;

enum TEST_CALLBACK
{
  CB_RECV,
  CB_WRITEV,
  CB_ACCEPT,
  CB_BUFFERS
};

constexpr int LISTEN_PORT = 9988;

void hexdump(void *ptr, int buflen)
{
  unsigned char *buf = (unsigned char *)ptr;
  int i, j;
  for (i = 0; i < buflen; i += 16)
  {
    printf("%06x: ", i);
    for (j = 0; j < 16; j++)
      if (i + j < buflen)
        printf("%02x ", buf[i + j]);
      else
        printf("   ");
    printf(" ");
    for (j = 0; j < 16; j++)
      if (i + j < buflen)
        printf("%c", isprint(buf[i + j]) ? buf[i + j] : '.');
    printf("\n");
  }
}

constexpr int probe_size = sizeof(io_uring_probe) + (sizeof(io_uring_probe_op) * IORING_OP_LAST);

int main(int argc, char **argv)
{
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
    consoleLogger->info("{} supported {}", i, probe->ops[i].flags & IO_URING_OP_SUPPORTED ? "true" : "false");
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
  r = IOURingHelper::SubmitAcceptNonBlockMulti(ring, sockFD, &accept_addr, &accept_addr_len, CB_ACCEPT);
  if (r < 0)
  {
    perror("accept");
    return EXIT_FAILURE;
  }

  // setup buffers
  uint32_t buf_size = 4096;
  uint16_t buf_group_id = 13;
  int num_bufs = 4;
  void *buffers = nullptr;
  size_t pageSize = MemManager::GetPageSize();
  if ((buf_size * num_bufs) % pageSize)
  {
    printf("Buffer size * num buffers must be a multiple of page size %zu\n", pageSize);
    return EXIT_FAILURE;
  }

  r = posix_memalign(&buffers, 4096, buf_size * num_bufs);

  if (r != 0)
  {
    perror("posix_memalign");
    return EXIT_FAILURE;
  }
  r = IOURingHelper::SubmitProvideBuffers(ring,
                                          buffers,
                                          num_bufs,
                                          buf_size,
                                          buf_group_id,
                                          CB_BUFFERS);
  if (r < 0)
  {
    perror("provide buffers");
    return EXIT_FAILURE;
  }
  else
  {
    consoleLogger->info("Provide buffers:{}", num_bufs);
  }

  char out_data[1024];
  struct iovec out_iov[1] = {{out_data, 1024}};

  auto process_ring_completions = [&accept_addr, &ring, &out_iov, ssl_mgr, consoleLogger, &buffers, num_bufs, buf_group_id, buf_size](int res, uint64_t userdata, int flags)
  {
    static int cb_fd = 0;
    static int used_buf_count = 0;

    switch (userdata)
    {
    case CB_ACCEPT:
    {
      consoleLogger->info("Accept fd={0} {1}", res, inet_ntoa(((struct sockaddr_in *)&accept_addr)->sin_addr));
      cb_fd = res;

      ssl_mgr->RegisterWithMemBIO(cb_fd, "localhost", false /* set to accept state for server*/);

      // set our first read request (should be multishot but not supported yet)
      int r = IOURingHelper::SubmitRecvMulti(ring, cb_fd, buf_group_id, CB_RECV);
      if (r < 0)
      {
        consoleLogger->error("Failed to submit recv");
      }
    }
    break;

    case CB_RECV:
    {
      if (res > 0)
      {
        uint16_t buf_id = flags >> IORING_CQE_BUFFER_SHIFT;
        if (flags & IORING_CQE_F_BUFFER)
        {
          consoleLogger->info("Group:{0} Buffer:{1}", buf_group_id, buf_id);
        }
        else
        {
          printf("Expecting buffer flag");
          return;
        }

        ++used_buf_count;
        if (used_buf_count == num_bufs)
        {
          consoleLogger->info("Used all buffers");
          used_buf_count = 0;

          int r = IOURingHelper::SubmitProvideBuffers(ring,
                                                      buffers,
                                                      num_bufs,
                                                      buf_size,
                                                      buf_group_id,
                                                      CB_BUFFERS);
          if (r < 0)
          {
            perror("SubmitProvideBuffers");
          }

          // start read again
          r = IOURingHelper::SubmitRecvMulti(ring, cb_fd, buf_group_id, CB_RECV);
          if (r < 0)
          {
            consoleLogger->error("SubmitRecvMulti");
          }
        }

        char *offset = reinterpret_cast<char *>(buffers) + (buf_size * buf_id);
        int r = ssl_mgr->PushReadBIO(cb_fd, offset, res);

        if (!ssl_mgr->IsInitFinished(cb_fd))
        {
          int r = ssl_mgr->DoHandshake(cb_fd);
          if (r != 0 && r != 1)
          {
            consoleLogger->error("Handshake error {0}", r);
          }
        }
        else if (ssl_mgr->PendingRead(cb_fd) > 0)
        {
          char read_buf[1025] = {0};
          struct iovec v[1];
          v[0].iov_base = read_buf;
          v[0].iov_len = 1024;

          int r = ssl_mgr->ReadvNonBlock(cb_fd, &v[0], 1);
          if (r > 0)
          {
            hexdump(read_buf, r);

            // echo back
            v[0].iov_len = r;
            ssl_mgr->WritevNonBlock(cb_fd, &v[0], 1);
          }
          else
          {
            consoleLogger->error("Read error {0}", r);
          }
        }

        if (ssl_mgr->PendingWrite(cb_fd) > 0)
        {
          int r = ssl_mgr->DrainWriteBIO(cb_fd, out_iov, 1);
          if (r > 0)
          {
            out_iov->iov_len = r;
            IOURingHelper::SubmitWritev(ring, cb_fd, &out_iov[0], 1, CB_WRITEV);
          }
          else
          {
            consoleLogger->error("DrainWriteBIO {0}", r);
          }
        }

        // only submit if this is no longer set, but with multi shot it should be
        if (!(flags & IORING_CQE_F_MORE))
        {
          int r = IOURingHelper::SubmitRecvMulti(ring, cb_fd, buf_group_id, CB_RECV);
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
    }
    break;

    case CB_WRITEV:
    {
      if (ssl_mgr->PendingWrite(cb_fd) > 0)
      {
        int r = ssl_mgr->DrainWriteBIO(cb_fd, out_iov, 1);
        if (r > 0)
        {
          out_iov->iov_len = r;
          IOURingHelper::SubmitWritev(ring, cb_fd, &out_iov[0], 1, CB_WRITEV);
        }
        else
        {
          consoleLogger->error("DrainWriteBIO {0}", r);
        }
      }
    }
    break;

    case CB_BUFFERS:
    {
      consoleLogger->info("Buffers provided");
    }
    break;

    default:
      consoleLogger->error("Unknown callback {0}", userdata);
      break;
    }
  };

  bool done = false;
  while (!done)
    IOURingHelper::Drain(ring, process_ring_completions);

  close(ring._fd);

  return EXIT_SUCCESS;
}