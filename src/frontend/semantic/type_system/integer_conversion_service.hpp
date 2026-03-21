#pragma once

#include <optional>

namespace sysycc {

class SemanticType;
class SemanticModel;

namespace detail {

enum class IntegerConversionKind : unsigned char {
    None,
    SignExtend,
    ZeroExtend,
    Truncate,
    Unsupported,
};

class IntegerTypeInfo {
  private:
    bool is_signed_ = false;
    int bit_width_ = 0;
    int rank_ = 0;

  public:
    IntegerTypeInfo(bool is_signed, int bit_width, int rank) noexcept
        : is_signed_(is_signed), bit_width_(bit_width), rank_(rank) {}

    bool get_is_signed() const noexcept { return is_signed_; }
    int get_bit_width() const noexcept { return bit_width_; }
    int get_rank() const noexcept { return rank_; }
};

class IntegerConversionPlan {
  private:
    IntegerConversionKind kind_ = IntegerConversionKind::Unsupported;
    const SemanticType *source_type_ = nullptr;
    const SemanticType *target_type_ = nullptr;

  public:
    IntegerConversionPlan(IntegerConversionKind kind,
                          const SemanticType *source_type,
                          const SemanticType *target_type) noexcept
        : kind_(kind), source_type_(source_type), target_type_(target_type) {}

    IntegerConversionKind get_kind() const noexcept { return kind_; }
    const SemanticType *get_source_type() const noexcept { return source_type_; }
    const SemanticType *get_target_type() const noexcept { return target_type_; }
};

class IntegerConversionService {
  public:
    std::optional<IntegerTypeInfo> get_integer_type_info(
        const SemanticType *type) const;

    std::optional<int> get_floating_type_rank(const SemanticType *type) const;

    const SemanticType *get_integer_promotion_type(
        const SemanticType *type, SemanticModel &semantic_model) const;

    const SemanticType *get_common_integer_type(
        const SemanticType *lhs, const SemanticType *rhs,
        SemanticModel &semantic_model) const;

    const SemanticType *get_usual_arithmetic_conversion_type(
        const SemanticType *lhs, const SemanticType *rhs,
        SemanticModel &semantic_model) const;

    IntegerConversionPlan get_integer_conversion_plan(
        const SemanticType *source_type,
        const SemanticType *target_type) const noexcept;
};

} // namespace detail
} // namespace sysycc
