//
// Created by dkorah on 04/07/17.
//

#include "gtest/gtest.h"

struct x
{
    int a;
    int b;

    x(const int& a, const int& b) : a(a), b(b) {}
};

bool operator<(const x& a, const x& b)
{
    return (a.a != b.a) ? (a.a < b.a) : (a.b < b.b);
}

TEST(TestMisc, Basic)
{
    x a(1, 2);
    x b(2, 3);

    std::cout << "less: " << std::less<x>()(a, b) << std::endl;
}
