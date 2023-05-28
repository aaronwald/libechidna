#include "echidna/event_hlpr.hpp"
#include "echidna/openssl_mgr.hpp"
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

typedef std::shared_ptr<spdlog::logger> LogType;
typedef OpenSSLManager<LogType> SSLType;

enum TEST_CALLBACK
{
  CB_RECV,
  CB_WRITEV,
  CB_ACCEPT
};

constexpr int LISTEN_PORT = 9988;

int main(int argc, char **argv)
{
  struct utsname u;
  uname(&u);
  printf("You are running kernel version: %s\n", u.release);

  std::function<int(int)> set_write_ws = [](int fd)
  {
    return 0;
  };
  spdlog::set_level(spdlog::level::debug);
  auto consoleLogger = spdlog::stdout_color_mt("console");
  auto ssl_mgr = std::make_shared<SSLType>(consoleLogger, set_write_ws, "/etc/ssl/certs/");
  ssl_mgr->Init();

  coypu_io_uring ring = {};
  int r = IOURingHelper::Create(ring);
  if (r < 0)
  {
    fprintf(stderr, "Failed to create io_uring\n");
    return EXIT_FAILURE;
  }

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
  std::cout << "Listen:" << LISTEN_PORT << std::endl;

  struct sockaddr accept_addr;
  socklen_t accept_addr_len;
  r = IOURingHelper::SubmitAcceptNonBlockMulti(ring, sockFD, &accept_addr, &accept_addr_len, CB_ACCEPT);

  // TODO Setup buffer manager for the connections
  char in_data[1024];

  struct iovec in_iov[1] = {{in_data, 1024}};

  auto process_ring_completions = [&accept_addr, &ring, &in_iov, ssl_mgr](int res, uint64_t userdata, int flags)
  {
    static int cb_fd = 0;

    switch (userdata)
    {
    case CB_ACCEPT:
    {
      printf("Accept fd=%d %s\n", res, inet_ntoa(((struct sockaddr_in *)&accept_addr)->sin_addr));
      cb_fd = res;

      ssl_mgr->RegisterWithMemBIO(cb_fd, "localhost", false);
      int acceptr = ssl_mgr->Accept(cb_fd);
      printf("%d\n", acceptr);

      IOURingHelper::SubmitReadv(ring, cb_fd, &in_iov[0], 1, CB_RECV);
    }
    break;

    case CB_RECV:
    {
      if (res > 0)
      {
        // call push read bio
        printf("SubmitRecv fd=%d\n", cb_fd);
        ssl_mgr->PushReadBIO(cb_fd, in_iov, 1);

        if (!(flags & IORING_CQE_F_MORE))
        {
          IOURingHelper::SubmitReadv(ring, cb_fd, &in_iov[0], 1, CB_RECV);
        }
      }
      else if (res == 0)
      {
        // closed?
      }
      else
      {
        // error
        printf("Readv fd=%d %s\n", res, strerror(-res));
      }
    }
    break;
    }
  };

  bool done = false;
  while (!done)
    IOURingHelper::Drain(ring, process_ring_completions);

  close(ring._fd);

  return EXIT_SUCCESS;
}