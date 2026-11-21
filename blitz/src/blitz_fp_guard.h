// blitz - ReXGlue Recompiled Project
//
// Floating-point exception guard.
//
// Xenon runs guest code with FP exceptions masked. On the host, an unmasked SSE
// exception (inexact, overflow, divide-by-zero, ...) raised by recompiled code
// becomes a structured exception and kills the thread. Blitz also imports
// KeEnableFpuExceptions, so it may try to turn them on itself.
//
// This vectored handler catches any SSE FP exception, masks all of them in the
// faulting thread's MXCSR and resumes at the same instruction, which then
// produces the masked (IEEE default) result, matching the console.

#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <xmmintrin.h>

#include <atomic>
#include <cstdint>

namespace blitz {

inline std::atomic<uint64_t> g_fp_exception_count{0};

inline LONG WINAPI FpExceptionHandler(EXCEPTION_POINTERS* info) {
  switch (info->ExceptionRecord->ExceptionCode) {
    case STATUS_FLOAT_DENORMAL_OPERAND:
    case STATUS_FLOAT_DIVIDE_BY_ZERO:
    case STATUS_FLOAT_INEXACT_RESULT:
    case STATUS_FLOAT_INVALID_OPERATION:
    case STATUS_FLOAT_OVERFLOW:
    case STATUS_FLOAT_STACK_CHECK:
    case STATUS_FLOAT_UNDERFLOW:
    case STATUS_FLOAT_MULTIPLE_FAULTS:
    case STATUS_FLOAT_MULTIPLE_TRAPS:
      g_fp_exception_count.fetch_add(1, std::memory_order_relaxed);
      info->ContextRecord->MxCsr |= _MM_MASK_MASK;
      return EXCEPTION_CONTINUE_EXECUTION;
    default:
      return EXCEPTION_CONTINUE_SEARCH;
  }
}

inline void* InstallFpGuard() { return AddVectoredExceptionHandler(1, FpExceptionHandler); }

inline void RemoveFpGuard(void* handle) {
  if (handle) RemoveVectoredExceptionHandler(handle);
}

}  // namespace blitz

#else

namespace blitz {
inline void* InstallFpGuard() { return nullptr; }
inline void RemoveFpGuard(void*) {}
}  // namespace blitz

#endif
