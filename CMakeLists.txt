cmake_minimum_required(VERSION 3.22)

# =============================================================================
#  door_rtos —— 按分层组织，头文件与源文件分离
#
#    include/doort/            公开 API 与配置（doort.h / doort_config*.h）
#    source/Inc/<layer>/*.hpp  内核私有头文件
#    source/Src/<layer>/*.cpp  内核实现
#    source/Src/doort.cpp      公共 C API 中转层（一行转发到各原语）
#
#    分层：
#      port    移植层：SysTick、任务栈帧伪造、PendSV 触发
#      kernel  内核：调度器、TCB、延时阻塞表、中断/上下文切换、故障
#      memory  句柄堆、增量紧缩器、紧缩任务
#      sync    信号量、互斥量、事件组、邮箱、异步信号槽
#      util    位图工具
#
#  两种用法：
#    1) 独立构建：cmake -S . -B build && cmake --build build
#    2) 作为子目录：add_subdirectory(door_rtos) 然后 target_link_libraries(app PRIVATE doort)
#
#  注意：库内按内核对象粒度拆分，静态链接时只会拉取被引用到的 .o。
#  中断/上下文切换（PendSV/SVC/SysTick/HardFault…）由启动向量表引用，
#  用户代码调用 dTaskCreate/dStartScheduler 即会把内核拉进来，无需 whole-archive。
# =============================================================================

# 允许被上层工程 add_subdirectory() 时不要重复定义 project()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(doort LANGUAGES C CXX)
    set(DOORT_STANDALONE ON)
else()
    set(DOORT_STANDALONE OFF)
endif()

add_library(doort STATIC
    # 公共 C API
    source/Src/doort.cpp

    # 移植层
    source/Src/port/doort_port.cpp

    # 内核
    source/Src/kernel/doort_global_state.cpp
    source/Src/kernel/doort_kernel_scheduler.cpp
    source/Src/kernel/doort_task.cpp
    source/Src/kernel/doort_task_blocked.cpp
    source/Src/kernel/doort_Idle_task.cpp
    source/Src/kernel/doort_Interrupt.cpp
    source/Src/kernel/doort_fault.cpp

    # 内存
    source/Src/memory/doort_memory_manage.cpp
    source/Src/memory/doort_memory_move_task.cpp

    # 同步原语与信号槽
    source/Src/sync/doort_sync_semaphore.cpp
    source/Src/sync/doort_sync_mutex.cpp
    source/Src/sync/doort_sync_event.cpp
    source/Src/sync/doort_sync_post.cpp
    source/Src/sync/doort_async_connect.cpp

    # 工具
    source/Src/util/doort_bitmap_tool.cpp
)

# 公开头文件路径（只需要 include/，source/Inc 下的是内核私有实现）
target_include_directories(doort PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 内核内部互相引用用的私有头文件路径
target_include_directories(doort PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/doort
    ${CMAKE_CURRENT_SOURCE_DIR}/source/Inc/port
    ${CMAKE_CURRENT_SOURCE_DIR}/source/Inc/kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/source/Inc/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/source/Inc/sync
    ${CMAKE_CURRENT_SOURCE_DIR}/source/Inc/util
)

target_compile_features(doort PUBLIC cxx_std_17)

# 该内核为 freestanding 风格，不使用异常/RTTI；如需与带异常的宿主混编可关掉此选项
option(DOORT_NO_EXCEPTIONS "Compile doort with -fno-exceptions -fno-rtti" OFF)
if(DOORT_NO_EXCEPTIONS)
    target_compile_options(doort PRIVATE -fno-exceptions -fno-rtti)
endif()

if(DOORT_STANDALONE)
    message(STATUS "door_rtos: standalone build, only the static library is produced")
endif()
