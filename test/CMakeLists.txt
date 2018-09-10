configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg
  )

list(APPEND LLVM_SPIRV_TEST_DEPS
  FileCheck count not
  llvm-as
  llvm-dis
  llvm-spirv
)

add_lit_testsuite(check-llvm-spirv "Running the LLVM-SPIRV regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  PARAMS llvm_site_config=${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg
  DEPENDS ${LLVM_SPIRV_TEST_DEPS}
  )
