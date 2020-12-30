; RUN: llvm-as < %s -o %t.bc
; RUN: llvm-spirv %t.bc -s -o - | llvm-dis -o - | FileCheck %s --check-prefix=CHECK-LLVM
; RUN: llvm-spirv %t.bc -o %t.spv
; RUN: spirv-val %t.spv
; RUN: llvm-spirv -to-text %t.spv -o - | FileCheck %s

; CHECK-LLVM: %2 = addrspacecast [2 x i64] addrspace(1)* @data to [2 x i64] addrspace(4)*
; CHECK-LLVM: %3 = getelementptr inbounds [2 x i64], [2 x i64] addrspace(4)* %2, i64 0, i32 0
; CHECK-LLVM-NOT:  addrspacecast [2 x i64] addrspace(1)* @data to [2 x i64] addrspace(4)*
; CHECK-LLVM: %4 = getelementptr inbounds [2 x i64], [2 x i64] addrspace(4)* %2, i64 1, i32 0
; CHECK-LLVM: %5 = select i1 %1, i64 addrspace(4)* %3, i64 addrspace(4)* %4

; CHECK: InBoundsPtrAccessChain
; PtrCastToGeneric 19 20 9
; InBoundsPtrAccessChain 21 25 20 24 15
; Select 21 26 17 22 25


; ModuleID = 'spec-const-duplicate.bc'
source_filename = "/work/tmp/spec-const-duplicate.cpp"
target datalayout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-n8:16:32:64"
target triple = "spir64-unknown-linux-sycldevice"

@data = internal unnamed_addr addrspace(1) constant [2 x i64] [i64 11, i64 22], align 8

define spir_func i64 @foo(i32 %a) #0 {
  %1 = icmp eq i32 %a, 0
  %2 = select i1 %1, i64 addrspace(4)* getelementptr inbounds ([2 x i64], [2 x i64] addrspace(4)* addrspacecast ([2 x i64] addrspace(1)* @data to [2 x i64] addrspace(4)*), i64 0, i32 0), i64 addrspace(4)* getelementptr inbounds ([2 x i64], [2 x i64] addrspace(4)* addrspacecast ([2 x i64] addrspace(1)* @data to [2 x i64] addrspace(4)*), i64 1, i32 0)
  %3 = load i64, i64 addrspace(4)* %2, align 8, !tbaa !6
  ret i64 %3
}

attributes #0 = { convergent nounwind }

!llvm.module.flags = !{!0}
!opencl.spir.version = !{!1}
!spirv.Source = !{!2}
!opencl.used.extensions = !{!3}
!opencl.used.optional.core.features = !{!4}
!opencl.compiler.options = !{!3}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, i32 2}
!2 = !{i32 4, i32 100000}
!3 = !{}
!4 = !{!"cl_doubles"}
!6 = !{!7, !7, i64 0}
!7 = !{!"double", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C++ TBAA"}
