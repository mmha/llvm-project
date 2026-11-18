// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -verify -DINT128 -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +cx16 -fclangir -emit-cir %s -verify -DLONG_DOUBLE -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -verify -DFENV_ACCESS -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -verify -DCOMPLEX -o /dev/null

#ifdef INT128
_Atomic __int128 i128;

__int128 mul_int128(__int128 x) {
  return i128 *= x; // expected-error {{ClangIR code gen Not Yet Implemented: atomic compare-exchange loop: library call}}
}
#endif

#ifdef LONG_DOUBLE
// With cx16 the 16-byte storage is lock-free, but the 80-bit value does not
// fill it.
_Atomic long double ld;

long double add_long_double(void) {
  return ld += 1; // expected-error {{ClangIR code gen Not Yet Implemented: atomic compare-exchange loop: value type}}
}
#endif

#ifdef FENV_ACCESS
float mul_float_fenv(_Atomic float *p, float x) {
#pragma STDC FENV_ACCESS ON
  return *p *= x; // expected-error {{ClangIR code gen Not Yet Implemented: atomic compound assignment with observable floating-point exceptions}}
}
#endif

#ifdef COMPLEX
_Bool add_bool_complex(_Atomic _Bool *p, double _Complex x) {
  return *p += x; // expected-error {{ClangIR code gen Not Yet Implemented: atomic compound assignment with a complex operand}}
}
#endif
