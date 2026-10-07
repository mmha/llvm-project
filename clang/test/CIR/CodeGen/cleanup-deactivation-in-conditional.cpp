// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefixes=CIR,CIR-NOEH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t-eh.cir
// RUN: FileCheck --input-file=%t-eh.cir %s -check-prefixes=CIR,CIR-EH
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// The members that an aggregate initialization has constructed so far are
// destroyed when a later member initializer is left early, here by a return
// in a statement expression in an operand of a conditional operator. The
// member cleanups are deactivated at the end of the initialization, while the
// cleanup scope that the conditional operator opened for its operands is still
// open: the flag that guards the cleanup of the first member is cleared inside
// that scope.

struct Obj {
  char c;
  Obj(char);
  ~Obj();
};

struct Pair {
  Obj a;
  Obj b;
};

Obj copy(const Obj &);
Obj make(bool);

void struct_ternary(bool b, bool c) {
  Pair p{Obj{'a'}, b ? ({
                     if (c)
                       return;
                     Obj{'b'};
                   })
                     : Obj{'c'}};
}

// The cleanup scope of the conditional operator is empty and canonicalized
// away.
//
// CIR-LABEL: cir.func {{.*}} @_Z14struct_ternarybb(
// CIR:         %[[P:.*]] = cir.alloca "p"
// CIR:         %[[ACTIVE:.*]] = cir.alloca "cleanup.isactive"
// CIR:         %[[A:.*]] = cir.get_member %[[P]][0] {name = "a"}
// CIR:         cir.call @_ZN3ObjC1Ec(%[[A]],
// CIR-NEXT:    %[[TRUE:.*]] = cir.const #true
// CIR-NEXT:    cir.store %[[TRUE]], %[[ACTIVE]]
// CIR-NEXT:    cir.cleanup.scope {
// CIR:           cir.if
// CIR:             cir.if
// CIR-NEXT:          cir.return
// CIR:           } else {
// CIR:             cir.call @_ZN3ObjC1Ec(
// CIR-NEXT:      } loc(
// CIR-NEXT:      %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:      cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:      %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:      cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:        cir.call @_ZN3ObjD1Ev(%[[A]])
// CIR:         cir.call @_ZN4PairD1Ev(%[[P]])

// The return leaves through the cleanup, which destroys the first member.
// Completing the initialization clears the flag.
//
// LLVM-LABEL: define dso_local void @_Z14struct_ternarybb(
// LLVM:         %[[A:.*]] = getelementptr inbounds nuw %struct.Pair, ptr %{{.*}}, i32 0, i32 0
// LLVM-NEXT:    call void @_ZN3ObjC1Ec(ptr {{.*}} %[[A]], i8 noundef signext 97)
// LLVM-NEXT:    store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}}, i8 noundef signext 98)
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}}, i8 noundef signext 99)
// LLVM-NEXT:    br label %[[END:[0-9]+]]
// LLVM:       [[END]]:
// LLVM-NEXT:    store i8 0, ptr %[[ACTIVE]]
// LLVM-NEXT:    store i32 0, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[CLEANUP]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %[[DESTROY:[0-9]+]], label %{{.*}}
// LLVM:       [[DESTROY]]:
// LLVM-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[A]])
// LLVM:         call void @_ZN4PairD1Ev(
// LLVM:         ret void

// OGCG-LABEL: define dso_local void @_Z14struct_ternarybb(
// OGCG:         %[[A:.*]] = getelementptr inbounds nuw %struct.Pair, ptr %p, i32 0, i32 0
// OGCG:       if.then:
// OGCG-NEXT:    call void @_ZN3ObjD1Ev(ptr {{.*}} %[[A]])
// OGCG-NEXT:    br label %return
// OGCG:       cond.end:
// OGCG-NEXT:    call void @_ZN4PairD1Ev(

// With a temporary in the other operand, the cleanup scope of the conditional
// operator destroys it at the end of the full-expression, after the flag of the
// first member has been cleared inside it.

void ternary_temporary(bool b, bool c) {
  Pair p{Obj{'a'}, b ? ({
                     if (c)
                       return;
                     Obj{'b'};
                   })
                     : copy(Obj{'t'})};
}

// CIR-LABEL: cir.func {{.*}} @_Z17ternary_temporarybb(
// CIR:         %[[P:.*]] = cir.alloca "p"
// CIR:         %[[TMP:.*]] = cir.alloca "ref.tmp0"
// CIR:         %[[COND:.*]] = cir.alloca "cleanup.cond"
// CIR:         %[[ACTIVE:.*]] = cir.alloca "cleanup.isactive"
// CIR:         %[[A:.*]] = cir.get_member %[[P]][0] {name = "a"}
// CIR:         cir.store %{{.*}}, %[[ACTIVE]]
// CIR-NEXT:    cir.cleanup.scope {
// CIR:           cir.cleanup.scope {
// CIR:             cir.if
// CIR:               cir.if
// CIR-NEXT:            cir.return
// CIR:             } else {
// CIR:               cir.call @_ZN3ObjC1Ec(%[[TMP]],
// CIR:               cir.call @_Z4copyRK3Obj(
// CIR-NEXT:        } loc(
// CIR-NEXT:        %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:        cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:        cir.yield
// CIR-NOEH-NEXT:   } cleanup normal {
// CIR-EH-NEXT:     } cleanup all {
// CIR-NEXT:        %[[TMP_ACTIVE:.*]] = cir.load {{.*}} %[[COND]]
// CIR-NEXT:        cir.if %[[TMP_ACTIVE]] {
// CIR-NEXT:          cir.call @_ZN3ObjD1Ev(%[[TMP]])
// CIR:             cir.yield
// CIR-NEXT:      } loc(
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:      %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:      cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:        cir.call @_ZN3ObjD1Ev(%[[A]])

// LLVM-LABEL: define dso_local void @_Z17ternary_temporarybb(
// LLVM:         call void @_Z4copyRK3Obj(
// LLVM-NEXT:    br label %[[END:[0-9]+]]
// LLVM:       [[END]]:
// LLVM-NEXT:    store i8 0, ptr %[[ACTIVE:[0-9]+]]
// LLVM-NEXT:    store i32 0, ptr %{{.*}}
// LLVM-NEXT:    br label %[[TMP_CLEANUP:[0-9]+]]
// LLVM:       [[TMP_CLEANUP]]:
// LLVM-NEXT:    load i8, ptr %{{[0-9]+}}
// LLVM:         call void @_ZN3ObjD1Ev(
// LLVM:         load i8, ptr %[[ACTIVE]]
// LLVM:         call void @_ZN3ObjD1Ev(
// LLVM:         call void @_ZN4PairD1Ev(

// OGCG-LABEL: define dso_local void @_Z17ternary_temporarybb(
// OGCG:       cond.end:
// OGCG-NEXT:    store i1 false, ptr %cleanup.isactive
// OGCG-NEXT:    %cleanup.is_active = load i1, ptr %cleanup.cond

// A logical operator opens such a scope too.

void logical_and(bool b, bool c) {
  Pair p{Obj{'a'}, make(b && ({
                          if (c)
                            return;
                          true;
                        }))};
}

// CIR-LABEL: cir.func {{.*}} @_Z11logical_andbb(
// CIR:         %[[P:.*]] = cir.alloca "p"
// CIR:         %[[ACTIVE:.*]] = cir.alloca "cleanup.isactive"
// CIR:         %[[A:.*]] = cir.get_member %[[P]][0] {name = "a"}
// CIR:         cir.store %{{.*}}, %[[ACTIVE]]
// CIR-NEXT:    cir.cleanup.scope {
// CIR:           cir.ternary
// CIR:             cir.return
// CIR:           cir.call @_Z4makeb(
// CIR-NEXT:      %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:      cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:      %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:      cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:        cir.call @_ZN3ObjD1Ev(%[[A]])

// LLVM-LABEL: define dso_local void @_Z11logical_andbb(
// LLVM:         call void @_Z4makeb(
// LLVM-NEXT:    store i8 0, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         load i8, ptr %[[ACTIVE]]
// LLVM:         call void @_ZN3ObjD1Ev(

// OGCG-LABEL: define dso_local void @_Z11logical_andbb(
// OGCG:       if.then:
// OGCG-NEXT:    call void @_ZN3ObjD1Ev(

// The partial array destroy of an array initialization is deactivated at the
// end of the initialization as well.

void array_ternary(bool b, bool c) {
  Obj arr[2] = {Obj{'a'}, b ? ({
                            if (c)
                              return;
                            Obj{'b'};
                          })
                            : Obj{'c'}};
}

// CIR-LABEL: cir.func {{.*}} @_Z13array_ternarybb(
// CIR:         %[[ARR:.*]] = cir.alloca "arr"
// CIR:         %[[END:.*]] = cir.alloca "arrayinit.endOfInit"
// CIR:         %[[ACTIVE:.*]] = cir.alloca "cleanup.isactive"
// CIR:         %[[BEGIN:.*]] = cir.cast array_to_ptrdecay %[[ARR]]
// CIR:         cir.store %{{.*}}, %[[ACTIVE]]
// CIR-NEXT:    cir.cleanup.scope {
// CIR:           cir.call @_ZN3ObjC1Ec(%[[BEGIN]],
// CIR:           %[[SECOND:.*]] = cir.ptr_stride %[[BEGIN]]
// CIR-NEXT:      cir.store {{.*}} %[[SECOND]], %[[END]]
// CIR:           cir.if
// CIR:             cir.if
// CIR-NEXT:          cir.return
// CIR:           } else {
// CIR:             cir.call @_ZN3ObjC1Ec(%[[SECOND]],
// CIR-NEXT:      } loc(
// CIR-NEXT:      %[[FALSE:.*]] = cir.const #false
// CIR-NEXT:      cir.store %[[FALSE]], %[[ACTIVE]]
// CIR-NEXT:      cir.yield
// CIR-NOEH-NEXT: } cleanup normal {
// CIR-EH-NEXT:   } cleanup all {
// CIR-NEXT:      %[[IS_ACTIVE:.*]] = cir.load {{.*}} %[[ACTIVE]]
// CIR-NEXT:      cir.if %[[IS_ACTIVE]] {
// CIR-NEXT:        cir.load {{.*}} %[[END]]
// CIR:             cir.do {
// CIR:               cir.call @_ZN3ObjD1Ev(

// LLVM-LABEL: define dso_local void @_Z13array_ternarybb(
// LLVM:         store i8 1, ptr %[[ACTIVE:[0-9]+]]
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}}, i8 noundef signext 97)
// LLVM:         store i32 1, ptr %[[SLOT:[0-9]+]]
// LLVM-NEXT:    br label %[[CLEANUP:[0-9]+]]
// LLVM:         call void @_ZN3ObjC1Ec(ptr {{.*}}, i8 noundef signext 99)
// LLVM-NEXT:    br label %[[END:[0-9]+]]
// LLVM:       [[END]]:
// LLVM-NEXT:    store i8 0, ptr %[[ACTIVE]]
// LLVM-NEXT:    store i32 0, ptr %[[SLOT]]
// LLVM-NEXT:    br label %[[CLEANUP]]
// LLVM:       [[CLEANUP]]:
// LLVM-NEXT:    %[[FLAG:.*]] = load i8, ptr %[[ACTIVE]]
// LLVM-NEXT:    %[[IS_ACTIVE:.*]] = trunc i8 %[[FLAG]] to i1
// LLVM-NEXT:    br i1 %[[IS_ACTIVE]], label %{{.*}}, label %{{.*}}
// LLVM:         call void @_ZN3ObjD1Ev(

// OGCG-LABEL: define dso_local void @_Z13array_ternarybb(
// OGCG:       if.then:
// OGCG-NEXT:    load ptr, ptr %arrayinit.endOfInit
// OGCG:       arraydestroy.body:
// OGCG:         call void @_ZN3ObjD1Ev(
