#include "cli/cli.hpp"
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

    const sysycc::PassResult result = complier.Run();
    if (!result.ok) {
        std::cerr << result.message << std::endl;
        return 1;
    }

    if (!result.message.empty()) {
        std::cout << result.message << std::endl;
    }
    return 0;
}
