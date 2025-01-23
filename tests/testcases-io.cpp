
#include "echidna/event_mgr.hpp"
#include "gtest/gtest.h"

#include <string>

using namespace coypu::event;

TEST(IOTest, Test1)
{
  int fd = 765;
  IOCallbackManager mgr;
  ASSERT_EQ(mgr.Register(fd), 0);

  int out_flags = 0;
  IOCallbacks::cb_func_t test_func = [&out_flags](int, int, int flags)
  { out_flags = flags; };

  mgr.SetCallback(fd, IORING_OP_CLOSE, test_func);

  mgr.Fire(fd, IORING_OP_CLOSE, 0, 7);
  ASSERT_EQ(out_flags, 7);

  std::shared_ptr<struct iovec> v1 = mgr.GetWriteCache(fd);
  ASSERT_EQ(v1->iov_len, IOCallbackManager::IOV_CACHE_BUF);

  auto buf = std::make_shared<char>();
  buf.reset(new char[IOCallbackManager::IOV_CACHE_BUF]);
  for (int i = 0; i < IOCallbackManager::IOV_CACHE_BUF; i++)
  {
    printf("%d / %d\n", i, IOCallbackManager::IOV_CACHE_BUF);
    buf.get()[i] = 'a';
  }

  for (int i = 0; i < v1->iov_len; i++)
  {
    reinterpret_cast<char *>(v1->iov_base)[i] = 'a';
  }
}
