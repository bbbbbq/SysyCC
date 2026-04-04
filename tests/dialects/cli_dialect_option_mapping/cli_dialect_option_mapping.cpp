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

    {
        char arg0[] = "sysycc";
        char arg1[] = "-S";
        char arg2[] = "--dump-core-ir";
        char arg3[] = "--backend=aarch64-native";
        char arg4[] = "--target=aarch64-unknown-linux-gnu";
        char arg5[] = "--stop-after=asm";
        char arg6[] = "-o";
        char arg7[] = "out.s";
        char arg8[] = "input.sy";
        char *argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8};
        ClI::Cli cli;
        cli.Run(9, argv);
        assert(!cli.get_has_error());
        ComplierOption option;
        cli.set_compiler_option(option);
        assert(option.emit_asm());
        assert(option.dump_core_ir());
        assert(option.get_stop_after_stage() == StopAfterStage::Asm);
        assert(option.get_output_file() == "out.s");
        assert(option.get_backend_options().get_backend_kind() ==
               BackendKind::AArch64Native);
        assert(option.get_backend_options().get_target_triple() ==
               "aarch64-unknown-linux-gnu");
        assert(option.get_backend_options().get_output_file() == "out.s");
    }

    return 0;
}
