#include "backend/asm_gen/riscv64/support/riscv64_llvm_target_machine_bridge.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Triple.h>

namespace sysycc {

namespace {

struct LoadedModule {
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::vector<Riscv64CodegenDiagnostic> diagnostics;
};

Riscv64CodegenDiagnostic make_error_diagnostic(std::string stage_name,
                                               std::string message,
                                               std::string file_path = {},
                                               int line = 0,
                                               int column = 0) {
    Riscv64CodegenDiagnostic diagnostic;
    diagnostic.severity = Riscv64CodegenDiagnosticSeverity::Error;
    diagnostic.stage_name = std::move(stage_name);
    diagnostic.message = std::move(message);
    diagnostic.file_path = std::move(file_path);
    diagnostic.line = line;
    diagnostic.column = column;
    return diagnostic;
}

void initialize_riscv64_codegen_once() {
    static std::once_flag initialized;
    std::call_once(initialized, []() {
        LLVMInitializeRISCVTargetInfo();
        LLVMInitializeRISCVTarget();
        LLVMInitializeRISCVTargetMC();
        LLVMInitializeRISCVAsmPrinter();
        LLVMInitializeRISCVAsmParser();
    });
}

std::string resolve_target_triple(const Riscv64CodegenFileRequest &request,
                                  const llvm::Module &module) {
    if (!request.options.target_triple.empty()) {
        return request.options.target_triple;
    }
    if (!module.getTargetTriple().str().empty()) {
        return module.getTargetTriple().str();
    }
    return "riscv64-unknown-linux-gnu";
}

template <typename ResultT>
ResultT fail_with_diagnostic(std::string stage_name, std::string message,
                             std::string file_path = {}) {
    ResultT result;
    result.status = Riscv64CodegenStatus::Failure;
    result.diagnostics.push_back(make_error_diagnostic(
        std::move(stage_name), std::move(message), std::move(file_path)));
    return result;
}

template <typename ResultT>
ResultT unsupported_input(std::string stage_name, std::string message,
                          std::string file_path = {}) {
    ResultT result;
    result.status = Riscv64CodegenStatus::UnsupportedInputFormat;
    result.diagnostics.push_back(make_error_diagnostic(
        std::move(stage_name), std::move(message), std::move(file_path)));
    return result;
}

LoadedModule load_llvm_ir_file(const std::string &file_path) {
    LoadedModule loaded;
    loaded.context = std::make_unique<llvm::LLVMContext>();
    llvm::SMDiagnostic parse_error;
    loaded.module = llvm::parseIRFile(file_path, parse_error, *loaded.context);
    if (loaded.module != nullptr) {
        return loaded;
    }

    std::string message;
    llvm::raw_string_ostream stream(message);
    parse_error.print(file_path.c_str(), stream);
    stream.flush();
    loaded.diagnostics.push_back(make_error_diagnostic(
        "llvm-ir",
        message.empty() ? "failed to parse LLVM IR input file" : message,
        file_path, parse_error.getLineNo(),
        static_cast<int>(parse_error.getColumnNo())));
    return loaded;
}

LoadedModule load_llvm_bitcode_file(const std::string &file_path) {
    LoadedModule loaded;
    loaded.context = std::make_unique<llvm::LLVMContext>();

    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer_or_error =
        llvm::MemoryBuffer::getFile(file_path);
    if (!buffer_or_error) {
        loaded.diagnostics.push_back(make_error_diagnostic(
            "llvm-bitcode", "failed to open LLVM bitcode input file",
            file_path));
        return loaded;
    }

    llvm::Expected<std::unique_ptr<llvm::Module>> module_or_error =
        llvm::parseBitcodeFile(buffer_or_error.get()->getMemBufferRef(),
                               *loaded.context);
    if (!module_or_error) {
        loaded.diagnostics.push_back(make_error_diagnostic(
            "llvm-bitcode", llvm::toString(module_or_error.takeError()),
            file_path));
        return loaded;
    }

    loaded.module = std::move(module_or_error.get());
    return loaded;
}

template <typename ResultT>
ResultT emit_loaded_module(LoadedModule loaded,
                           const Riscv64CodegenFileRequest &request,
                           llvm::CodeGenFileType file_type) {
    if (loaded.module == nullptr) {
        ResultT result;
        result.status = loaded.diagnostics.empty()
                            ? Riscv64CodegenStatus::Failure
                            : Riscv64CodegenStatus::UnsupportedInputFormat;
        result.diagnostics = std::move(loaded.diagnostics);
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(make_error_diagnostic(
                "llvm-import", "failed to load LLVM module",
                request.input_file_path));
        }
        return result;
    }

    initialize_riscv64_codegen_once();

    const std::string triple_text =
        resolve_target_triple(request, *loaded.module);
    const llvm::Triple triple(triple_text);
    if (triple.getArch() != llvm::Triple::riscv64) {
        return unsupported_input<ResultT>(
            "target",
            "RISC-V64 codegen requires a riscv64 target triple",
            request.input_file_path);
    }

    std::string lookup_error;
    const llvm::Target *target =
        llvm::TargetRegistry::lookupTarget(triple, lookup_error);
    if (target == nullptr) {
        return fail_with_diagnostic<ResultT>(
            "target",
            lookup_error.empty()
                ? "failed to resolve the LLVM target for RISC-V64 codegen"
                : lookup_error,
            request.input_file_path);
    }

    llvm::TargetOptions target_options;
    const std::optional<llvm::Reloc::Model> reloc_model =
        request.options.position_independent ? llvm::Reloc::PIC_
                                             : llvm::Reloc::Static;
    std::unique_ptr<llvm::TargetMachine> target_machine(target->createTargetMachine(
        triple, "generic-rv64", "", target_options, reloc_model,
        std::nullopt, llvm::CodeGenOptLevel::Default));
    if (target_machine == nullptr) {
        return fail_with_diagnostic<ResultT>(
            "target",
            "failed to create the LLVM target machine for RISC-V64 codegen",
            request.input_file_path);
    }

    loaded.module->setTargetTriple(triple);
    loaded.module->setDataLayout(target_machine->createDataLayout());

    std::string verify_error_text;
    llvm::raw_string_ostream verify_stream(verify_error_text);
    if (llvm::verifyModule(*loaded.module, &verify_stream)) {
        verify_stream.flush();
        return unsupported_input<ResultT>(
            "llvm-verify",
            verify_error_text.empty()
                ? "LLVM IR verification failed before RISC-V64 emission"
                : verify_error_text,
            request.input_file_path);
    }

    llvm::SmallVector<char, 0> output_buffer;
    llvm::raw_svector_ostream output_stream(output_buffer);
    llvm::legacy::PassManager pass_manager;
    if (target_machine->addPassesToEmitFile(pass_manager, output_stream,
                                            nullptr, file_type)) {
        return fail_with_diagnostic<ResultT>(
            "llvm-codegen",
            "the configured LLVM target machine cannot emit the requested RISC-V64 output kind",
            request.input_file_path);
    }

    pass_manager.run(*loaded.module);

    ResultT result;
    result.status = Riscv64CodegenStatus::Success;
    if constexpr (std::is_same<ResultT, Riscv64AsmCompileResult>::value) {
        result.asm_text.assign(output_buffer.begin(), output_buffer.end());
    } else {
        result.object_bytes.assign(output_buffer.begin(), output_buffer.end());
    }
    return result;
}

} // namespace

Riscv64AsmCompileResult
compile_llvm_ir_file_to_riscv64_asm(const Riscv64CodegenFileRequest &request) {
    return emit_loaded_module<Riscv64AsmCompileResult>(
        load_llvm_ir_file(request.input_file_path), request,
        llvm::CodeGenFileType::AssemblyFile);
}

Riscv64ObjectCompileResult compile_llvm_ir_file_to_riscv64_object(
    const Riscv64CodegenFileRequest &request) {
    return emit_loaded_module<Riscv64ObjectCompileResult>(
        load_llvm_ir_file(request.input_file_path), request,
        llvm::CodeGenFileType::ObjectFile);
}

Riscv64AsmCompileResult compile_llvm_bitcode_file_to_riscv64_asm(
    const Riscv64CodegenFileRequest &request) {
    return emit_loaded_module<Riscv64AsmCompileResult>(
        load_llvm_bitcode_file(request.input_file_path), request,
        llvm::CodeGenFileType::AssemblyFile);
}

Riscv64ObjectCompileResult compile_llvm_bitcode_file_to_riscv64_object(
    const Riscv64CodegenFileRequest &request) {
    return emit_loaded_module<Riscv64ObjectCompileResult>(
        load_llvm_bitcode_file(request.input_file_path), request,
        llvm::CodeGenFileType::ObjectFile);
}

} // namespace sysycc
