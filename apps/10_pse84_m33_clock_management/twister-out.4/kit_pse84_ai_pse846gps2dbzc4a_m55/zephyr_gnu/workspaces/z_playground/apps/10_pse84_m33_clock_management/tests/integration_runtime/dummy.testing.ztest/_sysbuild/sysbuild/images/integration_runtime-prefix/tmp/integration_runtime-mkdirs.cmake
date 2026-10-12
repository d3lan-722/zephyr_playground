# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/workspaces/z_playground/apps/10_pse84_m33_clock_management/tests/integration_runtime"
  "/workspaces/z_playground/apps/10_pse84_m33_clock_management/twister-out/kit_pse84_ai_pse846gps2dbzc4a_m55/zephyr_gnu/workspaces/z_playground/apps/10_pse84_m33_clock_management/tests/integration_runtime/dummy.testing.ztest/integration_runtime"
  "/workspaces/z_playground/apps/10_pse84_m33_clock_management/twister-out/kit_pse84_ai_pse846gps2dbzc4a_m55/zephyr_gnu/workspaces/z_playground/apps/10_pse84_m33_clock_management/tests/integration_runtime/dummy.testing.ztest/_sysbuild/sysbuild/images/integration_runtime-prefix"
  "/workspaces/z_playground/apps/10_pse84_m33_clock_management/twister-out/kit_pse84_ai_pse846gps2dbzc4a_m55/zephyr_gnu/workspaces/z_playground/apps/10_pse84_m33_clock_management/tests/integration_runtime/dummy.testing.ztest/_sysbuild/sysbuild/images/integration_runtime-prefix/tmp"
  "/workspaces/z_playground/apps/10_pse84_m33_clock_management/twister-out/kit_pse84_ai_pse846gps2dbzc4a_m55/zephyr_gnu/workspaces/z_playground/apps/10_pse84_m33_clock_management/tests/integration_runtime/dummy.testing.ztest/_sysbuild/sysbuild/images/integration_runtime-prefix/src/integration_runtime-stamp"
  "/workspaces/z_playground/apps/10_pse84_m33_clock_management/twister-out/kit_pse84_ai_pse846gps2dbzc4a_m55/zephyr_gnu/workspaces/z_playground/apps/10_pse84_m33_clock_management/tests/integration_runtime/dummy.testing.ztest/_sysbuild/sysbuild/images/integration_runtime-prefix/src"
  "/workspaces/z_playground/apps/10_pse84_m33_clock_management/twister-out/kit_pse84_ai_pse846gps2dbzc4a_m55/zephyr_gnu/workspaces/z_playground/apps/10_pse84_m33_clock_management/tests/integration_runtime/dummy.testing.ztest/_sysbuild/sysbuild/images/integration_runtime-prefix/src/integration_runtime-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/workspaces/z_playground/apps/10_pse84_m33_clock_management/twister-out/kit_pse84_ai_pse846gps2dbzc4a_m55/zephyr_gnu/workspaces/z_playground/apps/10_pse84_m33_clock_management/tests/integration_runtime/dummy.testing.ztest/_sysbuild/sysbuild/images/integration_runtime-prefix/src/integration_runtime-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/workspaces/z_playground/apps/10_pse84_m33_clock_management/twister-out/kit_pse84_ai_pse846gps2dbzc4a_m55/zephyr_gnu/workspaces/z_playground/apps/10_pse84_m33_clock_management/tests/integration_runtime/dummy.testing.ztest/_sysbuild/sysbuild/images/integration_runtime-prefix/src/integration_runtime-stamp${cfgdir}") # cfgdir has leading slash
endif()
