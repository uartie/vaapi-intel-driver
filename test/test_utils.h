/*
 * Copyright (C) 2016 Intel Corporation. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <chrono>
// #include <random>
#include <cstdlib>
#include <ctime>

template <typename T>
class RandomValueGenerator
{
public:
    RandomValueGenerator(const T& min, const T& max)
//         : dis(min, max)
        : min_(min), max_(max)
    { std::srand(std::time(0)); }

    const T operator()()
    {
//         static std::random_device rd;
//         static std::default_random_engine gen(rd());
//         return dis(gen);
//         T r =  min_ + std::rand() / (RAND_MAX / (max_ - min_ + 1) + 1);
        return T(std::rand() % (max_ + 1 - min_) + min_);
//         std::cout << int(r) << ",";
//         return r;
    }

private:
//     std::uniform_int_distribution<T> dis;
    T min_;
    T max_;
};

class Timer
{
public:
    typedef typename std::chrono::microseconds us;
    typedef typename std::chrono::milliseconds ms;
    typedef typename std::chrono::seconds       s;

    Timer()
        : m_start(std::chrono::steady_clock::now())
    { }

    template <typename T = std::chrono::microseconds>
    typename T::rep elapsed() const
    {
        std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now();
        return std::chrono::duration_cast<T>(
            now - m_start).count();
    }

    void reset()
    {
        m_start = std::chrono::steady_clock::now();
    }

private:
    std::chrono::steady_clock::time_point m_start;
};

#endif // TEST_UTILS_H
