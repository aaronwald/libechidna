#include <vector>
#if defined(__x86_64__)
#include <x86intrin.h>
#endif
#include <stdint.h>
#include "echidna/timer.hpp"

using namespace std;

#if defined(__x86_64__)
__attribute__((noinline)) int task(int iterations)
{
  volatile int i = 0;
  int x = 0;
  while (x < iterations)
  {
    x++;
  }
  i = x;
  return i;
}

void experiment(int task_size, int task_count, long long result __attribute__((unused)), std::vector<uint64_t> cycle_counter)
{
  // Reserve memory for counters in advance
  cycle_counter.resize(task_count, 0);

  // Execute task_count tasks of task_size
  for (int i = 0; i < task_count; i++)
  {
    uint32_t cycles_high, cycles_low, cycles_high1, cycles_low1;

    asm volatile("CPUID\n\t"
                 "RDTSC\n\t"
                 "mov %%edx, %0\n\t"
                 "mov %%eax, %1\n\t"
                 : "=r"(cycles_high), "=r"(cycles_low)::"%rax", "%rbx", "%rcx", "%rdx");

    // test
    result += task(task_size);

    asm volatile("RDTSCP\n\t"
                 "mov %%edx, %0\n\t"
                 "mov %%eax, %1\n\t"
                 "CPUID\n\t"
                 : "=r"(cycles_high1), "=r"(cycles_low1)::"%rax", "%rbx", "%rcx", "%rdx");

    uint64_t t1 = (((uint64_t)cycles_high << 32) | cycles_low);
    uint64_t t2 = (((uint64_t)cycles_high1 << 32) | cycles_low1);

    cycle_counter[i] = (t2 - t1);
  }
}
#endif
