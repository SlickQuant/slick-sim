#include "test_environment.hpp"

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    // InitGoogleTest removes gtest's own flags and leaves ours; parse them here so
    // the binary can be pointed at a build without any environment variables.
    auto* environment =
        new slick::sim::integration::CoinbaseIntegrationEnvironment(slick::sim::integration::parsePaths(argc, argv));
    slick::sim::integration::setEnvironment(environment);
    ::testing::AddGlobalTestEnvironment(environment);
    return RUN_ALL_TESTS();
}
