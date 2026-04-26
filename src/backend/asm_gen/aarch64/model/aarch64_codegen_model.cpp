#include "backend/asm_gen/aarch64/model/aarch64_object_model.hpp"

#include <utility>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"

namespace sysycc {

namespace {

AArch64VirtualReg memory_base_virtual_reg(const AArch64VirtualReg &reg) {
    return AArch64VirtualReg(reg.get_id(), AArch64VirtualRegKind::General64);
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

} // namespace

std::optional<AArch64ConditionCode>
parse_aarch64_condition_code(std::string_view text) noexcept {
    if (text == "eq")
        return AArch64ConditionCode::Eq;
    if (text == "ne")
        return AArch64ConditionCode::Ne;
    if (text == "cs" || text == "hs")
        return AArch64ConditionCode::Hs;
    if (text == "cc" || text == "lo")
        return AArch64ConditionCode::Lo;
    if (text == "mi")
        return AArch64ConditionCode::Mi;
    if (text == "pl")
        return AArch64ConditionCode::Pl;
    if (text == "vs")
        return AArch64ConditionCode::Vs;
    if (text == "vc")
        return AArch64ConditionCode::Vc;
    if (text == "hi")
        return AArch64ConditionCode::Hi;
    if (text == "ls")
        return AArch64ConditionCode::Ls;
    if (text == "ge")
        return AArch64ConditionCode::Ge;
    if (text == "lt")
        return AArch64ConditionCode::Lt;
    if (text == "gt")
        return AArch64ConditionCode::Gt;
    if (text == "le")
        return AArch64ConditionCode::Le;
    if (text == "al")
        return AArch64ConditionCode::Al;
    return std::nullopt;
}

std::string_view render_aarch64_condition_code(AArch64ConditionCode code) noexcept {
    switch (code) {
    case AArch64ConditionCode::Eq:
        return "eq";
    case AArch64ConditionCode::Ne:
        return "ne";
    case AArch64ConditionCode::Hs:
        return "hs";
    case AArch64ConditionCode::Lo:
        return "lo";
    case AArch64ConditionCode::Mi:
        return "mi";
    case AArch64ConditionCode::Pl:
        return "pl";
    case AArch64ConditionCode::Vs:
        return "vs";
    case AArch64ConditionCode::Vc:
        return "vc";
    case AArch64ConditionCode::Hi:
        return "hi";
    case AArch64ConditionCode::Ls:
        return "ls";
    case AArch64ConditionCode::Ge:
        return "ge";
    case AArch64ConditionCode::Lt:
        return "lt";
    case AArch64ConditionCode::Gt:
        return "gt";
    case AArch64ConditionCode::Le:
        return "le";
    case AArch64ConditionCode::Al:
        return "al";
    }
    return "eq";
}

std::optional<AArch64ShiftKind>
parse_aarch64_shift_kind(std::string_view text) noexcept {
    if (text == "lsl")
        return AArch64ShiftKind::Lsl;
    if (text == "lsr")
        return AArch64ShiftKind::Lsr;
    if (text == "asr")
        return AArch64ShiftKind::Asr;
    return std::nullopt;
}

std::string_view render_aarch64_shift_kind(AArch64ShiftKind kind) noexcept {
    switch (kind) {
    case AArch64ShiftKind::Lsl:
        return "lsl";
    case AArch64ShiftKind::Lsr:
        return "lsr";
    case AArch64ShiftKind::Asr:
        return "asr";
    }
    return "lsl";
}

AArch64MachineOpcode
classify_aarch64_machine_opcode(std::string_view mnemonic) noexcept {
    if (mnemonic.empty()) {
        return AArch64MachineOpcode::Unknown;
    }
    if (mnemonic == ".loc") {
        return AArch64MachineOpcode::DirectiveLoc;
    }
    if (starts_with(mnemonic, ".cfi_")) {
        return AArch64MachineOpcode::DirectiveCfi;
    }
    if (mnemonic == "ret") {
        return AArch64MachineOpcode::Return;
    }
    if (mnemonic == "b") {
        return AArch64MachineOpcode::Branch;
    }
    if (mnemonic == "bl") {
        return AArch64MachineOpcode::BranchLink;
    }
    if (mnemonic == "br") {
        return AArch64MachineOpcode::BranchRegister;
    }
    if (mnemonic == "blr") {
        return AArch64MachineOpcode::BranchLinkRegister;
    }
    if (starts_with(mnemonic, "b.")) {
        return AArch64MachineOpcode::BranchConditional;
    }
    if (mnemonic == "cbz") {
        return AArch64MachineOpcode::CompareBranchZero;
    }
    if (mnemonic == "cbnz") {
        return AArch64MachineOpcode::CompareBranchNonZero;
    }
    if (mnemonic == "movz") {
        return AArch64MachineOpcode::MoveWideZero;
    }
    if (mnemonic == "movk") {
        return AArch64MachineOpcode::MoveWideKeep;
    }
    if (mnemonic == "mov") {
        return AArch64MachineOpcode::Move;
    }
    if (mnemonic == "add") {
        return AArch64MachineOpcode::Add;
    }
    if (mnemonic == "sub") {
        return AArch64MachineOpcode::Sub;
    }
    if (mnemonic == "and") {
        return AArch64MachineOpcode::And;
    }
    if (mnemonic == "orr") {
        return AArch64MachineOpcode::Orr;
    }
    if (mnemonic == "eor") {
        return AArch64MachineOpcode::Eor;
    }
    if (mnemonic == "mul") {
        return AArch64MachineOpcode::Mul;
    }
    if (mnemonic == "madd") {
        return AArch64MachineOpcode::MultiplyAdd;
    }
    if (mnemonic == "msub") {
        return AArch64MachineOpcode::MultiplySubtract;
    }
    if (mnemonic == "sdiv") {
        return AArch64MachineOpcode::SignedDiv;
    }
    if (mnemonic == "udiv") {
        return AArch64MachineOpcode::UnsignedDiv;
    }
    if (mnemonic == "lsl") {
        return AArch64MachineOpcode::ShiftLeft;
    }
    if (mnemonic == "lsr") {
        return AArch64MachineOpcode::ShiftRightLogical;
    }
    if (mnemonic == "asr") {
        return AArch64MachineOpcode::ShiftRightArithmetic;
    }
    if (mnemonic == "cmp") {
        return AArch64MachineOpcode::Compare;
    }
    if (mnemonic == "csel") {
        return AArch64MachineOpcode::ConditionalSelect;
    }
    if (mnemonic == "cset") {
        return AArch64MachineOpcode::ConditionalSet;
    }
    if (mnemonic == "adrp") {
        return AArch64MachineOpcode::Adrp;
    }
    if (mnemonic == "stp") {
        return AArch64MachineOpcode::StorePair;
    }
    if (mnemonic == "ldp") {
        return AArch64MachineOpcode::LoadPair;
    }
    if (mnemonic == "ldr") {
        return AArch64MachineOpcode::Load;
    }
    if (mnemonic == "str") {
        return AArch64MachineOpcode::Store;
    }
    if (mnemonic == "ldrb") {
        return AArch64MachineOpcode::LoadByte;
    }
    if (mnemonic == "strb") {
        return AArch64MachineOpcode::StoreByte;
    }
    if (mnemonic == "ldrh") {
        return AArch64MachineOpcode::LoadHalf;
    }
    if (mnemonic == "strh") {
        return AArch64MachineOpcode::StoreHalf;
    }
    if (mnemonic == "ldur") {
        return AArch64MachineOpcode::LoadUnscaled;
    }
    if (mnemonic == "stur") {
        return AArch64MachineOpcode::StoreUnscaled;
    }
    if (mnemonic == "ldurb") {
        return AArch64MachineOpcode::LoadByteUnscaled;
    }
    if (mnemonic == "sturb") {
        return AArch64MachineOpcode::StoreByteUnscaled;
    }
    if (mnemonic == "ldurh") {
        return AArch64MachineOpcode::LoadHalfUnscaled;
    }
    if (mnemonic == "sturh") {
        return AArch64MachineOpcode::StoreHalfUnscaled;
    }
    if (mnemonic == "fmov") {
        return AArch64MachineOpcode::FloatMove;
    }
    if (mnemonic == "fadd") {
        return AArch64MachineOpcode::FloatAdd;
    }
    if (mnemonic == "fsub") {
        return AArch64MachineOpcode::FloatSub;
    }
    if (mnemonic == "fmul") {
        return AArch64MachineOpcode::FloatMul;
    }
    if (mnemonic == "fmadd") {
        return AArch64MachineOpcode::FloatMulAdd;
    }
    if (mnemonic == "fdiv") {
        return AArch64MachineOpcode::FloatDiv;
    }
    if (mnemonic == "fcmp") {
        return AArch64MachineOpcode::FloatCompare;
    }
    if (mnemonic == "scvtf") {
        return AArch64MachineOpcode::SignedIntToFloat;
    }
    if (mnemonic == "ucvtf") {
        return AArch64MachineOpcode::UnsignedIntToFloat;
    }
    if (mnemonic == "fcvtzs") {
        return AArch64MachineOpcode::FloatToSignedInt;
    }
    if (mnemonic == "fcvtzu") {
        return AArch64MachineOpcode::FloatToUnsignedInt;
    }
    if (mnemonic == "fcvt") {
        return AArch64MachineOpcode::FloatConvert;
    }
    return AArch64MachineOpcode::Unknown;
}

std::string_view
aarch64_machine_opcode_mnemonic(AArch64MachineOpcode opcode) noexcept {
    switch (opcode) {
    case AArch64MachineOpcode::DirectiveLoc:
        return ".loc";
    case AArch64MachineOpcode::Return:
        return "ret";
    case AArch64MachineOpcode::Branch:
        return "b";
    case AArch64MachineOpcode::BranchLink:
        return "bl";
    case AArch64MachineOpcode::BranchRegister:
        return "br";
    case AArch64MachineOpcode::BranchLinkRegister:
        return "blr";
    case AArch64MachineOpcode::CompareBranchZero:
        return "cbz";
    case AArch64MachineOpcode::CompareBranchNonZero:
        return "cbnz";
    case AArch64MachineOpcode::MoveWideZero:
        return "movz";
    case AArch64MachineOpcode::MoveWideKeep:
        return "movk";
    case AArch64MachineOpcode::Move:
        return "mov";
    case AArch64MachineOpcode::Add:
        return "add";
    case AArch64MachineOpcode::Sub:
        return "sub";
    case AArch64MachineOpcode::And:
        return "and";
    case AArch64MachineOpcode::Orr:
        return "orr";
    case AArch64MachineOpcode::Eor:
        return "eor";
    case AArch64MachineOpcode::Mul:
        return "mul";
    case AArch64MachineOpcode::MultiplyAdd:
        return "madd";
    case AArch64MachineOpcode::MultiplySubtract:
        return "msub";
    case AArch64MachineOpcode::SignedDiv:
        return "sdiv";
    case AArch64MachineOpcode::UnsignedDiv:
        return "udiv";
    case AArch64MachineOpcode::ShiftLeft:
        return "lsl";
    case AArch64MachineOpcode::ShiftRightLogical:
        return "lsr";
    case AArch64MachineOpcode::ShiftRightArithmetic:
        return "asr";
    case AArch64MachineOpcode::Compare:
        return "cmp";
    case AArch64MachineOpcode::ConditionalSelect:
        return "csel";
    case AArch64MachineOpcode::ConditionalSet:
        return "cset";
    case AArch64MachineOpcode::Adrp:
        return "adrp";
    case AArch64MachineOpcode::StorePair:
        return "stp";
    case AArch64MachineOpcode::LoadPair:
        return "ldp";
    case AArch64MachineOpcode::Load:
        return "ldr";
    case AArch64MachineOpcode::Store:
        return "str";
    case AArch64MachineOpcode::LoadByte:
        return "ldrb";
    case AArch64MachineOpcode::StoreByte:
        return "strb";
    case AArch64MachineOpcode::LoadHalf:
        return "ldrh";
    case AArch64MachineOpcode::StoreHalf:
        return "strh";
    case AArch64MachineOpcode::LoadUnscaled:
        return "ldur";
    case AArch64MachineOpcode::StoreUnscaled:
        return "stur";
    case AArch64MachineOpcode::LoadByteUnscaled:
        return "ldurb";
    case AArch64MachineOpcode::StoreByteUnscaled:
        return "sturb";
    case AArch64MachineOpcode::LoadHalfUnscaled:
        return "ldurh";
    case AArch64MachineOpcode::StoreHalfUnscaled:
        return "sturh";
    case AArch64MachineOpcode::FloatMove:
        return "fmov";
    case AArch64MachineOpcode::FloatAdd:
        return "fadd";
    case AArch64MachineOpcode::FloatSub:
        return "fsub";
    case AArch64MachineOpcode::FloatMul:
        return "fmul";
    case AArch64MachineOpcode::FloatMulAdd:
        return "fmadd";
    case AArch64MachineOpcode::FloatDiv:
        return "fdiv";
    case AArch64MachineOpcode::FloatCompare:
        return "fcmp";
    case AArch64MachineOpcode::SignedIntToFloat:
        return "scvtf";
    case AArch64MachineOpcode::UnsignedIntToFloat:
        return "ucvtf";
    case AArch64MachineOpcode::FloatToSignedInt:
        return "fcvtzs";
    case AArch64MachineOpcode::FloatToUnsignedInt:
        return "fcvtzu";
    case AArch64MachineOpcode::FloatConvert:
        return "fcvt";
    default:
        return "";
    }
}

bool aarch64_machine_opcode_is_directive(AArch64MachineOpcode opcode) noexcept {
    return opcode == AArch64MachineOpcode::DirectiveLoc ||
           opcode == AArch64MachineOpcode::DirectiveCfi;
}

const AArch64MachineOpcodeDescriptor &
describe_aarch64_machine_opcode(AArch64MachineOpcode opcode) noexcept {
    static const AArch64MachineOpcodeDescriptor kUnknown{
        AArch64MachineOpcode::Unknown, "", false, false, false};
    static const AArch64MachineOpcodeDescriptor kDirectiveLoc{
        AArch64MachineOpcode::DirectiveLoc, ".loc", true, false, false};
    static const AArch64MachineOpcodeDescriptor kDirectiveCfi{
        AArch64MachineOpcode::DirectiveCfi, ".cfi_*", true, false, false};
    static const AArch64MachineOpcodeDescriptor kReturn{
        AArch64MachineOpcode::Return, "ret", false, false, true};
    static const AArch64MachineOpcodeDescriptor kBranch{
        AArch64MachineOpcode::Branch, "b", false, false, true};
    static const AArch64MachineOpcodeDescriptor kBranchLink{
        AArch64MachineOpcode::BranchLink, "bl", false, true, true};
    static const AArch64MachineOpcodeDescriptor kBranchRegister{
        AArch64MachineOpcode::BranchRegister, "br", false, false, true};
    static const AArch64MachineOpcodeDescriptor kBranchLinkRegister{
        AArch64MachineOpcode::BranchLinkRegister, "blr", false, true, true};
    static const AArch64MachineOpcodeDescriptor kBranchConditional{
        AArch64MachineOpcode::BranchConditional, "b.<cond>", false, false, true};
    static const AArch64MachineOpcodeDescriptor kCompareBranchZero{
        AArch64MachineOpcode::CompareBranchZero, "cbz", false, false, true};
    static const AArch64MachineOpcodeDescriptor kCompareBranchNonZero{
        AArch64MachineOpcode::CompareBranchNonZero, "cbnz", false, false, true};
    static const AArch64MachineOpcodeDescriptor kMoveWideZero{
        AArch64MachineOpcode::MoveWideZero, "movz", false, false, false};
    static const AArch64MachineOpcodeDescriptor kMoveWideKeep{
        AArch64MachineOpcode::MoveWideKeep, "movk", false, false, false};
    static const AArch64MachineOpcodeDescriptor kMove{
        AArch64MachineOpcode::Move, "mov", false, false, false};
    static const AArch64MachineOpcodeDescriptor kAdd{
        AArch64MachineOpcode::Add, "add", false, false, false};
    static const AArch64MachineOpcodeDescriptor kSub{
        AArch64MachineOpcode::Sub, "sub", false, false, false};
    static const AArch64MachineOpcodeDescriptor kAnd{
        AArch64MachineOpcode::And, "and", false, false, false};
    static const AArch64MachineOpcodeDescriptor kOrr{
        AArch64MachineOpcode::Orr, "orr", false, false, false};
    static const AArch64MachineOpcodeDescriptor kEor{
        AArch64MachineOpcode::Eor, "eor", false, false, false};
    static const AArch64MachineOpcodeDescriptor kMul{
        AArch64MachineOpcode::Mul, "mul", false, false, false};
    static const AArch64MachineOpcodeDescriptor kMultiplyAdd{
        AArch64MachineOpcode::MultiplyAdd, "madd", false, false, false};
    static const AArch64MachineOpcodeDescriptor kMultiplySubtract{
        AArch64MachineOpcode::MultiplySubtract, "msub", false, false, false};
    static const AArch64MachineOpcodeDescriptor kSignedDiv{
        AArch64MachineOpcode::SignedDiv, "sdiv", false, false, false};
    static const AArch64MachineOpcodeDescriptor kUnsignedDiv{
        AArch64MachineOpcode::UnsignedDiv, "udiv", false, false, false};
    static const AArch64MachineOpcodeDescriptor kShiftLeft{
        AArch64MachineOpcode::ShiftLeft, "lsl", false, false, false};
    static const AArch64MachineOpcodeDescriptor kShiftRightLogical{
        AArch64MachineOpcode::ShiftRightLogical, "lsr", false, false, false};
    static const AArch64MachineOpcodeDescriptor kShiftRightArithmetic{
        AArch64MachineOpcode::ShiftRightArithmetic, "asr", false, false, false};
    static const AArch64MachineOpcodeDescriptor kCompare{
        AArch64MachineOpcode::Compare, "cmp", false, false, false};
    static const AArch64MachineOpcodeDescriptor kConditionalSelect{
        AArch64MachineOpcode::ConditionalSelect, "csel", false, false, false};
    static const AArch64MachineOpcodeDescriptor kConditionalSet{
        AArch64MachineOpcode::ConditionalSet, "cset", false, false, false};
    static const AArch64MachineOpcodeDescriptor kAdrp{
        AArch64MachineOpcode::Adrp, "adrp", false, false, false};
    static const AArch64MachineOpcodeDescriptor kStorePair{
        AArch64MachineOpcode::StorePair, "stp", false, false, false};
    static const AArch64MachineOpcodeDescriptor kLoadPair{
        AArch64MachineOpcode::LoadPair, "ldp", false, false, false};
    static const AArch64MachineOpcodeDescriptor kLoad{
        AArch64MachineOpcode::Load, "ldr", false, false, false};
    static const AArch64MachineOpcodeDescriptor kStore{
        AArch64MachineOpcode::Store, "str", false, false, false};
    static const AArch64MachineOpcodeDescriptor kLoadByte{
        AArch64MachineOpcode::LoadByte, "ldrb", false, false, false};
    static const AArch64MachineOpcodeDescriptor kStoreByte{
        AArch64MachineOpcode::StoreByte, "strb", false, false, false};
    static const AArch64MachineOpcodeDescriptor kLoadHalf{
        AArch64MachineOpcode::LoadHalf, "ldrh", false, false, false};
    static const AArch64MachineOpcodeDescriptor kStoreHalf{
        AArch64MachineOpcode::StoreHalf, "strh", false, false, false};
    static const AArch64MachineOpcodeDescriptor kLoadUnscaled{
        AArch64MachineOpcode::LoadUnscaled, "ldur", false, false, false};
    static const AArch64MachineOpcodeDescriptor kStoreUnscaled{
        AArch64MachineOpcode::StoreUnscaled, "stur", false, false, false};
    static const AArch64MachineOpcodeDescriptor kLoadByteUnscaled{
        AArch64MachineOpcode::LoadByteUnscaled, "ldurb", false, false, false};
    static const AArch64MachineOpcodeDescriptor kStoreByteUnscaled{
        AArch64MachineOpcode::StoreByteUnscaled, "sturb", false, false, false};
    static const AArch64MachineOpcodeDescriptor kLoadHalfUnscaled{
        AArch64MachineOpcode::LoadHalfUnscaled, "ldurh", false, false, false};
    static const AArch64MachineOpcodeDescriptor kStoreHalfUnscaled{
        AArch64MachineOpcode::StoreHalfUnscaled, "sturh", false, false, false};
    static const AArch64MachineOpcodeDescriptor kFloatMove{
        AArch64MachineOpcode::FloatMove, "fmov", false, false, false};
    static const AArch64MachineOpcodeDescriptor kFloatAdd{
        AArch64MachineOpcode::FloatAdd, "fadd", false, false, false};
    static const AArch64MachineOpcodeDescriptor kFloatSub{
        AArch64MachineOpcode::FloatSub, "fsub", false, false, false};
    static const AArch64MachineOpcodeDescriptor kFloatMul{
        AArch64MachineOpcode::FloatMul, "fmul", false, false, false};
    static const AArch64MachineOpcodeDescriptor kFloatMulAdd{
        AArch64MachineOpcode::FloatMulAdd, "fmadd", false, false, false};
    static const AArch64MachineOpcodeDescriptor kFloatDiv{
        AArch64MachineOpcode::FloatDiv, "fdiv", false, false, false};
    static const AArch64MachineOpcodeDescriptor kFloatCompare{
        AArch64MachineOpcode::FloatCompare, "fcmp", false, false, false};
    static const AArch64MachineOpcodeDescriptor kSignedIntToFloat{
        AArch64MachineOpcode::SignedIntToFloat, "scvtf", false, false, false};
    static const AArch64MachineOpcodeDescriptor kUnsignedIntToFloat{
        AArch64MachineOpcode::UnsignedIntToFloat, "ucvtf", false, false, false};
    static const AArch64MachineOpcodeDescriptor kFloatToSignedInt{
        AArch64MachineOpcode::FloatToSignedInt, "fcvtzs", false, false, false};
    static const AArch64MachineOpcodeDescriptor kFloatToUnsignedInt{
        AArch64MachineOpcode::FloatToUnsignedInt, "fcvtzu", false, false, false};
    static const AArch64MachineOpcodeDescriptor kFloatConvert{
        AArch64MachineOpcode::FloatConvert, "fcvt", false, false, false};

    switch (opcode) {
    case AArch64MachineOpcode::DirectiveLoc:
        return kDirectiveLoc;
    case AArch64MachineOpcode::DirectiveCfi:
        return kDirectiveCfi;
    case AArch64MachineOpcode::Return:
        return kReturn;
    case AArch64MachineOpcode::Branch:
        return kBranch;
    case AArch64MachineOpcode::BranchLink:
        return kBranchLink;
    case AArch64MachineOpcode::BranchRegister:
        return kBranchRegister;
    case AArch64MachineOpcode::BranchLinkRegister:
        return kBranchLinkRegister;
    case AArch64MachineOpcode::BranchConditional:
        return kBranchConditional;
    case AArch64MachineOpcode::CompareBranchZero:
        return kCompareBranchZero;
    case AArch64MachineOpcode::CompareBranchNonZero:
        return kCompareBranchNonZero;
    case AArch64MachineOpcode::MoveWideZero:
        return kMoveWideZero;
    case AArch64MachineOpcode::MoveWideKeep:
        return kMoveWideKeep;
    case AArch64MachineOpcode::Move:
        return kMove;
    case AArch64MachineOpcode::Add:
        return kAdd;
    case AArch64MachineOpcode::Sub:
        return kSub;
    case AArch64MachineOpcode::And:
        return kAnd;
    case AArch64MachineOpcode::Orr:
        return kOrr;
    case AArch64MachineOpcode::Eor:
        return kEor;
    case AArch64MachineOpcode::Mul:
        return kMul;
    case AArch64MachineOpcode::MultiplyAdd:
        return kMultiplyAdd;
    case AArch64MachineOpcode::MultiplySubtract:
        return kMultiplySubtract;
    case AArch64MachineOpcode::SignedDiv:
        return kSignedDiv;
    case AArch64MachineOpcode::UnsignedDiv:
        return kUnsignedDiv;
    case AArch64MachineOpcode::ShiftLeft:
        return kShiftLeft;
    case AArch64MachineOpcode::ShiftRightLogical:
        return kShiftRightLogical;
    case AArch64MachineOpcode::ShiftRightArithmetic:
        return kShiftRightArithmetic;
    case AArch64MachineOpcode::Compare:
        return kCompare;
    case AArch64MachineOpcode::ConditionalSelect:
        return kConditionalSelect;
    case AArch64MachineOpcode::ConditionalSet:
        return kConditionalSet;
    case AArch64MachineOpcode::Adrp:
        return kAdrp;
    case AArch64MachineOpcode::StorePair:
        return kStorePair;
    case AArch64MachineOpcode::LoadPair:
        return kLoadPair;
    case AArch64MachineOpcode::Load:
        return kLoad;
    case AArch64MachineOpcode::Store:
        return kStore;
    case AArch64MachineOpcode::LoadByte:
        return kLoadByte;
    case AArch64MachineOpcode::StoreByte:
        return kStoreByte;
    case AArch64MachineOpcode::LoadHalf:
        return kLoadHalf;
    case AArch64MachineOpcode::StoreHalf:
        return kStoreHalf;
    case AArch64MachineOpcode::LoadUnscaled:
        return kLoadUnscaled;
    case AArch64MachineOpcode::StoreUnscaled:
        return kStoreUnscaled;
    case AArch64MachineOpcode::LoadByteUnscaled:
        return kLoadByteUnscaled;
    case AArch64MachineOpcode::StoreByteUnscaled:
        return kStoreByteUnscaled;
    case AArch64MachineOpcode::LoadHalfUnscaled:
        return kLoadHalfUnscaled;
    case AArch64MachineOpcode::StoreHalfUnscaled:
        return kStoreHalfUnscaled;
    case AArch64MachineOpcode::FloatMove:
        return kFloatMove;
    case AArch64MachineOpcode::FloatAdd:
        return kFloatAdd;
    case AArch64MachineOpcode::FloatSub:
        return kFloatSub;
    case AArch64MachineOpcode::FloatMul:
        return kFloatMul;
    case AArch64MachineOpcode::FloatMulAdd:
        return kFloatMulAdd;
    case AArch64MachineOpcode::FloatDiv:
        return kFloatDiv;
    case AArch64MachineOpcode::FloatCompare:
        return kFloatCompare;
    case AArch64MachineOpcode::SignedIntToFloat:
        return kSignedIntToFloat;
    case AArch64MachineOpcode::UnsignedIntToFloat:
        return kUnsignedIntToFloat;
    case AArch64MachineOpcode::FloatToSignedInt:
        return kFloatToSignedInt;
    case AArch64MachineOpcode::FloatToUnsignedInt:
        return kFloatToUnsignedInt;
    case AArch64MachineOpcode::FloatConvert:
        return kFloatConvert;
    case AArch64MachineOpcode::Unknown:
    default:
        return kUnknown;
    }
}

AArch64MachineOperand::AArch64MachineOperand(AArch64MachineOperandKind kind,
                                             Payload payload)
    : kind_(kind), payload_(std::move(payload)) {}

AArch64MachineOperand
AArch64MachineOperand::use_virtual_reg(const AArch64VirtualReg &reg) {
    return AArch64MachineOperand(AArch64MachineOperandKind::VirtualReg,
                                 AArch64MachineVirtualRegOperand{reg, false});
}

AArch64MachineOperand
AArch64MachineOperand::def_virtual_reg(const AArch64VirtualReg &reg) {
    return AArch64MachineOperand(AArch64MachineOperandKind::VirtualReg,
                                 AArch64MachineVirtualRegOperand{reg, true});
}

AArch64MachineOperand
AArch64MachineOperand::physical_reg(unsigned reg_number,
                                    AArch64VirtualRegKind kind) {
    return AArch64MachineOperand(AArch64MachineOperandKind::PhysicalReg,
                                 AArch64MachinePhysicalRegOperand{reg_number, kind});
}

AArch64MachineOperand
AArch64MachineOperand::use_vector_reg(const AArch64VirtualReg &reg,
                                      unsigned lane_count,
                                      char element_kind) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::VectorReg,
        AArch64MachineVectorRegOperand{
            AArch64MachineVectorRegOperand::BaseKind::VirtualReg,
            AArch64VirtualReg(reg.get_id(), AArch64VirtualRegKind::Float128),
            0,
            false,
            lane_count,
            element_kind,
            std::nullopt});
}

AArch64MachineOperand
AArch64MachineOperand::def_vector_reg(const AArch64VirtualReg &reg,
                                      unsigned lane_count,
                                      char element_kind) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::VectorReg,
        AArch64MachineVectorRegOperand{
            AArch64MachineVectorRegOperand::BaseKind::VirtualReg,
            AArch64VirtualReg(reg.get_id(), AArch64VirtualRegKind::Float128),
            0,
            true,
            lane_count,
            element_kind,
            std::nullopt});
}

AArch64MachineOperand
AArch64MachineOperand::use_vector_lane(const AArch64VirtualReg &reg,
                                       char element_kind,
                                       unsigned lane_index) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::VectorReg,
        AArch64MachineVectorRegOperand{
            AArch64MachineVectorRegOperand::BaseKind::VirtualReg,
            AArch64VirtualReg(reg.get_id(), AArch64VirtualRegKind::Float128),
            0,
            false,
            1,
            element_kind,
            lane_index});
}

AArch64MachineOperand
AArch64MachineOperand::def_vector_lane(const AArch64VirtualReg &reg,
                                       char element_kind,
                                       unsigned lane_index) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::VectorReg,
        AArch64MachineVectorRegOperand{
            AArch64MachineVectorRegOperand::BaseKind::VirtualReg,
            AArch64VirtualReg(reg.get_id(), AArch64VirtualRegKind::Float128),
            0,
            true,
            1,
            element_kind,
            lane_index});
}

AArch64MachineOperand
AArch64MachineOperand::physical_vector_reg(unsigned reg_number,
                                           unsigned lane_count,
                                           char element_kind,
                                           bool is_def) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::VectorReg,
        AArch64MachineVectorRegOperand{
            AArch64MachineVectorRegOperand::BaseKind::PhysicalReg,
            {},
            reg_number,
            is_def,
            lane_count,
            element_kind,
            std::nullopt});
}

AArch64MachineOperand
AArch64MachineOperand::physical_vector_lane(unsigned reg_number,
                                            char element_kind,
                                            unsigned lane_index,
                                            bool is_def) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::VectorReg,
        AArch64MachineVectorRegOperand{
            AArch64MachineVectorRegOperand::BaseKind::PhysicalReg,
            {},
            reg_number,
            is_def,
            1,
            element_kind,
            lane_index});
}

AArch64MachineOperand AArch64MachineOperand::immediate(std::string text) {
    return AArch64MachineOperand(AArch64MachineOperandKind::Immediate,
                                 AArch64MachineImmediateOperand{std::move(text)});
}

AArch64MachineOperand AArch64MachineOperand::symbol(std::string text) {
    return AArch64MachineOperand::symbol(
        AArch64MachineSymbolReference::plain(std::move(text)));
}

AArch64MachineOperand
AArch64MachineOperand::symbol(AArch64SymbolReference reference) {
    return AArch64MachineOperand::symbol(
        AArch64MachineSymbolReference::plain(std::move(reference)));
}

AArch64MachineOperand
AArch64MachineOperand::symbol(AArch64MachineSymbolReference reference) {
    return AArch64MachineOperand(AArch64MachineOperandKind::Symbol,
                                 AArch64MachineSymbolOperand{std::move(reference)});
}

AArch64MachineOperand AArch64MachineOperand::label(std::string text) {
    return AArch64MachineOperand(AArch64MachineOperandKind::Label,
                                 AArch64MachineLabelOperand{std::move(text)});
}

AArch64MachineOperand
AArch64MachineOperand::condition_code(AArch64ConditionCode code) {
    return AArch64MachineOperand(AArch64MachineOperandKind::ConditionCode,
                                 AArch64MachineConditionCodeOperand{code});
}

AArch64MachineOperand AArch64MachineOperand::condition_code(std::string code) {
    return AArch64MachineOperand::condition_code(
        parse_aarch64_condition_code(code).value_or(AArch64ConditionCode::Eq));
}

AArch64MachineOperand AArch64MachineOperand::zero_register(bool use_64bit) {
    return AArch64MachineOperand(AArch64MachineOperandKind::ZeroRegister,
                                 AArch64MachineZeroRegisterOperand{use_64bit});
}

AArch64MachineOperand AArch64MachineOperand::shift(AArch64ShiftKind kind,
                                                   unsigned amount) {
    return AArch64MachineOperand(AArch64MachineOperandKind::Shift,
                                 AArch64MachineShiftOperand{kind, amount});
}

AArch64MachineOperand AArch64MachineOperand::shift(std::string mnemonic,
                                                   unsigned amount) {
    return AArch64MachineOperand::shift(
        parse_aarch64_shift_kind(mnemonic).value_or(AArch64ShiftKind::Lsl),
        amount);
}

AArch64MachineOperand AArch64MachineOperand::stack_pointer(bool use_64bit) {
    return AArch64MachineOperand(AArch64MachineOperandKind::StackPointer,
                                 AArch64MachineStackPointerOperand{use_64bit});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_virtual_reg(
    const AArch64VirtualReg &reg, std::optional<long long> immediate_offset,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    const AArch64VirtualReg base_reg = memory_base_virtual_reg(reg);
    AArch64MachineMemoryAddressOperand::OffsetPayload offset;
    if (immediate_offset.has_value()) {
        offset = AArch64MachineMemoryImmediateOffset{*immediate_offset};
    }
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg,
            .virtual_reg = base_reg,
            .physical_reg = 0,
            .stack_pointer_use_64bit = true,
            .offset = std::move(offset),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_physical_reg(
    unsigned reg_number, std::optional<long long> immediate_offset,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    AArch64MachineMemoryAddressOperand::OffsetPayload offset;
    if (immediate_offset.has_value()) {
        offset = AArch64MachineMemoryImmediateOffset{*immediate_offset};
    }
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg,
            .virtual_reg = {},
            .physical_reg = reg_number,
            .stack_pointer_use_64bit = true,
            .offset = std::move(offset),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_stack_pointer(
    std::optional<long long> immediate_offset, bool use_64bit,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    AArch64MachineMemoryAddressOperand::OffsetPayload offset;
    if (immediate_offset.has_value()) {
        offset = AArch64MachineMemoryImmediateOffset{*immediate_offset};
    }
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::StackPointer,
            .virtual_reg = {},
            .physical_reg = 0,
            .stack_pointer_use_64bit = use_64bit,
            .offset = std::move(offset),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_virtual_reg(
    const AArch64VirtualReg &reg, AArch64MachineSymbolReference symbolic_offset,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    const AArch64VirtualReg base_reg = memory_base_virtual_reg(reg);
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg,
            .virtual_reg = base_reg,
            .physical_reg = 0,
            .stack_pointer_use_64bit = true,
            .offset = std::move(symbolic_offset),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_physical_reg(
    unsigned reg_number, AArch64MachineSymbolReference symbolic_offset,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg,
            .virtual_reg = {},
            .physical_reg = reg_number,
            .stack_pointer_use_64bit = true,
            .offset = std::move(symbolic_offset),
            .address_mode = address_mode});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_physical_reg_indexed(
    unsigned base_reg_number, unsigned index_reg_number,
    AArch64VirtualRegKind index_kind, AArch64ShiftKind shift_kind,
    unsigned shift_amount) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg,
            .virtual_reg = {},
            .physical_reg = base_reg_number,
            .stack_pointer_use_64bit = true,
            .offset = AArch64MachineMemoryRegisterOffset{
                .reg_number = index_reg_number,
                .kind = index_kind,
                .shift_kind = shift_kind,
                .shift_amount = shift_amount,
            },
            .address_mode = AArch64MachineMemoryAddressOperand::AddressMode::Offset});
}

AArch64MachineOperand AArch64MachineOperand::memory_address_stack_pointer(
    AArch64MachineSymbolReference symbolic_offset, bool use_64bit,
    AArch64MachineMemoryAddressOperand::AddressMode address_mode) {
    return AArch64MachineOperand(
        AArch64MachineOperandKind::MemoryAddress,
        AArch64MachineMemoryAddressOperand{
            .base_kind = AArch64MachineMemoryAddressOperand::BaseKind::StackPointer,
            .virtual_reg = {},
            .physical_reg = 0,
            .stack_pointer_use_64bit = use_64bit,
            .offset = std::move(symbolic_offset),
            .address_mode = address_mode});
}

const AArch64MachineVirtualRegOperand *
AArch64MachineOperand::get_virtual_reg_operand() const noexcept {
    return std::get_if<AArch64MachineVirtualRegOperand>(&payload_);
}

const AArch64MachinePhysicalRegOperand *
AArch64MachineOperand::get_physical_reg_operand() const noexcept {
    return std::get_if<AArch64MachinePhysicalRegOperand>(&payload_);
}

const AArch64MachineVectorRegOperand *
AArch64MachineOperand::get_vector_reg_operand() const noexcept {
    return std::get_if<AArch64MachineVectorRegOperand>(&payload_);
}

const AArch64MachineConditionCodeOperand *
AArch64MachineOperand::get_condition_code_operand() const noexcept {
    return std::get_if<AArch64MachineConditionCodeOperand>(&payload_);
}

const AArch64MachineImmediateOperand *
AArch64MachineOperand::get_immediate_operand() const noexcept {
    return std::get_if<AArch64MachineImmediateOperand>(&payload_);
}

const AArch64MachineSymbolOperand *
AArch64MachineOperand::get_symbol_operand() const noexcept {
    return std::get_if<AArch64MachineSymbolOperand>(&payload_);
}

const AArch64MachineLabelOperand *
AArch64MachineOperand::get_label_operand() const noexcept {
    return std::get_if<AArch64MachineLabelOperand>(&payload_);
}

const AArch64MachineZeroRegisterOperand *
AArch64MachineOperand::get_zero_register_operand() const noexcept {
    return std::get_if<AArch64MachineZeroRegisterOperand>(&payload_);
}

const AArch64MachineShiftOperand *
AArch64MachineOperand::get_shift_operand() const noexcept {
    return std::get_if<AArch64MachineShiftOperand>(&payload_);
}

const AArch64MachineStackPointerOperand *
AArch64MachineOperand::get_stack_pointer_operand() const noexcept {
    return std::get_if<AArch64MachineStackPointerOperand>(&payload_);
}

const AArch64MachineMemoryAddressOperand *
AArch64MachineOperand::get_memory_address_operand() const noexcept {
    return std::get_if<AArch64MachineMemoryAddressOperand>(&payload_);
}

AArch64MachineInstr::AArch64MachineInstr(
    AArch64MachineOpcode opcode, std::vector<AArch64MachineOperand> operands,
    AArch64InstructionFlags flags, std::optional<AArch64DebugLocation> debug_location,
    std::vector<std::size_t> implicit_defs,
    std::vector<std::size_t> implicit_uses,
    std::optional<AArch64CallClobberMask> call_clobber_mask)
    : AArch64MachineInstr(
          std::string(aarch64_machine_opcode_mnemonic(opcode)),
          std::move(operands), flags, std::move(debug_location),
          std::move(implicit_defs), std::move(implicit_uses),
          std::move(call_clobber_mask)) {
    opcode_ = opcode;
}

AArch64MachineInstr::AArch64MachineInstr(
    std::string mnemonic, std::vector<AArch64MachineOperand> operands,
    AArch64InstructionFlags flags, std::optional<AArch64DebugLocation> debug_location,
    std::vector<std::size_t> implicit_defs,
    std::vector<std::size_t> implicit_uses,
    std::optional<AArch64CallClobberMask> call_clobber_mask)
    : opcode_(classify_aarch64_machine_opcode(mnemonic)),
      mnemonic_(std::move(mnemonic)),
      operands_(std::move(operands)),
      flags_(flags),
      debug_location_(std::move(debug_location)),
      explicit_defs_(collect_explicit_vreg_ids(operands_, true)),
      explicit_uses_(collect_explicit_vreg_ids(operands_, false)),
      implicit_defs_(std::move(implicit_defs)),
      implicit_uses_(std::move(implicit_uses)),
      call_clobber_mask_(std::move(call_clobber_mask)) {}

AArch64MachineInstr::AArch64MachineInstr(
    std::string mnemonic, std::vector<AArch64MachineOperand> operands,
    AArch64InstructionFlags flags, std::vector<std::size_t> implicit_defs,
    std::vector<std::size_t> implicit_uses,
    std::optional<AArch64CallClobberMask> call_clobber_mask)
    : AArch64MachineInstr(std::move(mnemonic), std::move(operands), flags,
                          std::nullopt, std::move(implicit_defs),
                          std::move(implicit_uses),
                          std::move(call_clobber_mask)) {}

} // namespace sysycc
