#pragma once

#include <cstddef>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace delivery::bench {
struct Measurement {
    double ms = 0.0;
    bool correct = false;
};

struct Version {
    std::string name;
    std::function<Measurement()> run;
};

struct BenchCase {
    std::string application;
    std::vector<std::string> sizes;
    std::function<std::vector<Version>(std::size_t sizeIndex)> prepare;
};

struct BenchOptions {
    int repetitions = 10;
    bool warmup = true;
    std::string only;
};

std::vector<unsigned> threadSweep(unsigned maxThreads);

class BenchRunner {
public:
    BenchRunner(std::ostream& csv, BenchOptions options);

    void run(const BenchCase& benchCase);
    std::size_t rowsWritten() const noexcept;
    std::size_t failures() const noexcept;

private:
    void writeRow(const std::string& application, const std::string& size,
                  const std::string& version, int repetition, const Measurement& m);

    std::ostream& csv_;
    BenchOptions options_;
    std::size_t rows_ = 0;
    std::size_t failures_ = 0;
};
}
