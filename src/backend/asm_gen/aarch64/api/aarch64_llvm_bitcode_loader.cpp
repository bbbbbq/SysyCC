#include "backend/asm_gen/aarch64/api/aarch64_llvm_bitcode_loader.hpp"

#include <filesystem>
#include <string>
#include <utility>

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

namespace sysycc {

namespace {

AArch64CodegenDiagnostic make_error(std::string stage_name,
                                    std::string message,
                                    std::string file_path = {}) {
    AArch64CodegenDiagnostic diagnostic;
    diagnostic.severity = AArch64CodegenDiagnosticSeverity::Error;
    diagnostic.stage_name = std::move(stage_name);
    diagnostic.message = std::move(message);
    diagnostic.file_path = std::move(file_path);
    return diagnostic;
}

} // namespace

AArch64BitcodeTextModule load_llvm_bitcode_as_text(const std::string &file_path) {
    AArch64BitcodeTextModule result;

    llvm::LLVMContext context;
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer_or_error =
        llvm::MemoryBuffer::getFile(file_path);
    if (!buffer_or_error) {
        result.diagnostics.push_back(make_error(
            "llvm-bitcode",
            "failed to open LLVM bitcode input for in-process reading",
            file_path));
        return result;
    }

    llvm::Expected<std::unique_ptr<llvm::Module>> module_or_error =
        llvm::parseBitcodeFile(buffer_or_error.get()->getMemBufferRef(), context);
    if (!module_or_error) {
        result.diagnostics.push_back(make_error(
            "llvm-bitcode", llvm::toString(module_or_error.takeError()),
            file_path));
        return result;
    }

    std::string textual_ir;
    llvm::raw_string_ostream output(textual_ir);
    module_or_error.get()->print(output, nullptr);
    output.flush();

    result.ok = true;
    result.textual_llvm_ir = std::move(textual_ir);
    return result;
}

AArch64BitcodeWriteResult write_llvm_ir_text_to_bitcode_file(
    const std::string &source_name, const std::string &text,
    const std::string &file_path) {
    AArch64BitcodeWriteResult result;

    llvm::LLVMContext context;
    llvm::SMDiagnostic parse_error;
    auto buffer = llvm::MemoryBuffer::getMemBufferCopy(text, source_name);
    std::unique_ptr<llvm::Module> module =
        llvm::parseIR(buffer->getMemBufferRef(), parse_error, context);
    if (module == nullptr) {
        std::string error_text;
        llvm::raw_string_ostream stream(error_text);
        parse_error.print(source_name.c_str(), stream);
        stream.flush();
        result.diagnostics.push_back(make_error(
            "llvm-bitcode", error_text.empty()
                                ? "failed to parse LLVM IR text before writing bitcode"
                                : error_text,
            source_name));
        return result;
    }

    const std::filesystem::path output_path(file_path);
    if (output_path.has_parent_path()) {
        std::error_code error_code;
        std::filesystem::create_directories(output_path.parent_path(),
                                            error_code);
        if (error_code) {
            result.diagnostics.push_back(make_error(
                "llvm-bitcode",
                "failed to create output directory for LLVM bitcode artifact",
                file_path));
            return result;
        }
    }

    std::error_code write_error;
    llvm::raw_fd_ostream output(file_path, write_error);
    if (write_error) {
        result.diagnostics.push_back(make_error(
            "llvm-bitcode",
            "failed to open LLVM bitcode output file for writing",
            file_path));
        return result;
    }
    llvm::WriteBitcodeToFile(*module, output);
    output.flush();
    if (output.has_error()) {
        result.diagnostics.push_back(make_error(
            "llvm-bitcode",
            "failed to write LLVM bitcode output file",
            file_path));
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace sysycc
