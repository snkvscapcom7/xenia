/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_threading.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/modules/xboxkrnl/kernel_state.h>
#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_private.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xevent.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xthread.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {
namespace xboxkrnl {


// r13 + 0x100: pointer to thread local state
// Thread local state:
//   0x14C: thread id
//   0x150: if >0 then error states don't get set
//   0x160: last error

// GetCurrentThreadId:
// lwz       r11, 0x100(r13)
// lwz       r3, 0x14C(r11)

// RtlGetLastError:
// lwz r11, 0x150(r13)
// if (r11 != 0) {
//   lwz r11, 0x100(r13)
//   stw r3, 0x160(r11)
// }

// RtlSetLastError:
// lwz r11, 0x150(r13)
// if (r11 != 0) {
//   lwz r11, 0x100(r13)
//   stw r3, 0x160(r11)
// }

// RtlSetLastNTError:
// r3 = RtlNtStatusToDosError(r3)
// lwz r11, 0x150(r13)
// if (r11 != 0) {
//   lwz r11, 0x100(r13)
//   stw r3, 0x160(r11)
// }


X_STATUS xeExCreateThread(
    uint32_t* handle_ptr, uint32_t stack_size, uint32_t* thread_id_ptr,
    uint32_t xapi_thread_startup,
    uint32_t start_address, uint32_t start_context, uint32_t creation_flags) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // DWORD
  // LPHANDLE Handle,
  // DWORD    StackSize,
  // LPDWORD  ThreadId,
  // LPVOID   XapiThreadStartup, ?? often 0
  // LPVOID   StartAddress,
  // LPVOID   StartContext,
  // DWORD    CreationFlags // 0x80?

  XThread* thread = new XThread(
      state, stack_size, xapi_thread_startup, start_address, start_context,
      creation_flags);

  X_STATUS result_code = thread->Create();
  if (XFAILED(result_code)) {
    // Failed!
    thread->Release();
    XELOGE("Thread creation failed: %.8X", result_code);
    return result_code;
  }

  if (handle_ptr) {
    *handle_ptr = thread->handle();
  }
  if (thread_id_ptr) {
    *thread_id_ptr = thread->thread_id();
  }
  return result_code;
}


SHIM_CALL ExCreateThread_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t stack_size = SHIM_GET_ARG_32(1);
  uint32_t thread_id_ptr = SHIM_GET_ARG_32(2);
  uint32_t xapi_thread_startup = SHIM_GET_ARG_32(3);
  uint32_t start_address = SHIM_GET_ARG_32(4);
  uint32_t start_context = SHIM_GET_ARG_32(5);
  uint32_t creation_flags = SHIM_GET_ARG_32(6);

  XELOGD(
      "ExCreateThread(%.8X, %d, %.8X, %.8X, %.8X, %.8X, %.8X)",
      handle_ptr,
      stack_size,
      thread_id_ptr,
      xapi_thread_startup,
      start_address,
      start_context,
      creation_flags);

  uint32_t handle;
  uint32_t thread_id;
  X_STATUS result = xeExCreateThread(
      &handle, stack_size, &thread_id, xapi_thread_startup,
      start_address, start_context, creation_flags);

  if (XSUCCEEDED(result)) {
    if (handle_ptr) {
      SHIM_SET_MEM_32(handle_ptr, handle);
    }
    if (thread_id_ptr) {
      SHIM_SET_MEM_32(thread_id_ptr, thread_id);
    }
  }
  SHIM_SET_RETURN(result);
}


uint32_t xeKeGetCurrentProcessType() {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // DWORD

  XELOGD(
      "KeGetCurrentProcessType()");

  return X_PROCTYPE_USER;
}


SHIM_CALL KeGetCurrentProcessType_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  XELOGD(
      "KeGetCurrentProcessType()");

  int result = xeKeGetCurrentProcessType();
  SHIM_SET_RETURN(result);
}


// The TLS system used here is a bit hacky, but seems to work.
// Both Win32 and pthreads use unsigned longs as TLS indices, so we can map
// right into the system for these calls. We're just round tripping the IDs and
// hoping for the best.


// http://msdn.microsoft.com/en-us/library/ms686801
uint32_t xeKeTlsAlloc() {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // DWORD

  uint32_t tls_index;

#if XE_PLATFORM(WIN32)
  tls_index = TlsAlloc();
#else
  pthread_key_t key;
  if (pthread_key_create(&key, NULL)) {
    tls_index = X_TLS_OUT_OF_INDEXES;
  } else {
    tls_index = (uint32_t)key;
  }
#endif  // WIN32

  return tls_index;
}


SHIM_CALL KeTlsAlloc_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  XELOGD(
      "KeTlsAlloc()");

  uint32_t result = xeKeTlsAlloc();
  SHIM_SET_RETURN(result);
}


// http://msdn.microsoft.com/en-us/library/ms686804
int KeTlsFree(uint32_t tls_index) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // BOOL
  // _In_  DWORD dwTlsIndex

  if (tls_index == X_TLS_OUT_OF_INDEXES) {
    return 0;
  }

  int result_code = 0;

#if XE_PLATFORM(WIN32)
  result_code = TlsFree(tls_index);
#else
  result_code = pthread_key_delete(tls_index) == 0;
#endif  // WIN32

  return result_code;
}


SHIM_CALL KeTlsFree_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);

  XELOGD(
      "KeTlsFree(%.8X)",
      tls_index);

  int result = xeKeTlsAlloc();
  SHIM_SET_RETURN(result);
}


// http://msdn.microsoft.com/en-us/library/ms686812
uint32_t xeKeTlsGetValue(uint32_t tls_index) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // LPVOID
  // _In_  DWORD dwTlsIndex

  uint32_t value = 0;

#if XE_PLATFORM(WIN32)
  value = (uint32_t)((uint64_t)TlsGetValue(tls_index));
#else
  value = (uint32_t)((uint64_t)pthread_getspecific(tls_index));
#endif  // WIN32

  if (!value) {
    XELOGW("KeTlsGetValue should SetLastError if result is NULL");
    // TODO(benvanik): SetLastError
  }

  return value;
}


SHIM_CALL KeTlsGetValue_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);

  XELOGD(
      "KeTlsGetValue(%.8X)",
      tls_index);

  uint32_t result = xeKeTlsGetValue(tls_index);
  SHIM_SET_RETURN(result);
}


// http://msdn.microsoft.com/en-us/library/ms686818
int xeKeTlsSetValue(uint32_t tls_index, uint32_t tls_value) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // BOOL
  // _In_      DWORD dwTlsIndex,
  // _In_opt_  LPVOID lpTlsValue

  int result_code = 0;

#if XE_PLATFORM(WIN32)
  result_code = TlsSetValue(tls_index, (LPVOID)tls_value);
#else
  result_code = pthread_setspecific(tls_index, (void*)tls_value) == 0;
#endif  // WIN32

  return result_code;
}


SHIM_CALL KeTlsSetValue_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t tls_index = SHIM_GET_ARG_32(0);
  uint32_t tls_value = SHIM_GET_ARG_32(1);

  XELOGD(
      "KeTlsSetValue(%.8X, %.8X)",
      tls_index, tls_value);

  int result = xeKeTlsSetValue(tls_index, tls_value);
  SHIM_SET_RETURN(result);
}


int32_t xeKeSetEvent(void* event_ptr, uint32_t increment, uint32_t wait) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  XEvent* ev = (XEvent*)XObject::GetObject(state, event_ptr);
  XEASSERTNOTNULL(ev);
  if (!ev) {
    return 0;
  }

  return ev->Set(increment, !!wait);
}


SHIM_CALL KeSetEvent_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t event_ref = SHIM_GET_ARG_32(0);
  uint32_t increment = SHIM_GET_ARG_32(1);
  uint32_t wait = SHIM_GET_ARG_32(2);

  XELOGD(
      "KeSetEvent(%.4X, %.4X, %.4X)",
      event_ref, increment, wait);

  void* event_ptr = SHIM_MEM_ADDR(event_ref);
  int32_t result = xeKeSetEvent(event_ptr, increment, wait);

  SHIM_SET_RETURN(result);
}


int32_t xeKeResetEvent(void* event_ptr) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  XEvent* ev = (XEvent*)XEvent::GetObject(state, event_ptr);
  XEASSERTNOTNULL(ev);
  if (!ev) {
    return 0;
  }

  return ev->Reset();
}


SHIM_CALL KeResetEvent_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t event_ref = SHIM_GET_ARG_32(0);

  XELOGD(
      "KeResetEvent(%.4X)",
      event_ref);

  void* event_ptr = SHIM_MEM_ADDR(event_ref);
  int32_t result = xeKeResetEvent(event_ptr);

  SHIM_SET_RETURN(result);
}


X_STATUS xeKeWaitForSingleObject(
    void* object_ptr, uint32_t wait_reason, uint32_t processor_mode,
    uint32_t alertable, uint64_t* opt_timeout) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  XObject* object = XObject::GetObject(state, object_ptr);
  if (!object) {
    // The only kind-of failure code.
    return X_STATUS_ABANDONED_WAIT_0;
  }

  return object->Wait(wait_reason, processor_mode, alertable, opt_timeout);
}


SHIM_CALL KeWaitForSingleObject_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t object = SHIM_GET_ARG_32(0);
  uint32_t wait_reason = SHIM_GET_ARG_32(1);
  uint32_t processor_mode = SHIM_GET_ARG_32(2);
  uint32_t alertable = SHIM_GET_ARG_32(3);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(4);

  XELOGD(
      "KeWaitForSingleObject(%.8X, %.8X, %.8X, %.1X, %.8X)",
      object, wait_reason, processor_mode, alertable, timeout_ptr);

  void* object_ptr = SHIM_MEM_ADDR(object);
  uint64_t timeout = timeout_ptr ? SHIM_MEM_64(timeout_ptr) : 0;
  X_STATUS result = xeKeWaitForSingleObject(
      object_ptr, wait_reason, processor_mode, alertable,
      timeout_ptr ? &timeout : NULL);

  SHIM_SET_RETURN(result);
}


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterThreadingExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", ExCreateThread, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeGetCurrentProcessType, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsAlloc, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsFree, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsGetValue, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeTlsSetValue, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeSetEvent, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", KeResetEvent, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", KeWaitForSingleObject, state);
}
