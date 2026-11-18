// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

int compound_assign(int b) {
  int x = 1;
  x *= b;
  x /= b;
  x %= b;
  x += b;
  x -= b;
  x >>= b;
  x <<= b;
  x &= b;
  x ^= b;
  x |= b;
  return x;
}

// CIR: cir.func{{.*}} @_Z15compound_assigni
// CIR:   %[[MUL:.*]] = cir.mul nsw %{{.*}}, %{{.*}} : !s32i
// CIR:   cir.store{{.*}} %[[MUL]], %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR:   %[[DIV:.*]] = cir.div %{{.*}}, %{{.*}} : !s32i
// CIR:   cir.store{{.*}} %[[DIV]], %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR:   %[[REM:.*]] = cir.rem %{{.*}}, %{{.*}} : !s32i
// CIR:   cir.store{{.*}} %[[REM]], %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR:   %[[ADD:.*]] = cir.add nsw %{{.*}}, %{{.*}} : !s32i
// CIR:   cir.store{{.*}} %[[ADD]], %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR:   %[[SUB:.*]] = cir.sub nsw %{{.*}}, %{{.*}} : !s32i
// CIR:   cir.store{{.*}} %[[SUB]], %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR:   %[[SHR:.*]] = cir.shift(right, %{{.*}} : !s32i, %{{.*}} : !s32i) -> !s32i
// CIR:   cir.store{{.*}} %[[SHR]], %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR:   %[[SHL:.*]] = cir.shift(left, %{{.*}} : !s32i, %{{.*}} : !s32i) -> !s32i
// CIR:   cir.store{{.*}} %[[SHL]], %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR:   %[[AND:.*]] = cir.and %{{.*}}, %{{.*}} : !s32i
// CIR:   cir.store{{.*}} %[[AND]], %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR:   %[[XOR:.*]] = cir.xor %{{.*}}, %{{.*}} : !s32i
// CIR:   cir.store{{.*}} %[[XOR]], %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR:   %[[OR:.*]] = cir.or %{{.*}}, %{{.*}} : !s32i
// CIR:   cir.store{{.*}} %[[OR]], %{{.*}} : !s32i, !cir.ptr<!s32i>

// LLVM: define {{.*}}i32 @_Z15compound_assigni
// LLVM:   %[[MUL:.*]] = mul nsw i32 %{{.*}}, %{{.*}}
// LLVM:   store i32 %[[MUL]], ptr %{{.*}}
// LLVM:   %[[DIV:.*]] = sdiv i32 %{{.*}}, %{{.*}}
// LLVM:   store i32 %[[DIV]], ptr %{{.*}}
// LLVM:   %[[REM:.*]] = srem i32 %{{.*}}, %{{.*}}
// LLVM:   store i32 %[[REM]], ptr %{{.*}}
// LLVM:   %[[ADD:.*]] = add nsw i32 %{{.*}}, %{{.*}}
// LLVM:   store i32 %[[ADD]], ptr %{{.*}}
// LLVM:   %[[SUB:.*]] = sub nsw i32 %{{.*}}, %{{.*}}
// LLVM:   store i32 %[[SUB]], ptr %{{.*}}
// LLVM:   %[[SHR:.*]] = ashr i32 %{{.*}}, %{{.*}}
// LLVM:   store i32 %[[SHR]], ptr %{{.*}}
// LLVM:   %[[SHL:.*]] = shl i32 %{{.*}}, %{{.*}}
// LLVM:   store i32 %[[SHL]], ptr %{{.*}}
// LLVM:   %[[AND:.*]] = and i32 %{{.*}}, %{{.*}}
// LLVM:   store i32 %[[AND]], ptr %{{.*}}
// LLVM:   %[[XOR:.*]] = xor i32 %{{.*}}, %{{.*}}
// LLVM:   store i32 %[[XOR]], ptr %{{.*}}
// LLVM:   %[[OR:.*]] = or i32 %{{.*}}, %{{.*}}
// LLVM:   store i32 %[[OR]], ptr %{{.*}}

// OGCG: define {{.*}}i32 @_Z15compound_assigni
// OGCG:   %[[MUL:.*]] = mul nsw i32 %{{.*}}, %{{.*}}
// OGCG:   store i32 %[[MUL]], ptr %{{.*}}
// OGCG:   %[[DIV:.*]] = sdiv i32 %{{.*}}, %{{.*}}
// OGCG:   store i32 %[[DIV]], ptr %{{.*}}
// OGCG:   %[[REM:.*]] = srem i32 %{{.*}}, %{{.*}}
// OGCG:   store i32 %[[REM]], ptr %{{.*}}
// OGCG:   %[[ADD:.*]] = add nsw i32 %{{.*}}, %{{.*}}
// OGCG:   store i32 %[[ADD]], ptr %{{.*}}
// OGCG:   %[[SUB:.*]] = sub nsw i32 %{{.*}}, %{{.*}}
// OGCG:   store i32 %[[SUB]], ptr %{{.*}}
// OGCG:   %[[SHR:.*]] = ashr i32 %{{.*}}, %{{.*}}
// OGCG:   store i32 %[[SHR]], ptr %{{.*}}
// OGCG:   %[[SHL:.*]] = shl i32 %{{.*}}, %{{.*}}
// OGCG:   store i32 %[[SHL]], ptr %{{.*}}
// OGCG:   %[[AND:.*]] = and i32 %{{.*}}, %{{.*}}
// OGCG:   store i32 %[[AND]], ptr %{{.*}}
// OGCG:   %[[XOR:.*]] = xor i32 %{{.*}}, %{{.*}}
// OGCG:   store i32 %[[XOR]], ptr %{{.*}}
// OGCG:   %[[OR:.*]] = or i32 %{{.*}}, %{{.*}}
// OGCG:   store i32 %[[OR]], ptr %{{.*}}

int atomic_volatile_add_assign(volatile _Atomic int &a, int x) {
  return a += x;
}

// CIR-LABEL: cir.func {{.*}} @_Z26atomic_volatile_add_assignRVU7_Atomicii
// CIR: %[[X:.*]] = cir.load {{.*}} : !cir.ptr<!s32i>, !s32i
// CIR: %[[A:.*]] = cir.load %{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CIR: cir.atomic.fetch add seq_cst syncscope(system) %[[A]], %[[X]] align(4) volatile : (!cir.ptr<!s32i>, !s32i) -> !s32i
// CIR: %[[RELOAD:.*]] = cir.load volatile align(4) atomic(seq_cst) %[[A]] : !cir.ptr<!s32i>, !s32i
// CIR: cir.store %[[RELOAD]], %{{.*}} : !s32i, !cir.ptr<!s32i>

// LLVM: define {{.*}}i32 @_Z26atomic_volatile_add_assignRVU7_Atomicii
// LLVM:   %[[A:.*]] = load ptr, ptr %{{.*}}
// LLVM:   atomicrmw volatile add ptr %[[A]], i32 %{{.*}} seq_cst, align 4
// LLVM:   %[[RELOAD:.*]] = load atomic volatile i32, ptr %[[A]] seq_cst, align 4
// LLVM:   store i32 %[[RELOAD]], ptr %{{.*}}

// OGCG: define {{.*}}i32 @_Z26atomic_volatile_add_assignRVU7_Atomicii
// OGCG:   %[[A:.*]] = load ptr, ptr %{{.*}}
// OGCG:   atomicrmw{{( volatile)?}} add ptr %[[A]], i32 %{{.*}} seq_cst, align 4
// OGCG:   %[[RELOAD:.*]] = load atomic volatile i32, ptr %[[A]] seq_cst, align 4
// OGCG:   ret i32 %[[RELOAD]]

_Atomic int &atomic_mul_assign_lvalue(_Atomic int &a, int x) { return a *= x; }

// CIR-LABEL: cir.func {{.*}} @_Z24atomic_mul_assign_lvalueRU7_Atomicii
// CIR: %[[A:.*]] = cir.load %{{.*}} : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CIR: cir.do {
// CIR:   cir.mul nsw
// CIR: } while {
// CIR:   cir.atomic.cmpxchg success(seq_cst) failure(seq_cst) syncscope(system) %[[A]], %{{.*}}, %{{.*}} align(4) : (!cir.ptr<!s32i>, !s32i, !s32i) -> (!s32i, !cir.bool)
// CIR: }
// CIR: cir.store %[[A]], %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>

// LLVM: define {{.*}}ptr @_Z24atomic_mul_assign_lvalueRU7_Atomicii
// LLVM:   %[[A:.*]] = load ptr, ptr %{{.*}}
// LLVM:   cmpxchg ptr %[[A]], i32 %{{.*}}, i32 %{{.*}} seq_cst seq_cst, align 4
// LLVM:   mul nsw i32
// LLVM:   store ptr %[[A]], ptr %{{.*}}

// OGCG: define {{.*}}ptr @_Z24atomic_mul_assign_lvalueRU7_Atomicii
// OGCG:   %[[A:.*]] = load ptr, ptr %{{.*}}
// OGCG:   mul nsw i32
// OGCG:   cmpxchg ptr %[[A]], i32 %{{.*}}, i32 %{{.*}} seq_cst seq_cst, align 4
// OGCG:   ret ptr %[[A]]
