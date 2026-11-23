#pragma once

#include "harness/client_driver.hpp"
#include "harness/sim_harness.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <utility>

namespace slick::sim::integration {

struct Paths {
    std::string sim_binary;
    std::string client_binary;
    std::string config_template;
    std::string work_directory;
};

/// Reads `--sim`, `--client`, `--config` and `--workdir` from the arguments
/// gtest left behind, with environment variables as a fallback so the binary can
/// also be run by hand.
Paths parsePaths(int argc, char** argv);

/// One simulator and three trading clients, started before the first test and torn down after the
/// last.
///
/// Record startup failures so tests can report them with the simulator log. Throwing from the
/// environment would prevent gtest from running any tests.
class CoinbaseIntegrationEnvironment : public ::testing::Environment {
public:
    explicit CoinbaseIntegrationEnvironment(Paths paths) : paths_(std::move(paths)) {}

    void SetUp() override;
    void TearDown() override;

    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    const Paths& paths() const { return paths_; }
    SimHarness& sim() { return sim_; }
    const SimHarness& sim() const { return sim_; }
    /// alice, bob or carol.
    ClientDriver& client(const std::string& user);
    /// Every started client, in alice, bob, carol order.
    std::array<ClientDriver, 3>& clients() { return clients_; }

private:
    Paths paths_;
    SimHarness sim_;
    std::array<ClientDriver, 3> clients_;
    bool ok_ = false;
    std::string error_;
};

/// The environment registered by main.cpp.
CoinbaseIntegrationEnvironment& env();

/// Registers the environment; called from main.cpp.
void setEnvironment(CoinbaseIntegrationEnvironment* environment);

#define REQUIRE_ENVIRONMENT()                                              \
    ASSERT_TRUE(::slick::sim::integration::env().ok())                      \
        << ::slick::sim::integration::env().error()

}  // namespace slick::sim::integration
