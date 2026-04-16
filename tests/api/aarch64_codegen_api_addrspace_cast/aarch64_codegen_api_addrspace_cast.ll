target triple = "aarch64-unknown-linux-gnu"

@seed = global i32 11
@seed_as1 = global ptr addrspace(1) addrspacecast (ptr @seed to ptr addrspace(1))

define ptr @roundtrip() {
entry:
  %p1 = addrspacecast ptr @seed to ptr addrspace(1)
  %p0 = addrspacecast ptr addrspace(1) %p1 to ptr
  ret ptr %p0
}

define i32 @main() {
entry:
  %p = call ptr @roundtrip()
  %v = load i32, ptr %p
  %gp = load ptr addrspace(1), ptr @seed_as1
  %gp0 = addrspacecast ptr addrspace(1) %gp to ptr
  %v2 = load i32, ptr %gp0
  %sum = add i32 %v, %v2
  %ok = icmp eq i32 %sum, 22
  %result = select i1 %ok, i32 0, i32 1
  ret i32 %result
}
