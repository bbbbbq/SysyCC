target triple = "aarch64-unknown-linux-gnu"

@seed = global i32 7
@ptr_global = global ptr @seed
@ptr_array = global [1 x ptr] [ ptr @seed ]

define i32 @main() {
entry:
  %p1 = load ptr, ptr @ptr_global
  %v1 = load i32, ptr %p1
  %slot = getelementptr inbounds [1 x ptr], ptr @ptr_array, i32 0, i32 0
  %p2 = load ptr, ptr %slot
  %v2 = load i32, ptr %p2
  %sum = add i32 %v1, %v2
  ret i32 %sum
}
