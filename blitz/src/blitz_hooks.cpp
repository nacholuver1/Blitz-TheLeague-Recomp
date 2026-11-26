// blitz - ReXGlue Recompiled Project

#include "generated/default/blitz_funcs.h"

#include <rex/ppc/hook.h>

#include <bit>
#include <cmath>

#include "blitz_settings.h"

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

// ---------------------------------------------------------------------------
// Ultrawide (blitz.aspect_ratio).
//
// The game cameras (sub_82249970, sub_8224B038, ...) fill a view command
// (vtable 0x820239E8) with a horizontal size at +156 and a vertical size at
// +160 = horizontal * 0x828007B0, the height / width ratio the 720p video mode
// sets to 9/16. They hand it to sub_82256FF8, which copies it into the render
// queue. Widening only the horizontal size there gives Hor+: the same vertical
// view, more to the sides (the picture keeps 1280x720 pixels and is presented
// at the chosen shape). Changing the ratio global instead keeps the width and
// crops the height (Vert-), and the 2D menus and HUD read it too.
// Other users of this command send 1 / 1 or sizes not tied to the ratio; they
// are left alone. The 2D overlay keeps its 16:9 layout and is stretched.
// ---------------------------------------------------------------------------
namespace {
constexpr u32 kViewCommandVtable = 0x820239E8;
constexpr u32 kViewHeightOverWidth = 0x828007B0;
}  // namespace

REX_HOOK_RAW(sub_82256FF8) {
  const u32 command = ctx.r4.u32;
  const double aspect = blitz::AspectRatio();
  if (command && aspect > 16.0 / 9.0 + 0.01 && REX_LOAD_U32(command) == kViewCommandVtable) {
    const f32 ratio = std::bit_cast<f32>(REX_LOAD_U32(kViewHeightOverWidth));
    const f32 horizontal = std::bit_cast<f32>(REX_LOAD_U32(command + 156));
    const f32 vertical = std::bit_cast<f32>(REX_LOAD_U32(command + 160));
    // A camera view: the vertical size was derived from the horizontal one.
    if (ratio > 0.0f && horizontal > 0.0f &&
        std::fabs(vertical - horizontal * ratio) <= 1e-4f * horizontal) {
      const f32 widen = static_cast<f32>(aspect * ratio);  // e.g. (64/27) * (9/16) = 4/3
      REX_STORE_U32(command + 156, std::bit_cast<u32>(horizontal * widen));
    }
  }
  __imp__sub_82256FF8(ctx, base);
}
