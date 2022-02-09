#pragma once

#include <string.h>
#include <memory>

namespace coypu::log
{
  class coypu_log
  {
  public:
    template <typename T>
    static void perror(std::shared_ptr<T> &logger, int errnum, const char *msg)
    {
      char buf[1024] = {};
      strerror_r(errnum, buf, 1024);
      logger->error("[{0}] ({1}): {2}", errnum, buf, msg);
    }

  private:
    coypu_log() = delete;
  };
}
