target triple = "aarch64-unknown-linux-gnu"

@values = global [4 x i32] zeroinitializer

define i32 @add(i32 %lhs, i32 %rhs) {
entry:
  %sum = add i32 %lhs, %rhs
  ret i32 %sum
}

define i32 @main() {
entry:
  %cmp = icmp slt i32 1, 2
  br i1 %cmp, label %then, label %else
then:
  %call = call i32 @add(i32 5, i32 6)
  br label %exit
else:
  br label %exit
exit:
  %phi = phi i32 [ %call, %then ], [ 0, %else ]
  %addr = getelementptr inbounds [4 x i32], ptr @values, i32 0, i32 2
  store i32 %phi, ptr %addr
  %loaded = load i32, ptr %addr
  ret i32 %loaded
}
