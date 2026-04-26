target triple = "aarch64-unknown-linux-gnu"

define i32 @vec_demo() {
entry:
  %vec1 = insertelement <4 x i32> zeroinitializer, i32 1, i32 0
  %vec2 = insertelement <4 x i32> %vec1, i32 2, i32 1
  %vec3 = insertelement <4 x i32> %vec2, i32 3, i32 2
  %vec4 = insertelement <4 x i32> %vec3, i32 4, i32 3
  %shuf = shufflevector <4 x i32> %vec4, <4 x i32> zeroinitializer, <4 x i32> <i32 3, i32 2, i32 1, i32 0>
  %splat = shufflevector <4 x i32> %vec4, <4 x i32> zeroinitializer, <4 x i32> zeroinitializer
  %addv = add <4 x i32> %shuf, %splat
  %mulv = mul <4 x i32> %addv, %vec4
  %minv = call <4 x i32> @llvm.smin.v4i32(<4 x i32> %mulv, <4 x i32> %addv)
  %maxv = call <4 x i32> @llvm.smax.v4i32(<4 x i32> %minv, <4 x i32> %vec4)
  %elt = extractelement <4 x i32> %maxv, i32 0
  %sum = call i32 @llvm.vector.reduce.add.v4i32(<4 x i32> %maxv)
  %min = call i32 @llvm.vector.reduce.smin.v4i32(<4 x i32> %maxv)
  %max = call i32 @llvm.vector.reduce.smax.v4i32(<4 x i32> %maxv)
  %out0 = add i32 %elt, %sum
  %out1 = add i32 %out0, %min
  %out2 = add i32 %out1, %max
  ret i32 %out2
}

declare <4 x i32> @llvm.smin.v4i32(<4 x i32>, <4 x i32>)
declare <4 x i32> @llvm.smax.v4i32(<4 x i32>, <4 x i32>)
declare i32 @llvm.vector.reduce.add.v4i32(<4 x i32>)
declare i32 @llvm.vector.reduce.smin.v4i32(<4 x i32>)
declare i32 @llvm.vector.reduce.smax.v4i32(<4 x i32>)
