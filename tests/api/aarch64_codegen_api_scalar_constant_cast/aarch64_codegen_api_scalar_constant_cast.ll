target triple = "aarch64-unknown-linux-gnu"

@trunc_i32 = global i32 trunc (i64 4294967297 to i32)
@zext_i64 = global i64 zext (i8 255 to i64)
@sext_i64 = global i64 sext (i8 -1 to i64)
@flt32 = global float sitofp (i32 3 to float)
@dbl_to_i32 = global i32 fptosi (double 7.75 to i32)
@bit_bits = global i32 bitcast (float 1.0 to i32)

define i32 @main() {
entry:
  %a = load i32, ptr @trunc_i32
  %b = load i64, ptr @zext_i64
  %c = load i64, ptr @sext_i64
  %d = load float, ptr @flt32
  %e = load i32, ptr @dbl_to_i32
  %f = load i32, ptr @bit_bits
  %d_i32 = fptosi float %d to i32
  %sum0 = add i32 %a, %d_i32
  %sum1 = add i32 %sum0, %e
  %a_ok = icmp eq i32 %a, 1
  %b_ok = icmp eq i64 %b, 255
  %c_ok = icmp eq i64 %c, -1
  %sum_ok = icmp eq i32 %sum1, 11
  %bit_ok = icmp eq i32 %f, 1065353216
  %all0 = and i1 %a_ok, %b_ok
  %all1 = and i1 %all0, %c_ok
  %all2 = and i1 %all1, %sum_ok
  %all3 = and i1 %all2, %bit_ok
  %result = select i1 %all3, i32 0, i32 1
  ret i32 %result
}
