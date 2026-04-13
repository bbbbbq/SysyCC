target triple = "aarch64-unknown-linux-gnu"

define i32 @dispatch(i32 %flag) {
entry:
  %is_zero = icmp eq i32 %flag, 0
  %dest = select i1 %is_zero, ptr blockaddress(@dispatch, %zero), ptr blockaddress(@dispatch, %nonzero)
  indirectbr ptr %dest, [label %zero, label %nonzero]

zero:
  ret i32 11

nonzero:
  ret i32 22
}

define i32 @main() {
entry:
  %a = call i32 @dispatch(i32 0)
  %b = call i32 @dispatch(i32 1)
  %sum = add i32 %a, %b
  %ok = icmp eq i32 %sum, 33
  %result = select i1 %ok, i32 0, i32 1
  ret i32 %result
}
