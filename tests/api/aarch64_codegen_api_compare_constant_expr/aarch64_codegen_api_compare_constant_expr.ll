target triple = "aarch64-unknown-linux-gnu"

@seed = global i32 5
@icmp_gt = global i1 icmp sgt (i32 7, i32 3)
@fcmp_eq = global i1 fcmp oeq (double 2.5, double 2.5)
@ptr_eq = global i1 icmp eq (ptr @seed, ptr @seed)
@ptr_ne = global i1 icmp ne (ptr @seed, ptr null)
@vec_cmp = global <4 x i1> icmp sgt (<4 x i32> <i32 1, i32 2, i32 3, i32 4>, <4 x i32> <i32 0, i32 3, i32 3, i32 1>)
@lane_from_cmp_select = global i32 extractelement (<4 x i32> select (<4 x i1> icmp sgt (<4 x i32> <i32 1, i32 2, i32 3, i32 4>, <4 x i32> <i32 0, i32 3, i32 3, i32 1>), <4 x i32> <i32 10, i32 20, i32 30, i32 40>, <4 x i32> <i32 5, i32 6, i32 7, i32 8>), i32 3)

define i32 @main() {
entry:
  %a = load i1, ptr @icmp_gt
  %a_i32 = select i1 %a, i32 1, i32 0
  %b = load i1, ptr @fcmp_eq
  %b_i32 = select i1 %b, i32 1, i32 0
  %c = load i1, ptr @ptr_eq
  %c_i32 = select i1 %c, i32 1, i32 0
  %d = load i1, ptr @ptr_ne
  %d_i32 = select i1 %d, i32 1, i32 0
  %lane1_ptr = getelementptr <4 x i1>, ptr @vec_cmp, i32 0, i32 1
  %lane1 = load i1, ptr %lane1_ptr
  %lane1_i32 = select i1 %lane1, i32 1, i32 0
  %lane3_ptr = getelementptr <4 x i1>, ptr @vec_cmp, i32 0, i32 3
  %lane3 = load i1, ptr %lane3_ptr
  %lane3_i32 = select i1 %lane3, i32 1, i32 0
  %picked = load i32, ptr @lane_from_cmp_select
  %sum0 = add i32 %a_i32, %b_i32
  %sum1 = add i32 %sum0, %c_i32
  %sum2 = add i32 %sum1, %d_i32
  %sum3 = add i32 %sum2, %lane1_i32
  %sum4 = add i32 %sum3, %lane3_i32
  %sum5 = add i32 %sum4, %picked
  %ok = icmp eq i32 %sum5, 45
  %result = select i1 %ok, i32 0, i32 1
  ret i32 %result
}
