/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_XOBJECT_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_XOBJECT_H_

#include <xenia/kernel/modules/xboxkrnl/kernel_state.h>

#include <xenia/kernel/xbox.h>


namespace xe {
namespace kernel {
  class Runtime;
}
}


namespace xe {
namespace kernel {
namespace xboxkrnl {


// http://www.nirsoft.net/kernel_struct/vista/DISPATCHER_HEADER.html
typedef struct {
  uint32_t type_flags;
  uint32_t signal_state;
  uint32_t wait_list_flink;
  uint32_t wait_list_blink;
} DISPATCH_HEADER;


class XObject {
public:
  enum Type {
    kTypeModule   = 0x00000001,
    kTypeThread   = 0x00000002,
    kTypeEvent    = 0x00000003,
  };

  XObject(KernelState* kernel_state, Type type);
  virtual ~XObject();

  KernelState* kernel_state();

  Type type();
  X_HANDLE handle() const;

  void RetainHandle();
  bool ReleaseHandle();
  void Retain();
  void Release();

  // Reference()
  // Dereference()

  virtual X_STATUS Wait(uint32_t wait_reason, uint32_t processor_mode,
                        uint32_t alertable, uint64_t* opt_timeout);

  static void LockType();
  static void UnlockType();
  static XObject* GetObject(KernelState* kernel_state, void* native_ptr);

protected:
  Runtime* runtime();
  xe_memory_ref memory(); // unretained

private:
  KernelState*  kernel_state_;

  volatile int32_t handle_ref_count_;
  volatile int32_t pointer_ref_count_;

  Type          type_;
  X_HANDLE      handle_;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_XOBJECT_H_
