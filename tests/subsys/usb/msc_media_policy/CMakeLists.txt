# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(msc_media_policy)

target_sources(app PRIVATE
  ${ZEPHYR_BASE}/subsys/usb/device/class/msc_media_policy.c
  src/main.c
)
target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/subsys/usb/device/class
)
target_compile_definitions(app PRIVATE MSC_MEDIA_POLICY_TEST_HOOK)
