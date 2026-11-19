#include <delivery/bench/BenchRunner.hpp>

#include <iomanip>
#include <utility>

namespace delivery::bench {
std::vector<unsigned> threadSweep(unsigned maxThreads) {
    std::vector<unsigned> counts;
    for (unsigned t = 1; t < maxThreads; t *= 2) {
        counts.push_back(t);
    }
    counts.push_back(maxThreads < 1 ? 1 : maxThreads);
    return counts;
}

BenchRunner::BenchRunner(std::ostream& csv, BenchOptions options)
    : csv_(csv), options_(std::move(options)) {
    csv_ << "application,size,version,repetition,ms,correct\n";
}

void BenchRunner::run(const BenchCase& benchCase) {
    if (!options_.only.empty() && options_.only != benchCase.application) {
        return;
    }
    for (std::size_t s = 0; s < benchCase.sizes.size(); ++s) {
        if (!options_.onlySize.empty() && options_.onlySize != benchCase.sizes[s]) {
            continue;
        }
        const std::vector<Version> versions = benchCase.prepare(s);
        if (options_.warmup) {
            for (const Version& version : versions) {
                version.run();
            }
        }
        for (int rep = 0; rep < options_.repetitions; ++rep) {
            for (const Version& version : versions) {
                writeRow(benchCase.application, benchCase.sizes[s], version.name, rep,
                         version.run());
            }
        }
    }
    csv_.flush();
}

std::size_t BenchRunner::rowsWritten() const noexcept {
    return rows_;
}

std::size_t BenchRunner::failures() const noexcept {
    return failures_;
}

void BenchRunner::writeRow(const std::string& application, const std::string& size,
                           const std::string& version, int repetition, const Measurement& m) {
    csv_ << application << ',' << size << ',' << version << ',' << repetition << ','
         << std::fixed << std::setprecision(4) << m.ms << ',' << (m.correct ? "yes" : "no")
         << '\n';
    ++rows_;
    if (!m.correct) {
        ++failures_;
    }
}
}
