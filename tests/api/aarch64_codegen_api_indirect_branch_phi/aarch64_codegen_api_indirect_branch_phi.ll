target triple = "aarch64-unknown-linux-gnu"

define i32 @dispatch(i32 %flag) {
entry:
  indirectbr ptr blockaddress(@dispatch, %target), [label %target]

target:
  %z = phi i32 [11, %entry]
  ret i32 %z
}

define i32 @main() {
entry:
  %a = call i32 @dispatch(i32 0)
  %ok = icmp eq i32 %a, 11
  %result = select i1 %ok, i32 0, i32 1
  ret i32 %result
}
