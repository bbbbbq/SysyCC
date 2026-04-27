#include <cassert>
#include <string>

#include "cli/cli.hpp"
#include "compiler/compiler_option.hpp"

using namespace sysycc;

int main() {
    {
        char arg0[] = "sysycc";
        char arg1[] = "-std=c99";
        char arg2[] = "-fsyntax-only";
        char arg3[] = "input.sy";
        char *argv[] = {arg0, arg1, arg2, arg3};
        ClI::Cli cli;
        cli.Run(4, argv);
        assert(!cli.get_has_error());
        CompilerOption option;
        cli.set_compiler_option(option);
        assert(option.get_driver_action() == DriverAction::SyntaxOnly);
        assert(option.get_stop_after_stage() == StopAfterStage::Semantic);
        assert(option.get_language_mode() == LanguageMode::C99);
        assert(!option.get_enable_gnu_dialect());
        assert(!option.get_enable_clang_dialect());
        assert(!option.get_enable_builtin_type_extension_pack());
    }

    {
        char arg0[] = "sysycc";
        char arg1[] = "--strict-c99";
        char arg2[] = "--enable-gnu-dialect";
        char arg3[] = "-fsyntax-only";
        char arg4[] = "input.sy";
        char *argv[] = {arg0, arg1, arg2, arg3, arg4};
        ClI::Cli cli;
        cli.Run(5, argv);
        assert(!cli.get_has_error());
        CompilerOption option;
        cli.set_compiler_option(option);
        assert(option.get_driver_action() == DriverAction::SyntaxOnly);
        assert(option.get_enable_gnu_dialect());
        assert(!option.get_enable_clang_dialect());
        assert(!option.get_enable_builtin_type_extension_pack());
    }

    {
        char arg0[] = "sysycc";
        char arg1[] = "-std=gnu99";
        char arg2[] = "-fclang-extensions";
        char arg3[] = "-fbuiltin-types";
        char arg4[] = "-fsyntax-only";
        char arg5[] = "input.sy";
        char *argv[] = {arg0, arg1, arg2, arg3, arg4, arg5};
        ClI::Cli cli;
        cli.Run(6, argv);
        assert(!cli.get_has_error());
        CompilerOption option;
        cli.set_compiler_option(option);
        assert(option.get_language_mode() == LanguageMode::Gnu99);
        assert(option.get_enable_gnu_dialect());
        assert(option.get_enable_clang_dialect());
        assert(option.get_enable_builtin_type_extension_pack());
    }

    {
        char arg0[] = "sysycc";
        char arg1[] = "-S";
        char arg2[] = "-emit-llvm";
        char arg3[] = "-o";
        char arg4[] = "out.ll";
        char arg5[] = "input.sy";
        char *argv[] = {arg0, arg1, arg2, arg3, arg4, arg5};
        ClI::Cli cli;
        cli.Run(6, argv);
        assert(!cli.get_has_error());
        CompilerOption option;
        cli.set_compiler_option(option);
        assert(option.get_driver_action() == DriverAction::EmitLlvmIr);
        assert(!option.emit_asm());
        assert(option.get_stop_after_stage() == StopAfterStage::IR);
        assert(option.get_output_file() == "out.ll");
        assert(option.get_backend_options().get_backend_kind() ==
               BackendKind::LlvmIr);
    }

    {
        char arg0[] = "sysycc";
        char arg1[] = "-S";
        char arg2[] = "-o";
        char arg3[] = "out.s";
        char arg4[] = "input.sy";
        char *argv[] = {arg0, arg1, arg2, arg3, arg4};
        ClI::Cli cli;
        cli.Run(5, argv);
        assert(!cli.get_has_error());
        CompilerOption option;
        cli.set_compiler_option(option);
        assert(option.get_driver_action() == DriverAction::EmitAssembly);
        assert(option.emit_asm());
        assert(option.get_stop_after_stage() == StopAfterStage::Asm);
        assert(option.get_output_file() == "out.s");
        assert(option.get_backend_options().get_backend_kind() ==
               BackendKind::AArch64Native);
        assert(option.get_backend_options().get_target_triple() ==
               "aarch64-unknown-linux-gnu");
        assert(option.get_backend_options().get_output_file() == "out.s");
    }

    {
        char arg0[] = "sysycc";
        char arg1[] = "-S";
        char arg2[] = "--backend=riscv64-native";
        char arg3[] = "-o";
        char arg4[] = "out.s";
        char arg5[] = "input.sy";
        char *argv[] = {arg0, arg1, arg2, arg3, arg4, arg5};
        ClI::Cli cli;
        cli.Run(6, argv);
        assert(!cli.get_has_error());
        CompilerOption option;
        cli.set_compiler_option(option);
        assert(option.get_driver_action() == DriverAction::EmitAssembly);
        assert(option.emit_asm());
        assert(option.get_stop_after_stage() == StopAfterStage::Asm);
        assert(option.get_output_file() == "out.s");
        assert(option.get_backend_options().get_backend_kind() ==
               BackendKind::Riscv64Native);
        assert(option.get_backend_options().get_target_triple() ==
               "riscv64-unknown-linux-gnu");
        assert(option.get_backend_options().get_output_file() == "out.s");
    }

    {
        char arg0[] = "sysycc";
        char arg1[] = "-DDEBUG=1";
        char arg2[] = "-ULEGACY";
        char arg3[] = "-include";
        char arg4[] = "force.h";
        char arg5[] = "-nostdinc";
        char arg6[] = "-Werror";
        char arg7[] = "-Wno-sign-compare";
        char arg8[] = "-fsyntax-only";
        char arg9[] = "input.sy";
        char *argv[] = {arg0, arg1, arg2, arg3, arg4,
                        arg5, arg6, arg7, arg8, arg9};
        ClI::Cli cli;
        cli.Run(10, argv);
        assert(!cli.get_has_error());
        CompilerOption option;
        cli.set_compiler_option(option);
        assert(option.get_no_stdinc());
        assert(option.get_command_line_macro_options().size() == 2U);
        assert(option.get_command_line_macro_options()[0].get_action_kind() ==
               CommandLineMacroActionKind::Define);
        assert(option.get_command_line_macro_options()[0].get_name() == "DEBUG");
        assert(option.get_command_line_macro_options()[0].get_replacement() ==
               "1");
        assert(option.get_command_line_macro_options()[0].has_replacement());
        assert(option.get_command_line_macro_options()[1].get_action_kind() ==
               CommandLineMacroActionKind::Undefine);
        assert(option.get_command_line_macro_options()[1].get_name() ==
               "LEGACY");
        assert(option.get_forced_include_files().size() == 1U);
        assert(option.get_forced_include_files()[0] == "force.h");
        assert(option.get_warning_policy().all_warnings_as_errors());
        assert(!option.get_warning_policy().should_emit_warning("sign-compare"));
    }

    return 0;
}
