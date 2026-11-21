// blitz - ReXGlue Recompiled Project

#include "generated/default/blitz_funcs.h"

#include <rex/ppc/hook.h>

// ---------------------------------------------------------------------------
// Recursive-lock wrapper race fix (boot hang in the resource loader).
//
// The game wraps every RTL_CRITICAL_SECTION in a small recursive lock:
//
//   struct GameLock {            // r3
//     RTL_CRITICAL_SECTION cs;   // +0x00 (0x1C bytes)
//     u32 owner_tid;             // +0x1C  -1 when free
//     u32 count;                 // +0x20  never decremented by the game
//   };
//
//   lock   sub_8262C228: RtlEnter(cs); if (owner==me) count++; else {owner=me; count=0;}
//   unlock sub_8262C290: if (owner==me) { RtlLeave(cs); if (count==0) owner=-1; }
//
// The unlock writes owner=-1 AFTER releasing the critical section. If another
// thread acquires it inside that window and sets owner to itself, the -1 store
// erases it; that thread's unlock then sees owner!=me, skips RtlLeave, and holds
// the critical section forever (its own recursive enters still succeed, so it
// keeps running while every other thread blocks). On a real 360 the window is a
// few instructions on a woken-but-not-yet-running thread; on PC the release
// does a host SetEvent and the waiter runs immediately on another core, so the
// resource loader deadlocks a second into boot.
//
// Fix: same logic, but clear the owner before releasing the critical section.
// ---------------------------------------------------------------------------
REX_HOOK_RAW(sub_8262C290) {
  REX_FUNC_PROLOGUE();
  u32 ea{};
  // Frame setup kept identical to the original so the guest stack is unchanged.
  ctx.r12.u64 = ctx.lr;
  REX_STACK_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
  REX_STACK_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  ea = -96 + ctx.r1.u32;
  REX_STACK_STORE_U32(ea, ctx.r1.u32);
  ctx.r1.u32 = ea;
  ctx.r31.u64 = ctx.r3.u64;

  // GetCurrentThreadId()
  ctx.lr = 0x8262C2A8;
  sub_826C6400(ctx, base);

  const u32 lock = ctx.r31.u32;
  if (ctx.r3.u32 == REX_LOAD_U32(lock + 28)) {
    // Moved up: reset the owner while we still hold the critical section.
    if (static_cast<i32>(REX_LOAD_U32(lock + 32)) == 0) {
      REX_STORE_U32(lock + 28, 0xFFFFFFFFu);
    }
    ctx.r3.u64 = lock;
    ctx.lr = 0x8262C2BC;
    __imp__RtlLeaveCriticalSection(ctx, base);
  }

  ctx.r1.s64 = ctx.r1.s64 + 96;
  ctx.r12.u64 = REX_STACK_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
  ctx.r31.u64 = REX_STACK_LOAD_U64(ctx.r1.u32 + -16);
}

// ---------------------------------------------------------------------------
// Offline create-a-team saving.
//
// sub_82429490 is XAPI's XUserCheckPrivilege(user, privilege, &result). The
// campaign create-a-team save (0x826653A8) requires privilege 247
// (XPRIVILEGE_USER_CREATED_CONTENT); a local, non-Live profile never has it, so
// the game says the team can't be saved without Xbox Live. Grant only that
// privilege. Communications (252) and multiplayer (254) still go through the
// original, so the game doesn't start attempting online features.
// ---------------------------------------------------------------------------
namespace {
constexpr u32 kPrivilegeUserCreatedContent = 247;
}

REX_DETOUR(sub_82429490, u32, (u32 user_index, u32 privilege, be_u32* result)) {
  if (privilege == kPrivilegeUserCreatedContent && result) {
    *result = 1;
    return 0;  // ERROR_SUCCESS
  }
  return original(user_index, privilege, result);
}
