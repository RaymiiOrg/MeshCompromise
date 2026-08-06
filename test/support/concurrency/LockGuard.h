#pragma once

namespace concurrency
{

class Lock
{
  public:
    void lock() {}
    void unlock() {}
};

class LockGuard
{
  public:
    explicit LockGuard(Lock *) {}
};

} // namespace concurrency
