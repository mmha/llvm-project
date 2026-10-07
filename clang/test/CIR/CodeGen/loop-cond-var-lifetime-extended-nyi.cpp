// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -DWHILE %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=WHILE --implicit-check-not=error:
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -O1 -fcxx-exceptions -fexceptions -fclangir -emit-cir -DWHILE %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=WHILE --implicit-check-not=error:
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -DFOR %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=FOR --implicit-check-not=error:
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -O1 -fcxx-exceptions -fexceptions -fclangir -emit-cir -DFOR %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=FOR --implicit-check-not=error:
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -DRVALUE_REF %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=RVALUE-REF --implicit-check-not=error:
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir -DAGGREGATE %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=AGGREGATE --implicit-check-not=error:
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -O1 -fcxx-exceptions -fexceptions -fclangir -emit-cir -DTRIVIAL %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=TRIVIAL

// A temporary that the initializer of a loop's condition variable
// lifetime-extends must be destroyed with the variable at the end of each
// iteration. CIRGen does not capture its cleanup into the loop op's cleanup
// region yet. Report that instead of producing a cir.condition that the
// verifier rejects (-O0) or failing an assertion (-O1, where the variable has
// lifetime markers).

struct S {
  S();
  ~S();
  operator bool() const;
};
S make();
void use(const S &);

struct Agg {
  const S &r;
  operator bool() const;
};

int geti();

#ifdef WHILE
void while_const_ref() {
  // WHILE: [[@LINE+1]]:10: error: ClangIR code gen Not Yet Implemented: loop condition variable with a lifetime-extended temporary
  while (const S &s = make())
    use(s);
}
#endif

#ifdef FOR
void for_const_ref() {
  // FOR: [[@LINE+1]]:10: error: ClangIR code gen Not Yet Implemented: loop condition variable with a lifetime-extended temporary
  for (; const S &s = make();)
    use(s);
}
#endif

#ifdef RVALUE_REF
void while_rvalue_ref() {
  // RVALUE-REF: [[@LINE+1]]:10: error: ClangIR code gen Not Yet Implemented: loop condition variable with a lifetime-extended temporary
  while (S &&s = make())
    use(s);
}
#endif

#ifdef AGGREGATE
void while_aggregate_ref_member() {
  // AGGREGATE: [[@LINE+1]]:10: error: ClangIR code gen Not Yet Implemented: loop condition variable with a lifetime-extended temporary
  while (Agg a = {make()})
    use(a.r);
}
#endif

#ifdef TRIVIAL
// The temporary needs no cleanup, so nothing is left to capture.
void while_const_ref_trivial() {
  while (const int &i = geti())
    (void)i;
}

// TRIVIAL-LABEL: cir.func {{.*}} @_Z23while_const_ref_trivialv()
// TRIVIAL:         cir.while {
// TRIVIAL:           cir.call @_Z4getiv()
// TRIVIAL:           cir.condition
// TRIVIAL:         } cleanup all {
// TRIVIAL-NEXT:      cir.lifetime.end
#endif
