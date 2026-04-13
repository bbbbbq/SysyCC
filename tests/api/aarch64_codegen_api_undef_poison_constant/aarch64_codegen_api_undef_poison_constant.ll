target triple = "aarch64-unknown-linux-gnu"

@u_i32 = global i32 undef
@p_i64 = global i64 poison
@u_ptr = global ptr undef
@p_vec = global <4 x i32> poison
@picked = global i32 select (i1 undef, i32 7, i32 3)
@cmp_from_poison = global i1 icmp eq (i32 poison, i32 0)
@lane_psel = global i32 extractelement (<4 x i32> select (<4 x i1> poison, <4 x i32> <i32 1, i32 2, i32 3, i32 4>, <4 x i32> <i32 5, i32 6, i32 7, i32 8>), i32 1)

define i32 @main() {
entry:
  %a = load i32, ptr @u_i32
  %b = load i64, ptr @p_i64
  %p = load ptr, ptr @u_ptr
  %p_is_null = icmp eq ptr %p, null
  %p_i32 = select i1 %p_is_null, i32 1, i32 0
  %vec_lane_ptr = getelementptr <4 x i32>, ptr @p_vec, i32 0, i32 2
  %vec_lane = load i32, ptr %vec_lane_ptr
  %picked_val = load i32, ptr @picked
  %cmp_val = load i1, ptr @cmp_from_poison
  %cmp_i32 = select i1 %cmp_val, i32 1, i32 0
  %lane = load i32, ptr @lane_psel
  %b_trunc = trunc i64 %b to i32
  %sum0 = add i32 %a, %b_trunc
  %sum1 = add i32 %sum0, %p_i32
  %sum2 = add i32 %sum1, %vec_lane
  %sum3 = add i32 %sum2, %picked_val
  %sum4 = add i32 %sum3, %cmp_i32
  %sum5 = add i32 %sum4, %lane
  %ok = icmp eq i32 %sum5, 11
  %result = select i1 %ok, i32 0, i32 1
  ret i32 %result
}
