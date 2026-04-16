target triple = "aarch64-unknown-linux-gnu"

@lane = global i32 extractelement (<4 x i32> <i32 1, i32 2, i32 3, i32 4>, i32 2)
@vec_insert = global <4 x i32> insertelement (<4 x i32> zeroinitializer, i32 9, i32 1)
@vec_shuffle = global <4 x i32> shufflevector (<4 x i32> <i32 1, i32 2, i32 3, i32 4>, <4 x i32> <i32 5, i32 6, i32 7, i32 8>, <4 x i32> <i32 3, i32 4, i32 1, i32 6>)
@lane_nested = global i32 extractelement (<4 x i32> insertelement (<4 x i32> zeroinitializer, i32 11, i32 2), i32 2)

define i32 @main() {
entry:
  %lane_value = load i32, ptr @lane

  %insert_lane_ptr = getelementptr <4 x i32>, ptr @vec_insert, i32 0, i32 1
  %insert_lane = load i32, ptr %insert_lane_ptr

  %shuffle0_ptr = getelementptr <4 x i32>, ptr @vec_shuffle, i32 0, i32 0
  %shuffle0 = load i32, ptr %shuffle0_ptr
  %shuffle1_ptr = getelementptr <4 x i32>, ptr @vec_shuffle, i32 0, i32 1
  %shuffle1 = load i32, ptr %shuffle1_ptr
  %shuffle3_ptr = getelementptr <4 x i32>, ptr @vec_shuffle, i32 0, i32 3
  %shuffle3 = load i32, ptr %shuffle3_ptr

  %nested_lane = load i32, ptr @lane_nested

  %sum0 = add i32 %lane_value, %insert_lane
  %sum1 = add i32 %sum0, %shuffle0
  %sum2 = add i32 %sum1, %shuffle1
  %sum3 = add i32 %sum2, %shuffle3
  %sum4 = add i32 %sum3, %nested_lane
  %ok = icmp eq i32 %sum4, 39
  %result = select i1 %ok, i32 0, i32 1
  ret i32 %result
}
