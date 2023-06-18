
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
  IOCallbacks::cb_func_t test_func = [&out_flags](int fd, uint64_t, int flags)
  { out_flags = flags; };

  mgr.SetCallback(fd, IORING_OP_CLOSE, test_func);

  mgr.Fire(fd, IORING_OP_CLOSE, 0, 0, 7);
  ASSERT_EQ(out_flags, 7);
}
