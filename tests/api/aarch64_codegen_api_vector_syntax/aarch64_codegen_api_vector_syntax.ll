target triple = "aarch64-unknown-linux-gnu"

define i32 @vec_demo() {
entry:
  %vec1 = insertelement <4 x i32> zeroinitializer, i32 1, i32 0
  %vec2 = insertelement <4 x i32> %vec1, i32 2, i32 1
  %vec3 = insertelement <4 x i32> %vec2, i32 3, i32 2
  %vec4 = insertelement <4 x i32> %vec3, i32 4, i32 3
  %shuf = shufflevector <4 x i32> %vec4, <4 x i32> zeroinitializer, <4 x i32> <i32 3, i32 2, i32 1, i32 0>
  %elt = extractelement <4 x i32> %shuf, i32 0
  %sum = call i32 @llvm.vector.reduce.add.v4i32(<4 x i32> %shuf)
  %out = add i32 %elt, %sum
  ret i32 %out
}

declare i32 @llvm.vector.reduce.add.v4i32(<4 x i32>)
