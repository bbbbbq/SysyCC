target triple = "aarch64-unknown-linux-gnu"

define i32 @add(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}

define i32 @main() {
entry:
  %always = icmp eq i32 0, 0
  %callee = select i1 %always, ptr @add, ptr @add
  %sum = call i32 %callee(i32 4, i32 5)
  ret i32 %sum
}
