/*
 * File: /tomato/tests/src/test.cpp
 * 
 * Created the 20 May 2024, 01:36 am by TinyMinori
 * Description :
 * 
 * Project repository: https://github.com/TinyMinori/tomato
 * Copyright 2024 TinyMinori
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <stdexcept>
#include "LuauContext.h"

TEST_CASE("Check existing variables")
{
    tomato::LuauContext script{};
    script.run("./resources/existing-variables.luau");

    REQUIRE(script.doesExist("globalVar") == true);
    REQUIRE(script.doesExist("localScopeVar") == false);
    REQUIRE(script.doesExist("doesNotExist") == false);
}

TEST_CASE("Check script opening")
{
    tomato::LuauContext script{};
    CHECK_THROWS_MATCHES(
        script.run("./resources/non-existing.luau"),
        std::runtime_error,
        Catch::Matchers::ExceptionMessageMatcher("The script named ./resources/non-existing.luau is not a correct script location."));
    
    CHECK_NOTHROW(script.run("./resources/existing-script.luau"));
}