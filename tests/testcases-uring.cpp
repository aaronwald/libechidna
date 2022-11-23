
#include "gtest/gtest.h"
#include "echidna/event_hlpr.hpp"

#include <string>

using namespace coypu::event;

TEST(URingTest, Test1)
{
  coypu_io_uring ring = {};

  int r = IOURingHelper::Create(ring);
  ASSERT_TRUE(r == 0);
  ASSERT_TRUE(ring._fd > 0);

  close(ring._fd);
}