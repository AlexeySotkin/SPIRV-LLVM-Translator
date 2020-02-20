// RUN: %clang_cc1 -triple spir64 -cl-std=cl2.0 -O0 -finclude-default-header %s -emit-llvm-bc -o %t.bc
// RUN: llvm-spirv %t.bc -o %t.spv
// RUN: spirv-val %t.spv
// RUN: llvm-spirv %t.spv -to-text -o - | FileCheck %s

// CHECK: Capability Kernel
// CHECK: Capability Pipes
// CHECK: Capability Groups
// CHECK: Capability DeviceEnqueue

// CHECK: Decorate {{[0-9]+}} BuiltIn 36
// CHECK: Decorate {{[0-9]+}} BuiltIn 37
// CHECK: Decorate {{[0-9]+}} BuiltIn 38
// CHECK: Decorate {{[0-9]+}} BuiltIn 39
// CHECK: Decorate {{[0-9]+}} BuiltIn 40
// CHECK: Decorate {{[0-9]+}} BuiltIn 41
// CHECK: ControlBarrier
// CHECK: ControlBarrier
// CHECK: GroupAll 
// CHECK: GroupAny 
// CHECK: GroupBroadcast
// CHECK: GroupIAdd
// CHECK: GroupSMin
// CHECK: GroupSMax
// CHECK: GroupFAdd
// CHECK: GroupUMin
// CHECK: GroupSMax
// CHECK: GroupIAdd
// CHECK: GroupFMin
// CHECK: GroupUMax
// CHECK: GroupReserveReadPipePackets
// CHECK: GroupReserveWritePipePackets
// CHECK: GroupCommitReadPipe
// CHECK: GroupCommitWritePipe

// CHECK: GetKernelNDrangeSubGroupCount
// CHECK: GetKernelNDrangeMaxSubGroupSize

#pragma OPENCL EXTENSION cl_khr_subgroups : enable

void test_cl_khr_subgroups(read_only pipe int in_pipe, write_only pipe int out_pipe) {
  get_sub_group_size();
  get_max_sub_group_size();
  get_num_sub_groups();
  get_enqueued_num_sub_groups();
  get_sub_group_id();
  get_sub_group_local_id();

  sub_group_barrier(CLK_LOCAL_MEM_FENCE);
  sub_group_barrier(CLK_LOCAL_MEM_FENCE, memory_scope_sub_group);

  sub_group_all(1);
  sub_group_any(1);
  sub_group_broadcast(1, 2);
  sub_group_reduce_add(1);
  sub_group_reduce_min(1);
  sub_group_reduce_max(1);
  sub_group_scan_exclusive_add(1.0f);
  sub_group_scan_exclusive_min(1u);
  sub_group_scan_exclusive_max(1);
  sub_group_scan_inclusive_add(1);
  sub_group_scan_inclusive_min(1.0f);
  sub_group_scan_inclusive_max(1u);

  reserve_id_t res_id = sub_group_reserve_read_pipe(in_pipe, 2);
  res_id = sub_group_reserve_write_pipe(out_pipe, 2);
  sub_group_commit_read_pipe(in_pipe, res_id);
  sub_group_commit_write_pipe(out_pipe, res_id);

  ndrange_t ndrange;
  get_kernel_sub_group_count_for_ndrange(ndrange, ^(){});
  get_kernel_max_sub_group_size_for_ndrange(ndrange, ^(){});
}
