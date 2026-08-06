#include <gtest/gtest.h>

namespace meshcompromise
{
unsigned long hostMillis = 0;
}

unsigned long millis()
{
    return meshcompromise::hostMillis;
}

unsigned long micros()
{
    return meshcompromise::hostMillis * 1000;
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
