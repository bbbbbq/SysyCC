#include <cassert>
#include <string>

#include "cli/cli.hpp"
#include "compiler/complier_option.hpp"

using namespace sysycc;

int main() {
    {
        char arg0[] = "sysycc";
        char arg1[] = "--strict-c99";
        char arg2[] = "input.sy";
        char *argv[] = {arg0, arg1, arg2};
        ClI::Cli cli;
        cli.Run(3, argv);
        assert(!cli.get_has_error());
        ComplierOption option;
        cli.set_compiler_option(option);
        assert(!option.get_enable_gnu_dialect());
        assert(!option.get_enable_clang_dialect());
        assert(!option.get_enable_builtin_type_extension_pack());
    }

    {
        char arg0[] = "sysycc";
        char arg1[] = "--strict-c99";
        char arg2[] = "--enable-gnu-dialect";
        char arg3[] = "input.sy";
        char *argv[] = {arg0, arg1, arg2, arg3};
        ClI::Cli cli;
        cli.Run(4, argv);
        assert(!cli.get_has_error());
        ComplierOption option;
        cli.set_compiler_option(option);
        assert(option.get_enable_gnu_dialect());
        assert(!option.get_enable_clang_dialect());
        assert(!option.get_enable_builtin_type_extension_pack());
    }

    {
        char arg0[] = "sysycc";
        char arg1[] = "--disable-clang-dialect";
        char arg2[] = "--disable-builtin-types";
        char arg3[] = "input.sy";
        char *argv[] = {arg0, arg1, arg2, arg3};
        ClI::Cli cli;
        cli.Run(4, argv);
        assert(!cli.get_has_error());
        ComplierOption option;
        cli.set_compiler_option(option);
        assert(option.get_enable_gnu_dialect());
        assert(!option.get_enable_clang_dialect());
        assert(!option.get_enable_builtin_type_extension_pack());
    }

    return 0;
}
