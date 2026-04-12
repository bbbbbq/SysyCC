source_filename = "aarch64_codegen_api_rich_syntax.c"
target triple = "aarch64-unknown-linux-gnu"
module asm ".p2align 3"
$synthetic = comdat any

%Pair = type { i32, i32 }

@pair_global = dso_local global %Pair { i32 7, i32 11 }, align 4
@pair_alias = alias %Pair, ptr @pair_global

define dso_local i32 @sum_pair(ptr nocapture noundef %p) local_unnamed_addr #0 !dbg !10 {
entry:
  %lhs.ptr = getelementptr inbounds %Pair, ptr %p, i32 0, i32 0, !dbg !11
  %lhs = load i32, ptr %lhs.ptr, align 4, !dbg !12
  %rhs.ptr = getelementptr inbounds %Pair, ptr %p, i32 0, i32 1
  %rhs = load i32, ptr %rhs.ptr, align 4
  %sum = add nsw i32 %lhs, %rhs
  ret i32 %sum, !dbg !13
}

@sum_pair_alias = alias i32 (ptr), ptr @sum_pair

define dso_local i32 @main() #0 {
entry:
  %call = call noundef i32 @sum_pair_alias(ptr noundef @pair_alias)
  %is_nonzero = icmp ne i32 %call, 0
  %picked = select i1 %is_nonzero, i32 %call, i32 0
  ret i32 %picked
}

attributes #0 = { nounwind }
!llvm.module.flags = !{!0}
!0 = !{i32 1, !"wchar_size", i32 4}
!10 = !{}
!11 = !{}
!12 = !{}
!13 = !{}
