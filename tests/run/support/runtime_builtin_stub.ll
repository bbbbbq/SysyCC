define float @__builtin_fabsf(float %value) {
entry:
  %neg = fneg float %value
  %cmp = fcmp olt float %value, 0.0000000000000000e+00
  %res = select i1 %cmp, float %neg, float %value
  ret float %res
}

define double @__builtin_fabs(double %value) {
entry:
  %neg = fneg double %value
  %cmp = fcmp olt double %value, 0.0000000000000000e+00
  %res = select i1 %cmp, double %neg, double %value
  ret double %res
}

define fp128 @__builtin_fabsl(fp128 %value) {
entry:
  %neg = fneg fp128 %value
  %zero = fpext double 0.0000000000000000e+00 to fp128
  %cmp = fcmp olt fp128 %value, %zero
  %res = select i1 %cmp, fp128 %neg, fp128 %value
  ret fp128 %res
}

define float @__builtin_inff() {
entry:
  ret float 0x7FF0000000000000
}

define double @__builtin_inf() {
entry:
  ret double 0x7FF0000000000000
}

define fp128 @__builtin_infl() {
entry:
  %inf = fpext double 0x7FF0000000000000 to fp128
  ret fp128 %inf
}
