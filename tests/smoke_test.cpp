#include <ave/time.hpp>

#if defined(AVE_USE_GTEST)
#include <gtest/gtest.h>

TEST(TimeTest, RationalInitializationAndReduction) {
    ave::Rational r1(24, 48);
    EXPECT_EQ(r1.numerator(), 1);
    EXPECT_EQ(r1.denominator(), 2);
    EXPECT_DOUBLE_EQ(r1.to_seconds(), 0.5);
}

TEST(TimeTest, RationalAdditionAndSubtraction) {
    ave::Rational r1(1, 24);
    ave::Rational r2(1, 24);
    ave::Rational sum = r1 + r2;
    EXPECT_EQ(sum.numerator(), 1);
    EXPECT_EQ(sum.denominator(), 12);

    ave::Rational diff = sum - r1;
    EXPECT_EQ(diff, r1);
}

TEST(TimeTest, FrameConversion) {
    ave::Rational rate24(24, 1);
    ave::Rational oneSecond(1, 1);
    EXPECT_EQ(oneSecond.to_frames(rate24), 24);

    ave::Rational halfSecond(1, 2);
    EXPECT_EQ(halfSecond.to_frames(rate24), 12);
}

#else
#include <cassert>
#include <iostream>

int main() {
    std::cout << "Running AVE Smoke Tests (Standalone fallback mode)...\n";
    ave::Rational r1(24, 48);
    assert(r1.numerator() == 1);
    assert(r1.denominator() == 2);
    assert(r1.to_seconds() == 0.5);

    ave::Rational rate24(24, 1);
    ave::Rational oneSecond(1, 1);
    assert(oneSecond.to_frames(rate24) == 24);

    std::cout << "All AVE smoke tests passed successfully.\n";
    return 0;
}
#endif

