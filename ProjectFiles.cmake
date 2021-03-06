# Copyright (c) <year> <author> (<email>) Distributed under the MIT License. See accompanying file
# LICENSE.md or copy at http://opensource.org/licenses/MIT

# Set project source files.
set(SRC "${SRC_PATH}/ThreadRegistry.cpp" "${SRC_PATH}/barrier.cpp" "${SRC_PATH}/TraceLog.cpp")

set(BENCH_SRC_PATH "${SRC_PATH}/benchmark")
set(BENCH_SRC "${BENCH_SRC_PATH}/benchMutex.cpp")
set(BENCH2_SRC "${BENCH_SRC_PATH}/benchMutex2.cpp")
set(FAIRTEST_SRC "${SRC_PATH}/fairnessTest.cpp")

# Set project benchmark files. set(BENCHMARK_SRC "${SRC_PATH}/benchmark.cpp")

# Set project test source files.
set(TEST_SRC
    "${TEST_SRC_PATH}/testBase.cpp"
    "${TEST_SRC_PATH}/testMutex.cpp"
    "${TEST_SRC_PATH}/testFairMutex.cpp")
