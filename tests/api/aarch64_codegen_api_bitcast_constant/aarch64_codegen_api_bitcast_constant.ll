target triple = "aarch64-unknown-linux-gnu"

@seed = global i32 7
@seed_ptr = global ptr bitcast (ptr @seed to ptr)
@seed_alias = alias i32, ptr bitcast (ptr @seed to ptr)
@pair = global { i32, i32 } { i32 1, i32 9 }
@field_via_i8 = global ptr getelementptr inbounds (i8, ptr bitcast (ptr @pair to ptr), i64 4)

define i32 @read_seed(ptr %p) {
entry:
  %value = load i32, ptr %p
  ret i32 %value
}

@read_seed_alias = alias i32 (ptr), ptr bitcast (ptr @read_seed to ptr)

define i32 @main() {
entry:
  %loaded_ptr = load ptr, ptr @seed_ptr
  %a = call i32 @read_seed_alias(ptr %loaded_ptr)
  %b = load i32, ptr @seed_alias
  %field_ptr = load ptr, ptr @field_via_i8
  %c = load i32, ptr %field_ptr
  %sum1 = add i32 %a, %b
  %sum2 = add i32 %sum1, %c
  ret i32 %sum2
}
