#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>
#include <QApplication>

int main(int argc, char *argv[])
{
    Q_UNUSED(argc)
    Q_UNUSED(argv)

    testing::InitGoogleTest();

    return RUN_ALL_TESTS();
}
