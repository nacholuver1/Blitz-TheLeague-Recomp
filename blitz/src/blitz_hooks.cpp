// blitz - ReXGlue Recompiled Project

#include "generated/default/blitz_funcs.h"

#include <rex/ppc/hook.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <time.h>
#endif
#include <immintrin.h>

#include "blitz_settings.h"

#include <rex/logging/log_category.h>

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

// ---------------------------------------------------------------------------
// Smart wait (blitz.smart_wait, blitz.wait_spin_us, blitz.wait_sleep_us).
//
// The D3D library's GPU waits spin, which is free on the console (its db16cyc
// hint hands the core to the other hardware thread) but keeps a whole host
// core busy: at the profile menu one thread sat at 99% of a core, in the
// ring-space wait sub_82449BC0 (2026-11-27). Every D3D wait loop calls
// sub_82457430 on each pass: 8 db16cyc (nothing on the host), then the hang
// check (5000-tick timeout); it returns 1 to keep waiting. After spinning
// wait_spin_us, each further pass sleeps wait_sleep_us on a high-resolution
// timer. Ported from Blitz: The League II 1.2.2 (sub_824AF628 there). Idea
// from the Pirates of the Caribbean: At World's End and Guitar Hero: Warriors
// of Rock ReXGlue recomps, written fresh.
// ---------------------------------------------------------------------------
namespace {
using WaitClock = std::chrono::steady_clock;

// Polls closer together than this belong to the same wait (sleeps are at
// most 1 ms, the setting's maximum).
constexpr auto kSameWaitGap = std::chrono::microseconds(1500);

struct WaitState {
  WaitClock::time_point start{};
  WaitClock::time_point last{};
};
thread_local WaitState t_wait;

#if defined(_WIN32)
// This thread's high-resolution timer, closed when the thread exits.
struct WaitTimer {
  HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS);
  ~WaitTimer() {
    if (handle) CloseHandle(handle);
  }
};
#endif

void SleepMicroseconds(i32 us) {
#if defined(_WIN32)
  thread_local WaitTimer wait_timer;
  const HANDLE timer = wait_timer.handle;
  if (timer) {
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(us) * 10;  // relative, 100 ns units
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
      WaitForSingleObject(timer, INFINITE);
      return;
    }
  }
  Sleep(1);  // no high-resolution timer (Windows before 10 1803)
#else
  timespec ts{0, static_cast<long>(us) * 1000};
  while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
  }  // a signal cut it short: sleep the rest
#endif
}

// Called on each pass of a wait that is not over yet.
void BackOff() {
  const auto now = WaitClock::now();
  if (now - t_wait.last > kSameWaitGap) t_wait.start = now;
  const auto& settings = blitz::Settings();
  if (now - t_wait.start < std::chrono::microseconds(settings.wait_spin_us.Get())) {
    _mm_pause();
  } else {
    SleepMicroseconds(settings.wait_sleep_us.Get());
  }
  t_wait.last = WaitClock::now();
}
}  // namespace

REX_HOOK_RAW(sub_82457430) {
  __imp__sub_82457430(ctx, base);
  if (ctx.r3.u32 == 1 && blitz::Settings().smart_wait.Get()) {
    BackOff();
  }
}

// ---------------------------------------------------------------------------
// Audio barrier (sound stopping for good). sub_82469588 is a rendezvous in the
// game's audio library: each audio thread sets its byte (index = its processor
// number, PCR byte 268) in a shared 64-bit word, then spins until the word is
// either all expected bytes (it then clears it) or zero (another thread cleared
// it). On the Xbox each thread owns a hardware thread and is never paused. On a
// PC, a thread paused after the word filled up can miss both states: the
// clearing thread clears, comes round again and sets its byte for the next
// round, and the paused thread then sees neither "full" nor "zero" while the
// other waits for its byte - both spin forever (stacks 2026-11-27: two audio
// threads in this loop, starve snapshot "a guest audio callback is blocked").
// The host version keeps the barrier on the host: a mutex-guarded mask of the
// threads that arrived and a round counter. The last arrival clears the mask,
// ends the round and clears the game's word; the others wait for the round to
// change, so a paused thread always gets out of the round it was in. The game's
// word still gets each thread's byte and the final zero.
// ---------------------------------------------------------------------------
namespace {
struct AudioBarrier {
  std::mutex mutex;
  u64 arrived = 0;              // bytes in REX_LOAD_U64 order, guarded by mutex
  std::atomic<u32> round{0};
};

AudioBarrier& FindAudioBarrier(u32 word) {
  static std::mutex mutex;
  static std::unordered_map<u32, AudioBarrier> barriers;  // nodes never move
  std::lock_guard lock(mutex);
  return barriers[word];
}
}  // namespace

REX_HOOK_RAW(sub_82469588) {
  if (!blitz::Settings().audio_barrier_fix.Get()) {
    __imp__sub_82469588(ctx, base);
    return;
  }
  const u32 object = ctx.r3.u32;
  const u32 word = ctx.r4.u32;
  if (REX_LOAD_U32(object + 304) == 0) {
    return;
  }
  // One byte per hardware thread (byte k = slot at object + 308 + 4k); byte 0
  // is the top byte of the value REX_LOAD_U64 returns.
  u64 expected = 0;
  for (u32 k = 0; k < 6; ++k) {
    if (REX_LOAD_U32(object + 308 + 4 * k) != 0) {
      expected |= u64{1} << (56 - 8 * k);
    }
  }
  const u8 processor = REX_LOAD_U8(ctx.r13.u32 + 268);
  if (processor >= 8) {
    // Outside the 8-byte word: never seen; leave it to the game's own code.
    __imp__sub_82469588(ctx, base);
    return;
  }
  AudioBarrier& barrier = FindAudioBarrier(word);
  u32 my_round;
  {
    std::lock_guard lock(barrier.mutex);
    REX_STORE_U8(word + processor, 1);
    barrier.arrived |= u64{1} << (56 - 8 * processor);
    if ((barrier.arrived & expected) == expected) {
      barrier.arrived = 0;
      REX_STORE_U64(word, 0);
      barrier.round.fetch_add(1, std::memory_order_release);
      return;
    }
    my_round = barrier.round.load(std::memory_order_relaxed);
  }
  for (u32 spins = 0; barrier.round.load(std::memory_order_acquire) == my_round; ++spins) {
    if (spins < 2000) {
      _mm_pause();
    } else {
      std::this_thread::yield();
    }
  }
}

// ---------------------------------------------------------------------------
// Frame rate: 60 (the game's own) or 30 mode (blitz.frame_mode).
//
// The game's speed is one fixed game step per main-loop pass, and each pass
// also runs the frame task sub_822DF528, which renders and presents (the
// present waits for the 60 Hz vblank). Below 60 fps the game runs in slow
// motion. The frame task has the console's own frame skip, but a graphics
// flag ([[[0x82873160]+8]+12], 1 from boot) bypasses it, and even forced on it
// only reaches 3 game steps per 2 presents (its 2-frame window counts the
// skipped, nearly free frame; logs 2026-11-27).
//
// 30 mode: the frame task renders on every other pass and skips the render on
// the passes between, so each drawn frame carries two game steps. A skipped
// pass waits until two guest vblanks have passed since the last one, so a
// fast GPU still runs 60 steps a second and not 120. Loading screens (game
// mode 17/18), the frame task's second early check and scenes the game itself
// locks to 30 fps keep the game's own path, which renders every pass, as the
// game does there. The cutscenes (players running on, cheerleaders, coaches,
// celebrations) present with a swap interval of 2: the graphics library's swap
// callback gets the interval with each swap, and 30 mode reads the last one,
// or those scenes would get a second game step and run at double speed.
//
// blitz.frame_diag (off, not saved): once a second, log frame-task passes,
// renders skipped, game steps (sub_822E1530), presents (sub_82462A78), the
// game mode and the early-check state.
// ---------------------------------------------------------------------------
namespace {
constexpr u32 kGameMode = 0x828007C4;

struct VblankClock {
  std::atomic<u64> count{0};
  std::mutex mutex;
  std::condition_variable cv;
};
VblankClock g_vblank;

struct FramePacer {  // main thread only
  bool active = false;      // in 30-mode pairs (false: 60 mode or the game's own path)
  bool render_next = true;
  u64 mark = 0;             // vblank count the next skipped pass waits two vblanks from
};
FramePacer g_pacer;

// Set while 30 mode draws a frame that carries two game steps (see the frame
// timer hook below).
bool g_pair_render = false;

// The scene update ran during this pass's game step (set by its hook, read and
// cleared by the frame task; same thread, the step runs first). In the game's
// loading modes 17/18, 30 mode leaves the frame task to the game until the
// scene update has run two passes in a row (the kickoff run-on starts while the
// mode still says loading), then keeps 30 mode until the mode changes.
bool g_scene_this_pass = false;
int g_scene_passes = 0;
bool g_loading_scene = false;

// The game's last present interval in vblanks (1 = 60 fps, 2 = locked 30).
std::atomic<u32> g_present_interval{0};  // 0 until the first swap

// A skipped pass: wait until two vblanks have passed since the last one.
void WaitForVblankCount(u64 target) {
  if (g_vblank.count.load() < target) {
    std::unique_lock lock(g_vblank.mutex);
    // Bounded: vblanks always run, but never hang the game if they stop.
    g_vblank.cv.wait_for(lock, std::chrono::milliseconds(50),
                         [&] { return g_vblank.count.load() >= target; });
  }
}

void WaitSecondVblank() {
  WaitForVblankCount(g_pacer.mark + 2);
  // A late pass starts a new grid here; missed vblanks are not made up.
  g_pacer.mark = g_vblank.count.load();
}

bool EarlyCheckB(u8* base) {
  if (u32 p = REX_LOAD_U32(0x82873184)) {
    if (u32 q = REX_LOAD_U32(p + 192)) return REX_LOAD_U8(q + 330) != 0;
  }
  return false;
}

bool EarlyCheckA(u8* base) {
  if (u32 a = REX_LOAD_U32(0x82873160)) {
    if (u32 b = REX_LOAD_U32(a + 8)) return REX_LOAD_U32(b + 12) != 0;
  }
  return false;
}

struct FrameDiag {
  std::atomic<u32> frames{0}, early_a{0}, early_b{0}, locked30{0}, skipped{0}, renders{0}, steps{0},
      presents{0}, scene_updates{0}, halved{0};
  std::chrono::steady_clock::time_point last{};
  bool started = false;
};
FrameDiag g_frame_diag;

void FrameDiagTick(u8* base) {
  auto& d = g_frame_diag;
  const auto now = std::chrono::steady_clock::now();
  if (!d.started) {
    d.started = true;
    d.last = now;
    return;
  }
  if (now - d.last < std::chrono::seconds(1)) return;
  const double secs = std::chrono::duration<double>(now - d.last).count();
  REXLOG_INFO(
      "frame diag: {:.2f}s mode{} frames {} skipped {} renders {} steps {} presents {} | early a {} "
      "b {} | game mode {} vblanks {} | interval {} locked30 passes {} | scene updates {} halved {}",
      secs, blitz::FrameMode30() ? 30 : 60, d.frames.exchange(0), d.skipped.exchange(0),
      d.renders.exchange(0), d.steps.exchange(0), d.presents.exchange(0), d.early_a.exchange(0),
      d.early_b.exchange(0), u32{REX_LOAD_U32(kGameMode)}, g_vblank.count.load(),
      g_present_interval.load(), d.locked30.exchange(0), d.scene_updates.exchange(0),
      d.halved.exchange(0));
  d.last = now;
}
}  // namespace

// ---------------------------------------------------------------------------
// Scene time step in 30 mode. The game's frame timer (sub_823AA520, timer object
// [0x82873150]) runs once per drawn frame from the render path: it stores the
// seconds since its previous call as a double at +200, plus the frame rate and
// a 64-frame frame-rate ring. The in-engine scenes (players running on, huddles,
// cheerleaders, coaches, celebrations) read that time step on every game step
// (sub_8233FDD0, inside the step task). In 30 mode a drawn frame carries two game
// steps, so both read the whole frame's time and the scenes ran at double speed
// (eye tests and an in-game memory scan, 2026-11-28). When 30 mode draws such a
// frame, the time step is halved after the timer runs, so the two steps together
// add up to real time. It stays halved until the next drawn frame's timer call
// overwrites it; the frame rate and the ring (read by the render side) are left
// as measured.
// ---------------------------------------------------------------------------
constexpr u32 kFrameTimerPtr = 0x82873150;

REX_HOOK_RAW(sub_823AA520) {
  const u32 timer = ctx.r3.u32;
  __imp__sub_823AA520(ctx, base);
  if (g_pair_render && timer != 0 && timer == REX_LOAD_U32(kFrameTimerPtr)) {
    const f64 step = std::bit_cast<f64>(u64{REX_LOAD_U64(timer + 200)});
    REX_STORE_U64(timer + 200, std::bit_cast<u64>(step * 0.5));
    if (blitz::Settings().frame_diag.Get()) g_frame_diag.halved++;
  }
}

// The scene update that reads the frame time step.
REX_HOOK_RAW(sub_8233FDD0) {
  g_scene_this_pass = true;
  if (blitz::Settings().frame_diag.Get()) g_frame_diag.scene_updates++;
  __imp__sub_8233FDD0(ctx, base);
}

// The graphics library's swap callback: the present interval of each swap is in
// the argument's top 16 bits.
REX_HOOK_RAW(sub_82462860) {
  g_present_interval.store(ctx.r3.u32 >> 16, std::memory_order_relaxed);
  __imp__sub_82462860(ctx, base);
}

// The game's graphics interrupt callback (registered with
// VdSetGraphicsInterruptCallback): r3 = source, 0 = vblank.
REX_HOOK_RAW(sub_82448EE8) {
  if (ctx.r3.u32 == 0) {
    {
      std::lock_guard lock(g_vblank.mutex);
      g_vblank.count.fetch_add(1);
    }
    g_vblank.cv.notify_all();
  }
  __imp__sub_82448EE8(ctx, base);
}

REX_HOOK_RAW(sub_822DF528) {
  const bool mode30 = blitz::FrameMode30();
  const bool diag = blitz::Settings().frame_diag.Get();
  if (!mode30 && !diag) {
    g_pacer.active = false;
    g_scene_this_pass = false;
    g_scene_passes = 0;
    g_loading_scene = false;
    __imp__sub_822DF528(ctx, base);
    return;
  }
  auto& d = g_frame_diag;
  const u32 renders_before = d.renders.load();
  if (diag) {
    if (EarlyCheckA(base)) d.early_a++;
    if (EarlyCheckB(base)) d.early_b++;
  }
  const u32 mode = REX_LOAD_U32(kGameMode);
  const bool scene_ran = g_scene_this_pass;
  g_scene_this_pass = false;
  if (mode == 17 || mode == 18) {
    g_scene_passes = scene_ran ? std::min(g_scene_passes + 1, 2) : 0;
    if (g_scene_passes >= 2) g_loading_scene = true;
  } else {
    g_scene_passes = 0;
    g_loading_scene = false;
  }
  const bool loading = (mode == 17 || mode == 18) && !g_loading_scene;
  const bool game_locked30 = g_present_interval.load(std::memory_order_relaxed) >= 2;
  if (diag && game_locked30) d.locked30++;
  if (!mode30) {
    g_pacer.active = false;
    __imp__sub_822DF528(ctx, base);
  } else if (loading || game_locked30 || EarlyCheckB(base)) {
    // The game's own path (renders every pass here); pairs start again after.
    __imp__sub_822DF528(ctx, base);
    g_pacer.active = false;
  } else if (!g_pacer.active || g_pacer.render_next) {
    // Entering 30 mode (start, a switch from 60, after a loading screen): anchor the
    // pacing at this render, so the first skipped pass also waits two vblanks.
    if (!g_pacer.active) {
      g_pacer.active = true;
      g_pacer.mark = g_vblank.count.load();
    }
    g_pair_render = true;
    sub_822DF2F0(ctx, base);  // the frame task's render call
    g_pair_render = false;
    g_pacer.render_next = false;
  } else {
    WaitSecondVblank();
    g_pacer.render_next = true;
  }
  if (diag) {
    d.frames++;
    if (d.renders.load() == renders_before) d.skipped++;
    FrameDiagTick(base);
  }
}

REX_HOOK_RAW(sub_822DF2F0) {
  if (blitz::Settings().frame_diag.Get()) g_frame_diag.renders++;
  __imp__sub_822DF2F0(ctx, base);
}

REX_HOOK_RAW(sub_822E1530) {
  if (blitz::Settings().frame_diag.Get()) g_frame_diag.steps++;
  __imp__sub_822E1530(ctx, base);
}

REX_HOOK_RAW(sub_82462A78) {
  if (blitz::Settings().frame_diag.Get()) g_frame_diag.presents++;
  __imp__sub_82462A78(ctx, base);
}
