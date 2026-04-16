target triple = "aarch64-unknown-linux-gnu"

%Pair = type { i32, i32 }

@items = global [4 x i32] [i32 1, i32 3, i32 5, i32 7]
@plus_one = global ptr getelementptr inbounds ([4 x i32], ptr @items, i32 0, i32 1)
@pair = global %Pair { i32 11, i32 13 }
@payload = global { i32 } { i32 19 }
@q = global ptr getelementptr inbounds ([4 x i32], ptr @items, i32 0, i32 3)
@g_ptr = global ptr getelementptr inbounds ({ i32 }, ptr @payload, i32 0, i32 0)
@field_ptrs = global [1 x ptr] [ptr getelementptr inbounds (%Pair, ptr @pair, i32 0, i32 1)]

define i32 @main() {
entry:
  %p1 = load ptr, ptr @q
  %v1 = load i32, ptr %p1
  %slot = getelementptr inbounds [1 x ptr], ptr @field_ptrs, i32 0, i32 0
  %p2 = load ptr, ptr %slot
  %v2 = load i32, ptr %p2
  %sum = add i32 %v1, %v2
  ret i32 %sum
}
