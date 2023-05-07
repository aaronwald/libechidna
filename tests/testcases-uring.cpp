
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

TEST(URingTest, TestNop1)
{
  coypu_io_uring ring = {};

  int r = IOURingHelper::Create(ring);
  ASSERT_EQ(r, 0);
  ASSERT_TRUE(ring._fd > 0);

  r = IOURingHelper::SubmitNop(ring, 123);
  ASSERT_EQ(r, 0);

  bool match = false;
  auto cb_check = [&match](int res, uint64_t userdata)
  {
    ASSERT_EQ(userdata, 123);
    match = true;
  };

  while (!match)
    IOURingHelper::Drain(ring, cb_check);

  ASSERT_TRUE(match);
  close(ring._fd);
}