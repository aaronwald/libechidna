
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <string.h>
#include <string>

#include "echidna/mem.hpp"
#include "gtest/gtest.h"

using namespace coypu::mem;

TEST(MemTest, Test1)
{
  int node = MemManager::GetMaxNumaNode();
  ASSERT_TRUE(node >= 0);
}
