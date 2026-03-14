#include <iostream>
#include "cli/cli.hpp"
#include "compiler/complier_option.hpp"
ClI::Cli cli;
sysycc::ComplierOption option;

int main(int argc, char *argv[]) {
    cli.Run(argc, argv);
    cli.set_compiler_option(option);
    return 0;
}
    