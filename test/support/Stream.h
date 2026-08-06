#pragma once

#include <cstddef>
#include <cstdint>

class Stream
{
  public:
    virtual ~Stream() = default;
    virtual int read() { return -1; }
    virtual size_t readBytes(uint8_t *, size_t) { return 0; }
    virtual size_t write(const uint8_t *, size_t length) { return length; }
    virtual size_t write(uint8_t) { return 1; }
    virtual size_t print(const char *) { return 0; }
    virtual size_t print(char) { return 0; }
    virtual size_t print(int) { return 0; }
    virtual size_t println(const char *) { return 0; }
    virtual size_t println() { return 0; }
    virtual size_t printf(const char *, ...) { return 0; }
};
