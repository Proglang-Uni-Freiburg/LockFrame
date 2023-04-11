cmake_minimum_required(VERSION 3.14)
project(reader)

# GoogleTest requires at least C++11
set(CMAKE_CXX_STANDARD 17)

add_executable(
  reader
  reader.cpp
  ../lockframe.cpp
  ../vectorclock.cpp
  ../pwrdetector.cpp
  ../undead.cpp
  ../pwrundeaddetector.cpp
  ../pwrundeadguarddetector.cpp
  ../debug/pwrdetector_optimized_4.cpp
  ../debug/pwr_paper.cpp
  ../debug/pwr_shared_ptr.cpp
  ../debug/pwr_no_syncs.cpp
  ../debug/pwr_remove_after_sync.cpp
  ../debug/pwr_remove_sync_equal.cpp
  ../debug/pwr_dont_add_reads.cpp
  ../debug/pwr_no_hist_optimized.cpp
  ../debug/pwr_for_undead.cpp
)

target_link_libraries(
  reader
)

if(DEFINED ${PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT})
    add_compile_definitions(PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT=${PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT})
else()
    add_compile_definitions(PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT=5)
endif()

if(DEFINED COLLECT_STATISTICS)
    add_compile_definitions(COLLECT_STATISTICS=COLLECT_STATISTICS)
endif()
