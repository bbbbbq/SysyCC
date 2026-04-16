target triple = "aarch64-unknown-linux-gnu"

@picked = global i32 select (i1 true, i32 7, i32 3)
@seed = global i32 5
@picked_ptr = global ptr select (i1 false, ptr null, ptr @seed)
@vec_sel = global <4 x i32> select (<4 x i1> <i1 true, i1 false, i1 false, i1 true>, <4 x i32> <i32 1, i32 2, i32 3, i32 4>, <4 x i32> <i32 5, i32 6, i32 7, i32 8>)
@lane_from_select = global i32 extractelement (<4 x i32> select (<4 x i1> <i1 false, i1 false, i1 true, i1 true>, <4 x i32> <i32 9, i32 10, i32 11, i32 12>, <4 x i32> <i32 13, i32 14, i32 15, i32 16>), i32 3)

define i32 @main() {
entry:
  %a = load i32, ptr @picked
  %p = load ptr, ptr @picked_ptr
  %b = load i32, ptr %p
  %vec1_ptr = getelementptr <4 x i32>, ptr @vec_sel, i32 0, i32 1
  %c = load i32, ptr %vec1_ptr
  %d = load i32, ptr @lane_from_select
  %sum0 = add i32 %a, %b
  %sum1 = add i32 %sum0, %c
  %sum2 = add i32 %sum1, %d
  %ok = icmp eq i32 %sum2, 34
  %result = select i1 %ok, i32 0, i32 1
  ret i32 %result
}
