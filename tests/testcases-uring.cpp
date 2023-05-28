#include "gtest/gtest.h"
#include "echidna/event_hlpr.hpp"
#include "echidna/openssl_mgr.hpp"
#include <string>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

using namespace coypu::event;
using namespace coypu::net::ssl;

TEST(URingTest, Test1)
{
  coypu_io_uring ring = {};

  int r = IOURingHelper::Create(ring);
  ASSERT_TRUE(r == 0);
  ASSERT_TRUE(ring._fd > 0);

  close(ring._fd);
}

TEST(URingTest, TestNop1)
{
  coypu_io_uring ring = {};

  int r = IOURingHelper::Create(ring);
  ASSERT_EQ(r, 0);
  ASSERT_TRUE(ring._fd > 0);

  r = IOURingHelper::SubmitNop(ring, 123);
  ASSERT_EQ(r, 0);

  bool match = false;
  auto cb_check = [&match](int res, uint64_t userdata, int)
  {
    ASSERT_EQ(userdata, 123);
    match = true;
  };

  while (!match)
    IOURingHelper::Drain(ring, cb_check);

  ASSERT_TRUE(match);
  close(ring._fd);
}

TEST(UringTest, TestPipe1)
{
  coypu_io_uring ring = {};

  int r = IOURingHelper::Create(ring);
  ASSERT_EQ(r, 0);
  ASSERT_TRUE(ring._fd > 0);

  int pipefd[2];
  r = pipe2(pipefd, O_NONBLOCK);
  ASSERT_EQ(r, 0);

  char buf[1024] = {0};
  struct iovec iov1 = {.iov_base = buf, .iov_len = sizeof(buf)};

  char buf2[1024] = {0};
  strcpy(buf2, "hello");
  struct iovec iov2 = {.iov_base = buf2, .iov_len = 5};

  r = IOURingHelper::SubmitWritev(ring, pipefd[1], &iov2, 1, 124);
  r = IOURingHelper::SubmitReadv(ring, pipefd[0], &iov1, 1, 123);

  bool match = false;
  auto cb_check = [&match](int res, uint64_t userdata, int)
  {
    if (userdata == 123)
      match = true;
  };

  while (!match)
    IOURingHelper::Drain(ring, cb_check);

  ASSERT_TRUE(match);
  ASSERT_EQ(strncmp(buf, buf2, 5), 0);

  close(pipefd[0]);
  close(pipefd[1]);
  close(ring._fd);
}

typedef std::shared_ptr<spdlog::logger> LogType;
typedef OpenSSLManager<LogType> SSLType;

TEST(UringTest, TestPipe2)
{
  coypu_io_uring ring = {};
  std::function<int(int)> set_write_ws = [](int fd) -> int
  {
    return 0;
  };

  spdlog::set_level(spdlog::level::debug);
  auto consoleLogger = spdlog::stdout_color_mt("console");
  auto ssl_mgr = std::make_shared<SSLType>(consoleLogger, set_write_ws, "/etc/ssl/certs/");
  ssl_mgr->Init();

  int r = IOURingHelper::Create(ring);
  ASSERT_EQ(r, 0);
  ASSERT_TRUE(ring._fd > 0);

  int pipefd[2];
  r = pipe2(pipefd, O_NONBLOCK);
  ASSERT_EQ(r, 0);

  char in_buf[1024] = {0};
  struct iovec iov1 = {.iov_base = in_buf, .iov_len = sizeof(in_buf)};

  char buf2[1024] = {0};
  strcpy(buf2, "hello");
  struct iovec iov2 = {.iov_base = buf2, .iov_len = 5};

  ssl_mgr->RegisterWithMemBIO(pipefd[0], "localhost", true);
  ssl_mgr->RegisterWithMemBIO(pipefd[1], "localhost", true);

  r = IOURingHelper::SubmitWritev(ring, pipefd[1], &iov2, 1, 124);
  r = IOURingHelper::SubmitReadv(ring, pipefd[0], &iov1, 1, 123);

  bool match = false;
  auto cb_check = [&match](int res, uint64_t userdata, int)
  {
    if (userdata == 123)
      match = true;
  };

  while (!match)
    IOURingHelper::Drain(ring, cb_check);

  ASSERT_TRUE(match);
  ASSERT_EQ(strncmp(in_buf, buf2, 5), 0);

  close(pipefd[0]);
  close(pipefd[1]);
  close(ring._fd);
}