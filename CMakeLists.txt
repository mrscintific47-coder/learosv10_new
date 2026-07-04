cmake_minimum_required(VERSION 3.16)
project(LearnOS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Charts)

add_executable(LearnOS
    src/main.cpp
    src/MainWindow.cpp
    src/HeatMap.cpp
    src/HeatMapDriver.cpp
    src/ProcessViewer.cpp
    src/CpuMemMonitor.cpp
    src/SandboxManager.cpp
    src/AlgorithmStepper.cpp
    src/MemoryInspector.cpp
    src/MemoryLab.cpp
    src/MemoryLabDriver.cpp
    src/DataStructureLab.cpp
    src/DSLabDriver.cpp
    src/IPCLab.cpp
    src/SignalPanel.cpp
    src/EventBus.cpp
    src/ActivityFeed.cpp
    src/ExperimentManager.cpp
    src/ExperimentLab.cpp
    src/Explainer.cpp
    src/ThreadLab.cpp
    src/NamespaceLab.cpp
    src/EbpfLab.cpp
    src/FilesystemLab.cpp
)

# pthread for ThreadLab (pthread_create, pthread_mutex_t, etc.)
find_package(Threads REQUIRED)

target_link_libraries(LearnOS
    Qt6::Core
    Qt6::Widgets
    Qt6::Charts
    Threads::Threads
)

# Optional: libsystemd for journal feed (gracefully skipped if not present)
find_library(SYSTEMD_LIB systemd)
if(SYSTEMD_LIB)
    target_link_libraries(LearnOS ${SYSTEMD_LIB})
    target_compile_definitions(LearnOS PRIVATE LEARNOS_HAVE_SYSTEMD=1)
    message(STATUS "libsystemd found — journal feed enabled")
else()
    message(STATUS "libsystemd not found — journal feed disabled (install libsystemd-dev to enable)")
endif()

# -lrt for POSIX mqueue (mq_open) and shm_open/shm_unlink
find_library(RT_LIB rt)
if(RT_LIB)
    target_link_libraries(LearnOS ${RT_LIB})
    message(STATUS "librt found — POSIX mqueue and shm_open enabled")
else()
    message(STATUS "librt not found — POSIX mqueue/shm_open may not link")
endif()

# Worker binaries — built alongside the main binary and placed next to it
add_executable(memlab_worker tools/memlab_worker.cpp)
set_target_properties(memlab_worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:LearnOS>)

add_executable(dslab_worker tools/dslab_worker.cpp)
set_target_properties(dslab_worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:LearnOS>)

add_executable(learnos_thread_worker tools/thread_worker.cpp)
target_link_libraries(learnos_thread_worker Threads::Threads)
set_target_properties(learnos_thread_worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:LearnOS>)

add_executable(sched_worker tools/sched_worker.cpp)
target_link_libraries(sched_worker Threads::Threads)
set_target_properties(sched_worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:LearnOS>)

# Install targets
install(TARGETS LearnOS memlab_worker dslab_worker learnos_thread_worker sched_worker
    RUNTIME DESTINATION bin)
