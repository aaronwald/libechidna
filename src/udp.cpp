
#include "echidna/udp.hpp"

using namespace coypu::udp;

int UDPHelper::CreateUDPNonBlock()
{
  return ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
}
