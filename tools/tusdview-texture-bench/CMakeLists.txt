# Standalone headless GPU texture processing benchmark. It intentionally does
# not link or include tusdview's renderer.
# TIFF/TEX input is part of the benchmark's corpus path. The top-level project
# enables TINYUSDZ_WITH_TIFF by default; keep that option visible to callers so
# a deliberately disabled core decoder produces a clear configuration choice.
if(NOT TINYUSDZ_WITH_TIFF)
  message(WARNING "texture GPU benchmark: TIFF/TEX input disabled by TINYUSDZ_WITH_TIFF=OFF")
endif()
include(CTest)
if(BUILD_TESTING)
  enable_testing()
endif()
find_program(TUSDVIEW_TEXTURE_GLSLANG glslangValidator)
if(NOT TUSDVIEW_TEXTURE_GLSLANG)
  message(WARNING "texture GPU benchmark disabled: glslangValidator not found")
  return()
endif()

include(CheckLanguage)
check_language(HIP)
if(CMAKE_HIP_COMPILER)
  enable_language(HIP)
  set(TUSDVIEW_TEXTURE_HAVE_HIP TRUE)
  set(TUSDVIEW_TEXTURE_HIP_ARCHITECTURES "native" CACHE STRING
      "HIP architecture(s) for the texture benchmark")
else()
  message(STATUS "texture GPU benchmark: HIP compiler not found; HIP backend disabled")
endif()
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  set(TUSDVIEW_TEXTURE_HAVE_CUDA TRUE)
  set(TUSDVIEW_TEXTURE_CUDA_ARCHITECTURES "120" CACHE STRING
      "CUDA architecture(s) for the texture benchmark")
else()
  message(STATUS "texture GPU benchmark: CUDA compiler not found; CUDA backend disabled")
endif()

set(TEXBENCH_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${TEXBENCH_GEN})
add_custom_command(
  OUTPUT ${TEXBENCH_GEN}/resize_comp.spv.h ${TEXBENCH_GEN}/resize_hdr_comp.spv.h ${TEXBENCH_GEN}/compress_comp.spv.h ${TEXBENCH_GEN}/bc6h_comp.spv.h
  COMMAND ${TUSDVIEW_TEXTURE_GLSLANG} -V --target-env vulkan1.2
          --vn resize_comp_spv -o ${TEXBENCH_GEN}/resize_comp.spv.h
          ${CMAKE_CURRENT_SOURCE_DIR}/vk/resize.comp
  COMMAND ${TUSDVIEW_TEXTURE_GLSLANG} -V --target-env vulkan1.2
          --vn resize_hdr_comp_spv -o ${TEXBENCH_GEN}/resize_hdr_comp.spv.h
          ${CMAKE_CURRENT_SOURCE_DIR}/vk/resize_hdr.comp
  COMMAND ${TUSDVIEW_TEXTURE_GLSLANG} -V --target-env vulkan1.2
          --vn compress_comp_spv -o ${TEXBENCH_GEN}/compress_comp.spv.h
          ${CMAKE_CURRENT_SOURCE_DIR}/vk/compress.comp
  COMMAND ${TUSDVIEW_TEXTURE_GLSLANG} -V --target-env vulkan1.2
          --vn bc6h_comp_spv -o ${TEXBENCH_GEN}/bc6h_comp.spv.h
          ${CMAKE_CURRENT_SOURCE_DIR}/vk/bc6h.comp
  DEPENDS vk/resize.comp vk/resize_hdr.comp vk/compress.comp vk/bc6h.comp
  VERBATIM)
add_custom_target(tusdview_texture_gpu_shaders
  DEPENDS ${TEXBENCH_GEN}/resize_comp.spv.h ${TEXBENCH_GEN}/resize_hdr_comp.spv.h ${TEXBENCH_GEN}/compress_comp.spv.h ${TEXBENCH_GEN}/bc6h_comp.spv.h)

add_executable(tusdview_texture_gpu_bench
  main.cc texture_gpu.cc vulkan_processor.cc
  ${PROJECT_SOURCE_DIR}/examples/common/volk/volk.c
  )
if(TUSDVIEW_TEXTURE_HAVE_HIP)
  target_sources(tusdview_texture_gpu_bench PRIVATE hip_processor.hip)
  set_source_files_properties(hip_processor.hip PROPERTIES LANGUAGE HIP)
  set_target_properties(tusdview_texture_gpu_bench PROPERTIES
    HIP_ARCHITECTURES "${TUSDVIEW_TEXTURE_HIP_ARCHITECTURES}")
  target_compile_definitions(tusdview_texture_gpu_bench PRIVATE TUSDVIEW_TEXTURE_HAVE_HIP=1)
endif()
if(TUSDVIEW_TEXTURE_HAVE_CUDA)
  target_sources(tusdview_texture_gpu_bench PRIVATE cuda_processor.cu)
  set_target_properties(tusdview_texture_gpu_bench PROPERTIES
    CUDA_ARCHITECTURES "${TUSDVIEW_TEXTURE_CUDA_ARCHITECTURES}")
  target_compile_definitions(tusdview_texture_gpu_bench PRIVATE TUSDVIEW_TEXTURE_HAVE_CUDA=1)
  set_source_files_properties(cuda_processor.cu PROPERTIES LANGUAGE CUDA)
endif()
add_dependencies(tusdview_texture_gpu_bench tusdview_texture_gpu_shaders)
target_compile_features(tusdview_texture_gpu_bench PRIVATE cxx_std_17)
target_compile_definitions(tusdview_texture_gpu_bench PRIVATE VK_NO_PROTOTYPES)
target_include_directories(tusdview_texture_gpu_bench PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${TEXBENCH_GEN}
  ${PROJECT_SOURCE_DIR}/examples/common/volk
  ${PROJECT_SOURCE_DIR}/examples/common/vulkan-headers/include
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/external
  ${PROJECT_SOURCE_DIR}/src/external/textools/texcomp/include
  ${PROJECT_SOURCE_DIR}/src/external/textools/texcomp/src)
target_link_libraries(tusdview_texture_gpu_bench PRIVATE
  ${TINYUSDZ_TARGET_STATIC} tinyusdz_textools ${CMAKE_DL_LIBS})
if(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(tusdview_texture_gpu_bench PRIVATE Threads::Threads m)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(tusdview_texture_gpu_bench PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wno-unused-parameter;-Wno-misleading-indentation>
    $<$<COMPILE_LANGUAGE:CUDA>:-Wno-unused-result>
    $<$<COMPILE_LANGUAGE:HIP>:-Wno-unused-result;-Wno-unused-value>
    $<$<COMPILE_LANGUAGE:C>:-w>)
endif()

add_test(NAME tusdview-texture-gpu-synthetic
  COMMAND tusdview_texture_gpu_bench --backend vulkan --synthetic
          --iterations 2 --warmup 1)
set_tests_properties(tusdview-texture-gpu-synthetic PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu" TIMEOUT 120)

add_test(NAME tusdview-texture-gpu-mips
  COMMAND tusdview_texture_gpu_bench --backend vulkan --synthetic
          --format astc --mips 3 --iterations 1 --warmup 0)
set_tests_properties(tusdview-texture-gpu-mips PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;mips" TIMEOUT 120)

add_test(NAME tusdview-texture-gpu-bc6h-mips
  COMMAND tusdview_texture_gpu_bench --backend vulkan --synthetic
          --format bc6h --mips 3 --iterations 1 --warmup 0)
set_tests_properties(tusdview-texture-gpu-bc6h-mips PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;mips;hdr" TIMEOUT 120)

add_test(NAME tusdview-texture-vulkan-nvidia-mips
  COMMAND tusdview_texture_gpu_bench --backend vulkan --device NVIDIA
          --synthetic --format astc --mips 3 --iterations 1 --warmup 0)
set_tests_properties(tusdview-texture-vulkan-nvidia-mips PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;mips;nvidia" TIMEOUT 120)
add_test(NAME tusdview-texture-vulkan-nvidia-bc6h-mips
  COMMAND tusdview_texture_gpu_bench --backend vulkan --device NVIDIA
          --synthetic --format bc6h --mips 3 --iterations 1 --warmup 0)
set_tests_properties(tusdview-texture-vulkan-nvidia-bc6h-mips PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;mips;nvidia;hdr" TIMEOUT 120)

add_test(NAME tusdview-texture-vulkan-formats
  COMMAND tusdview_texture_gpu_bench --backend vulkan --synthetic
          --format bc1,bc3,bc5,bc6h,bc7,astc --mips 0
          --filter bilinear --linear --iterations 1 --warmup 0)
set_tests_properties(tusdview-texture-vulkan-formats PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;formats;hdr;mips" TIMEOUT 180)

add_test(NAME tusdview-texture-vulkan-nvidia-formats
  COMMAND tusdview_texture_gpu_bench --backend vulkan --device NVIDIA --synthetic
          --format bc1,bc3,bc5,bc6h,bc7,astc --mips 0
          --filter bilinear --linear --iterations 1 --warmup 0)
set_tests_properties(tusdview-texture-vulkan-nvidia-formats PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;formats;hdr;mips;nvidia" TIMEOUT 180)

if(TUSDVIEW_TEXTURE_HAVE_HIP)
  get_filename_component(_texture_hip_bin "${CMAKE_HIP_COMPILER}" DIRECTORY)
  get_filename_component(_texture_hip_root "${_texture_hip_bin}/../../.." REALPATH)
  set_tests_properties(tusdview-texture-gpu-synthetic tusdview-texture-gpu-mips tusdview-texture-gpu-bc6h-mips tusdview-texture-vulkan-formats PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${_texture_hip_root}/lib:$ENV{LD_LIBRARY_PATH}")
  add_test(NAME tusdview-texture-hip-mips
    COMMAND ${CMAKE_COMMAND} -E env
            "LD_LIBRARY_PATH=${_texture_hip_root}/lib:$ENV{LD_LIBRARY_PATH}"
            $<TARGET_FILE:tusdview_texture_gpu_bench> --backend hip --synthetic
            --format astc --mips 3 --iterations 1 --warmup 0)
  set_tests_properties(tusdview-texture-hip-mips PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;mips;hip" TIMEOUT 120)
  add_test(NAME tusdview-texture-hip-bc6h-mips
    COMMAND ${CMAKE_COMMAND} -E env
            "LD_LIBRARY_PATH=${_texture_hip_root}/lib:$ENV{LD_LIBRARY_PATH}"
            $<TARGET_FILE:tusdview_texture_gpu_bench> --backend hip --synthetic
            --format bc6h --mips 3 --iterations 1 --warmup 0)
  set_tests_properties(tusdview-texture-hip-bc6h-mips PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;mips;hip;hdr" TIMEOUT 120)
  add_test(NAME tusdview-texture-hip-formats
    COMMAND ${CMAKE_COMMAND} -E env
            "LD_LIBRARY_PATH=${_texture_hip_root}/lib:$ENV{LD_LIBRARY_PATH}"
            $<TARGET_FILE:tusdview_texture_gpu_bench> --backend hip --synthetic
            --format bc1,bc3,bc5,bc6h,bc7,astc --mips 0
            --filter bilinear --linear --iterations 1 --warmup 0)
  set_tests_properties(tusdview-texture-hip-formats PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;formats;hdr;mips;hip" TIMEOUT 180)
endif()

if(TUSDVIEW_TEXTURE_HAVE_CUDA)
  get_filename_component(_texture_cuda_bin "${CMAKE_CUDA_COMPILER}" DIRECTORY)
  get_filename_component(_texture_cuda_root "${_texture_cuda_bin}/.." REALPATH)
  add_test(NAME tusdview-texture-cuda-mips
    COMMAND tusdview_texture_gpu_bench --backend cuda --synthetic
            --format astc --mips 3 --iterations 1 --warmup 0)
  set_tests_properties(tusdview-texture-cuda-mips PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;mips;cuda" TIMEOUT 120)
  add_test(NAME tusdview-texture-cuda-bc6h-mips
    COMMAND tusdview_texture_gpu_bench --backend cuda --synthetic
            --format bc6h --mips 3 --iterations 1 --warmup 0)
  set_tests_properties(tusdview-texture-cuda-bc6h-mips PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;mips;cuda;hdr" TIMEOUT 120)
  add_test(NAME tusdview-texture-cuda-formats
    COMMAND ${CMAKE_COMMAND} -E env
            "LD_LIBRARY_PATH=${_texture_cuda_root}/lib64:${_texture_hip_root}/lib:$ENV{LD_LIBRARY_PATH}"
            $<TARGET_FILE:tusdview_texture_gpu_bench> --backend cuda --synthetic
            --format bc1,bc3,bc5,bc6h,bc7,astc --mips 0
            --filter bilinear --linear --iterations 1 --warmup 0)
  set_tests_properties(tusdview-texture-cuda-formats PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;formats;hdr;mips;cuda" TIMEOUT 180)
  set_tests_properties(tusdview-texture-cuda-mips tusdview-texture-cuda-bc6h-mips
    tusdview-texture-cuda-formats tusdview-texture-vulkan-nvidia-mips
    tusdview-texture-vulkan-nvidia-bc6h-mips tusdview-texture-vulkan-nvidia-formats PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${_texture_cuda_root}/lib64:${_texture_hip_root}/lib:$ENV{LD_LIBRARY_PATH}")
endif()
