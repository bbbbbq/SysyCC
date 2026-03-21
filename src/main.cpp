#include "cli/cli.hpp"
#include "common/diagnostic/diagnostic_formatter.hpp"
#include "compiler/complier.hpp"
#include "compiler/complier_option.hpp"

#include <iostream>

int main(int argc, char *argv[]) {
    ClI::Cli cli;
    cli.Run(argc, argv);

    if (cli.get_is_help() || cli.get_is_version()) {
        return 0;
    }

    if (cli.get_has_error() || !cli.has_input_file()) {
        return 1;
    }

    sysycc::ComplierOption option;
    cli.set_compiler_option(option);
    sysycc::Complier complier(option);

    sysycc::PassResult result = complier.Run();
    const sysycc::DiagnosticEngine &diagnostic_engine =
        complier.get_context().get_diagnostic_engine();
    if (!result.ok) {
        if (!diagnostic_engine.get_diagnostics().empty()) {
            sysycc::DiagnosticFormatter::print_diagnostics(std::cerr,
                                                           diagnostic_engine);
        } else {
            std::cerr << result.message << '\n';
        }
        return 1;
    }

    if (!diagnostic_engine.get_diagnostics().empty()) {
        sysycc::DiagnosticFormatter::print_diagnostics(std::cerr,
                                                       diagnostic_engine);
    }

    if (!result.message.empty()) {
        std::cout << result.message << '\n';
    }
    return 0;
}
