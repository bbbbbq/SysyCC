target triple = "aarch64-unknown-linux-gnu"

@seed = global i32 7
@seed_addr = global i64 ptrtoint (ptr @seed to i64)
@null_ptr = global ptr inttoptr (i64 0 to ptr)
@seed_roundtrip_alias = alias i32, ptr inttoptr (i64 ptrtoint (ptr @seed to i64) to ptr)
@pair = global { i32, i32 } { i32 1, i32 9 }
@field_addr = global i64 ptrtoint (ptr getelementptr inbounds ({ i32, i32 }, ptr @pair, i64 0, i32 1) to i64)

define i64 @get_seed_addr() {
entry:
  ret i64 ptrtoint (ptr @seed to i64)
}

define ptr @get_null_ptr() {
entry:
  ret ptr inttoptr (i64 0 to ptr)
}

define i32 @main() {
entry:
  %a = load i64, ptr @seed_addr
  %b = load i64, ptr @field_addr
  %c = load i32, ptr @seed_roundtrip_alias
  %sum = add i64 %a, %b
  %is_nonzero = icmp ne i64 %sum, 0
  %alias_ok = icmp eq i32 %c, 7
  %all_ok = and i1 %is_nonzero, %alias_ok
  %result = select i1 %all_ok, i32 0, i32 1
  ret i32 %result
}
