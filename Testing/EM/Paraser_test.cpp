#include <gtest/gtest.h>
#include "ara/exec/parser/Parser.h"
#include "ara/exec/parser/manifest_parser.h"

using namespace ara::exec::parser;

TEST(ParserTest, ParseExecutionManifest)
{
    std::string path = "../App/Manifests/execution_manifest.json";
    Parser parser(path);

    auto execution_manifest = parser.ParseExecutionManifest();
    EXPECT_EQ(execution_manifest->manifest_id, "exec_id");
    EXPECT_EQ(execution_manifest->processes.size(), 4);

}

TEST(ParserTest, ParseExecutionManifestInvalid)
{
    std::string path = "../App/Manifests/machine_manifest.json";
    Parser parser(path);

    auto execution_manifest = parser.ParseExecutionManifest();
    EXPECT_EQ(execution_manifest, nullptr);

}