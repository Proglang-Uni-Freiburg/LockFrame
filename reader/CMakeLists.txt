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
  ../debug/pwrdetector_optimized_4.cpp
  ../debug/pwr_paper.cpp
  ../debug/pwr_shared_ptr.cpp
  ../debug/pwr_no_syncs.cpp
  ../debug/pwr_remove_after_sync.cpp
  ../debug/pwr_remove_sync_equal.cpp
  ../debug/pwr_dont_add_reads.cpp
)

target_link_libraries(
  reader
)

