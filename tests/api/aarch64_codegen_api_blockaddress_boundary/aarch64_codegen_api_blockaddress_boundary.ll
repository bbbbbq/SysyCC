target triple = "aarch64-unknown-linux-gnu"

@jump_target = global ptr blockaddress(@foo, %target)
@jump_target_i64 = global i64 ptrtoint (ptr blockaddress(@foo, %target) to i64)

define i64 @get_target_i64() {
entry:
  ret i64 ptrtoint (ptr blockaddress(@foo, %target) to i64)
}

define void @foo() {
entry:
  br label %target

target:
  ret void
}
