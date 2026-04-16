int sysycc_builtin_setjmp(void **buf);
void sysycc_builtin_longjmp(void **buf, int value) __attribute__((noreturn));

#define __builtin_setjmp(buf) sysycc_builtin_setjmp((void **)(buf))
#define __builtin_longjmp(buf, value) sysycc_builtin_longjmp((void **)(buf), (value))
