// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Base is trivial but not standard-layout, so its base subobject is a
// distinct packed CIR record: five bytes, which is not a multiple of the
// int member's ABI alignment. alignas(8) gives that subobject language
// alignment 8. The packed field places it at offset 4, which is not a
// multiple of 8, so the x86-64 ABI passes Holder in memory.

struct alignas(8) Base {
  int i;
private:
  char c;
};

struct Holder {
  char prefix[4];
  [[no_unique_address]] Base b __attribute__((packed));
};

void take(Holder h) {}

// CIR-DAG: !rec_Base2Ebase = !cir.struct<"Base.base" packed {data !s32i, data !s8i}>
// CIR-DAG: !rec_Holder = !cir.struct<"Holder" {data !cir.array<!s8i x 4>, data !rec_Base2Ebase, pad !cir.array<!u8i x 3>}>
// CIR-DAG: Base.base = #cir.record_layout<arg_passing_kind = can_pass_in_regs, has_trivial_dtor = true, record_align = 8, unadjusted_align = 4>
// CIR-DAG: NT.base = #cir.record_layout<arg_passing_kind = cannot_pass_in_regs
// CIR-DAG: , U = #cir.record_layout<arg_passing_kind = can_pass_in_regs
// CIR-DAG: U.base = #cir.record_layout<arg_passing_kind = cannot_pass_in_regs

// CIR-LABEL: cir.func {{.*}} @_Z4take6Holder(
// CIR-SAME:    !cir.ptr<!rec_Holder> {llvm.align = 8 : i64, llvm.byval = !rec_Holder, llvm.noundef}

// LLVM-LABEL: define dso_local void @_Z4take6Holder(ptr noundef byval(%struct.Holder) align 8 %{{.*}})

struct NT {
  NT() noexcept {}
  NT(const NT &) {}
  NT(NT &&) = default;
  alignas(2) bool b = false;
};

struct NUAHolder {
  NUAHolder() {}
  NUAHolder(const NUAHolder &) = delete;
  NUAHolder(NUAHolder &&) = default;
  [[no_unique_address]] NT t;
  [[no_unique_address]] bool flag = true;
};
static_assert(sizeof(NUAHolder) == 2);

union U {
  U() : t() {}
  U(U &&) = default;
  [[no_unique_address]] NT t;
  [[no_unique_address]] bool c;
};
struct UHolder {
  [[no_unique_address]] U u;
  [[no_unique_address]] bool flag;
};
static_assert(sizeof(UHolder) == 2);

NUAHolder makeHolder() { return NUAHolder(); }
int takeHolder(NUAHolder h) { return h.flag; }
UHolder makeUHolder() { return UHolder(); }

// CIR-DAG: cir.func {{.*}} @_Z10makeHolderv(%{{.+}}: !cir.ptr<!rec_NUAHolder> {{{.*}}llvm.sret = !rec_NUAHolder
// CIR-DAG: cir.func {{.*}} @_Z10takeHolder9NUAHolder(%{{.+}}: !cir.ptr<!rec_NUAHolder> {{{.*}}llvm.byval = !rec_NUAHolder
// CIR-DAG: cir.func {{.*}} @_Z11makeUHolderv(%{{.+}}: !cir.ptr<!rec_UHolder> {{{.*}}llvm.sret = !rec_UHolder

// LLVM: define {{.*}} void @_Z10makeHolderv(ptr {{.*}}sret(%struct.NUAHolder) align 2 %{{.+}})
// LLVM: define {{.*}} i32 @_Z10takeHolder9NUAHolder(ptr {{.*}}byval(%struct.NUAHolder) align 8 %{{.+}})
// LLVM: define {{.*}} void @_Z11makeUHolderv(ptr {{.*}}sret(%struct.UHolder) align 2 %{{.+}})

struct NTBase {
  NTBase() {}
  NTBase(const NTBase &) = delete;
  NTBase(NTBase &&) = default;
  [[no_unique_address]] NT nt;
};
struct Derived : NTBase {
  char c;
};
static_assert(sizeof(Derived) == 2);

int takeDerived(Derived d) { return d.c; }
Derived passDerived(Derived d) { return static_cast<Derived &&>(d); }

// LLVM: define {{.*}} i32 @_Z11takeDerived7Derived(ptr {{.*}}byval(%struct.Derived) align 8 %{{.+}})
// LLVM: define {{.*}} void @_Z11passDerived7Derived(ptr {{.*}}sret(%struct.Derived) align 2 %{{.+}}, ptr {{.*}}byval(%struct.Derived) align 8 %{{.+}})

struct ZeroArray {
  ZeroArray() {}
  ZeroArray(const ZeroArray &) = delete;
  ZeroArray(ZeroArray &&) = default;
  NT absent[0];
  alignas(2) bool tag = false;
};
struct ZeroWrapper {
  [[no_unique_address]] ZeroArray z;
  [[no_unique_address]] bool flag;
};
static_assert(sizeof(ZeroWrapper) == 2);

int takeZeroWrapper(ZeroWrapper w) { return w.flag; }
ZeroWrapper makeZeroWrapper();
ZeroWrapper relayZeroWrapper() { return makeZeroWrapper(); }

// CIR-DAG: cir.func {{.*}} @_Z15takeZeroWrapper11ZeroWrapper(%{{.+}}: !u16i
// CIR-DAG: cir.func {{.*}} @_Z16relayZeroWrapperv() -> !u16i

// LLVM: define {{.*}} i32 @_Z15takeZeroWrapper11ZeroWrapper(i16 %{{.+}})
// LLVM: define {{.*}} i16 @_Z16relayZeroWrapperv()
