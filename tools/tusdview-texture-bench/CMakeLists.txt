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
  ${PROJECT_SOURCE_DIR}/src/external/hipew/hipew.c
  ${PROJECT_SOURCE_DIR}/examples/common/volk/volk.c
  )
if(TUSDVIEW_TEXTURE_HAVE_HIP)
  if(NOT TARGET tusdview_texture_hip_plugin)
    add_library(tusdview_texture_hip_plugin MODULE hip_processor.hip)
    target_include_directories(tusdview_texture_hip_plugin PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tusdview_texture_hip_plugin PROPERTIES
      HIP_ARCHITECTURES "${TUSDVIEW_TEXTURE_HIP_ARCHITECTURES}"
      PREFIX "")
  endif()
  add_dependencies(tusdview_texture_gpu_bench tusdview_texture_hip_plugin)
  target_compile_definitions(tusdview_texture_gpu_bench PRIVATE
    TUSDVIEW_TEXTURE_HAVE_HIP=1 TUSDVIEW_TEXTURE_HIP_DYNAMIC=1
    TUSDVIEW_TEXTURE_HIP_PLUGIN_PATH="$<TARGET_FILE:tusdview_texture_hip_plugin>")
endif()
if(TUSDVIEW_TEXTURE_HAVE_CUDA)
  target_sources(tusdview_texture_gpu_bench PRIVATE cuda_processor.cu)
  set_target_properties(tusdview_texture_gpu_bench PROPERTIES
    CUDA_ARCHITECTURES "${TUSDVIEW_TEXTURE_CUDA_ARCHITECTURES}")
  target_compile_definitions(tusdview_texture_gpu_bench PRIVATE TUSDVIEW_TEXTURE_HAVE_CUDA=1)
  set_source_files_properties(cuda_processor.cu PROPERTIES LANGUAGE CUDA)
endif()

# HDR/EXR real-data checks. Set TUSDVIEW_TEXTURE_GPU_ROOT to an EXR (or use
# TUSDVIEW_TEXTURE_GPU_HDR_ROOT for a separate HDR asset) to enable these.
set(_texture_hdr_test_env
    "TUSDVIEW_TEXTURE_BENCH=$<TARGET_FILE:tusdview_texture_gpu_bench>"
    "TUSDVIEW_TEXTURE_GPU_FORMATS=bc6h,bc7,astc"
    "TUSDVIEW_TEXTURE_GPU_MIPS=3"
    "TUSDVIEW_TEXTURE_GPU_MAX_DIMENSION=2048"
    "TUSDVIEW_TEXTURE_GPU_MIN_PSNR=5")
add_test(NAME tusdview-texture-vulkan-nvidia-hdr-real
  COMMAND ${CMAKE_COMMAND} -E env
          ${_texture_hdr_test_env}
          "TUSDVIEW_TEXTURE_GPU_BACKEND=vulkan"
          "TUSDVIEW_TEXTURE_GPU_DEVICE=NVIDIA"
          bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
set_tests_properties(tusdview-texture-vulkan-nvidia-hdr-real PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;hdr;nvidia" TIMEOUT 900)

add_test(NAME tusdview-texture-vulkan-amd-hdr-real
  COMMAND ${CMAKE_COMMAND} -E env
          ${_texture_hdr_test_env}
          "TUSDVIEW_TEXTURE_GPU_BACKEND=vulkan"
          "TUSDVIEW_TEXTURE_GPU_DEVICE=AMD"
          bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
set_tests_properties(tusdview-texture-vulkan-amd-hdr-real PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;hdr;amd" TIMEOUT 900)

set(_texture_bc6h_full_env
    "TUSDVIEW_TEXTURE_BENCH=$<TARGET_FILE:tusdview_texture_gpu_bench>"
    "TUSDVIEW_TEXTURE_GPU_FORMATS=bc6h"
    "TUSDVIEW_TEXTURE_GPU_MIPS=3"
    "TUSDVIEW_TEXTURE_GPU_MIN_PSNR=5"
    "TUSDVIEW_TEXTURE_GPU_MAX_GPU_MS=60000")
add_test(NAME tusdview-texture-vulkan-nvidia-bc6h-full-real
  COMMAND ${CMAKE_COMMAND} -E env
          ${_texture_bc6h_full_env}
          "TUSDVIEW_TEXTURE_GPU_BACKEND=vulkan"
          "TUSDVIEW_TEXTURE_GPU_DEVICE=NVIDIA"
          bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
set_tests_properties(tusdview-texture-vulkan-nvidia-bc6h-full-real PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;hdr;full;nvidia" TIMEOUT 1800)

add_test(NAME tusdview-texture-vulkan-amd-bc6h-full-real
  COMMAND ${CMAKE_COMMAND} -E env
          ${_texture_bc6h_full_env}
          "TUSDVIEW_TEXTURE_GPU_BACKEND=vulkan"
          "TUSDVIEW_TEXTURE_GPU_DEVICE=AMD"
          bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
set_tests_properties(tusdview-texture-vulkan-amd-bc6h-full-real PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;hdr;full;amd" TIMEOUT 1800)
if(TUSDVIEW_TEXTURE_HAVE_HIP)
  add_test(NAME tusdview-texture-hip-hdr-real
    COMMAND ${CMAKE_COMMAND} -E env
            ${_texture_hdr_test_env}
            "TUSDVIEW_TEXTURE_GPU_MAX_DIMENSION=2048"
            "TUSDVIEW_TEXTURE_GPU_BACKEND=hip"
            bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
  set_tests_properties(tusdview-texture-hip-hdr-real PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;hdr;hip" TIMEOUT 900)
endif()

if(TUSDVIEW_TEXTURE_HAVE_CUDA)
  add_test(NAME tusdview-texture-cuda-hdr-real
    COMMAND ${CMAKE_COMMAND} -E env
            ${_texture_hdr_test_env}
            "TUSDVIEW_TEXTURE_GPU_MAX_DIMENSION=2048"
            "TUSDVIEW_TEXTURE_GPU_BACKEND=cuda"
            bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
  set_tests_properties(tusdview-texture-cuda-hdr-real PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;hdr;cuda" TIMEOUT 900)
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
  ${PROJECT_SOURCE_DIR}/src/external/hipew
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

# Opt-in real-corpus checks. These remain skipped unless the caller supplies
# TUSDVIEW_TEXTURE_GPU_ROOT, so ordinary builds never depend on private assets.
add_test(NAME tusdview-texture-vulkan-nvidia-real
  COMMAND ${CMAKE_COMMAND} -E env
          "TUSDVIEW_TEXTURE_BENCH=$<TARGET_FILE:tusdview_texture_gpu_bench>"
          "TUSDVIEW_TEXTURE_GPU_BACKEND=vulkan"
          "TUSDVIEW_TEXTURE_GPU_DEVICE=NVIDIA"
          "TUSDVIEW_TEXTURE_GPU_PREFER_TEXTURES=1"
          bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
set_tests_properties(tusdview-texture-vulkan-nvidia-real PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;nvidia" TIMEOUT 900)

add_test(NAME tusdview-texture-vulkan-nvidia-formats-real
  COMMAND ${CMAKE_COMMAND} -E env
          "TUSDVIEW_TEXTURE_BENCH=$<TARGET_FILE:tusdview_texture_gpu_bench>"
          "TUSDVIEW_TEXTURE_GPU_BACKEND=vulkan"
          "TUSDVIEW_TEXTURE_GPU_DEVICE=NVIDIA"
          "TUSDVIEW_TEXTURE_GPU_FORMATS=bc1,bc3,bc5,bc7,astc"
          "TUSDVIEW_TEXTURE_GPU_MIPS=3"
          "TUSDVIEW_TEXTURE_GPU_MAX_IMAGES=1"
          "TUSDVIEW_TEXTURE_GPU_PREFER_TEXTURES=1"
          "TUSDVIEW_TEXTURE_GPU_MIN_PSNR=1"
          bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
set_tests_properties(tusdview-texture-vulkan-nvidia-formats-real PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;formats;nvidia" TIMEOUT 900)

add_test(NAME tusdview-texture-vulkan-amd-real
  COMMAND ${CMAKE_COMMAND} -E env
          "TUSDVIEW_TEXTURE_BENCH=$<TARGET_FILE:tusdview_texture_gpu_bench>"
          "TUSDVIEW_TEXTURE_GPU_BACKEND=vulkan"
          "TUSDVIEW_TEXTURE_GPU_DEVICE=AMD"
          "TUSDVIEW_TEXTURE_GPU_PREFER_TEXTURES=1"
          bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
set_tests_properties(tusdview-texture-vulkan-amd-real PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;amd" TIMEOUT 900)

add_test(NAME tusdview-texture-vulkan-amd-formats-real
  COMMAND ${CMAKE_COMMAND} -E env
          "TUSDVIEW_TEXTURE_BENCH=$<TARGET_FILE:tusdview_texture_gpu_bench>"
          "TUSDVIEW_TEXTURE_GPU_BACKEND=vulkan"
          "TUSDVIEW_TEXTURE_GPU_DEVICE=AMD"
          "TUSDVIEW_TEXTURE_GPU_FORMATS=bc1,bc3,bc5,bc7,astc"
          "TUSDVIEW_TEXTURE_GPU_MIPS=3"
          "TUSDVIEW_TEXTURE_GPU_MAX_IMAGES=1"
          "TUSDVIEW_TEXTURE_GPU_PREFER_TEXTURES=1"
          "TUSDVIEW_TEXTURE_GPU_MIN_PSNR=1"
          bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
set_tests_properties(tusdview-texture-vulkan-amd-formats-real PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;formats;amd" TIMEOUT 900)

add_test(NAME tusdview-texture-tiff-metadata-real
  COMMAND ${CMAKE_COMMAND} -E env
          "TUSDVIEW_TEXTURE_BENCH=$<TARGET_FILE:tusdview_texture_gpu_bench>"
          "TUSDVIEW_TEXTURE_GPU_METADATA_ONLY=1"
          "TUSDVIEW_TEXTURE_GPU_MAX_IMAGES=8"
          "TUSDVIEW_TEXTURE_GPU_PREFER_TEXTURES=1"
          bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
set_tests_properties(tusdview-texture-tiff-metadata-real PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;tiff;metadata" TIMEOUT 300)

add_test(NAME tusdview-texture-tiff-decode-real
  COMMAND ${CMAKE_COMMAND} -E env
          "TUSDVIEW_TEXTURE_BENCH=$<TARGET_FILE:tusdview_texture_gpu_bench>"
          "TUSDVIEW_TEXTURE_GPU_BACKEND=vulkan"
          "TUSDVIEW_TEXTURE_GPU_FORMATS=bc7"
          "TUSDVIEW_TEXTURE_GPU_MIPS=3"
          "TUSDVIEW_TEXTURE_GPU_MAX_IMAGES=1"
          "TUSDVIEW_TEXTURE_GPU_MAX_GPU_MS=5000"
          bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
set_tests_properties(tusdview-texture-tiff-decode-real PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;tiff;decode" TIMEOUT 900)

add_test(NAME tusdview-texture-vulkan-validation
  COMMAND ${CMAKE_COMMAND} -E env
          "TUSDVIEW_TEXTURE_BENCH=$<TARGET_FILE:tusdview_texture_gpu_bench>"
          bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-vulkan-validation.sh")
set_tests_properties(tusdview-texture-vulkan-validation PROPERTIES
  SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;vulkan;validation" TIMEOUT 180)

if(TUSDVIEW_TEXTURE_HAVE_HIP)
  add_test(NAME tusdview-texture-hip-real
    COMMAND ${CMAKE_COMMAND} -E env
            "TUSDVIEW_TEXTURE_BENCH=$<TARGET_FILE:tusdview_texture_gpu_bench>"
            "TUSDVIEW_TEXTURE_GPU_BACKEND=hip"
            "TUSDVIEW_TEXTURE_GPU_PREFER_TEXTURES=1"
            bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
  set_tests_properties(tusdview-texture-hip-real PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;hip" TIMEOUT 900)
endif()

if(TUSDVIEW_TEXTURE_HAVE_CUDA)
  add_test(NAME tusdview-texture-cuda-real
    COMMAND ${CMAKE_COMMAND} -E env
            "TUSDVIEW_TEXTURE_BENCH=$<TARGET_FILE:tusdview_texture_gpu_bench>"
            "TUSDVIEW_TEXTURE_GPU_BACKEND=cuda"
            "TUSDVIEW_TEXTURE_GPU_PREFER_TEXTURES=1"
            bash "${PROJECT_SOURCE_DIR}/examples/tusdview/tests/run-texture-gpu-real.sh")
  set_tests_properties(tusdview-texture-cuda-real PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "tusdview;gpu;real;cuda" TIMEOUT 900)
endif()

if(TUSDVIEW_TEXTURE_HAVE_HIP)
  get_filename_component(_texture_hip_bin "${CMAKE_HIP_COMPILER}" DIRECTORY)
  get_filename_component(_texture_hip_root "${_texture_hip_bin}/../../.." REALPATH)
  set_tests_properties(tusdview-texture-gpu-synthetic tusdview-texture-gpu-mips tusdview-texture-gpu-bc6h-mips tusdview-texture-vulkan-formats
    tusdview-texture-vulkan-nvidia-real tusdview-texture-vulkan-nvidia-formats-real
    tusdview-texture-vulkan-amd-real
    tusdview-texture-vulkan-amd-formats-real
    tusdview-texture-tiff-metadata-real tusdview-texture-vulkan-nvidia-hdr-real
    tusdview-texture-vulkan-amd-hdr-real tusdview-texture-tiff-decode-real
    tusdview-texture-vulkan-validation tusdview-texture-vulkan-nvidia-bc6h-full-real
    tusdview-texture-vulkan-amd-bc6h-full-real PROPERTIES
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
    tusdview-texture-vulkan-nvidia-bc6h-mips tusdview-texture-vulkan-nvidia-formats
    tusdview-texture-vulkan-nvidia-real tusdview-texture-vulkan-nvidia-formats-real
    tusdview-texture-vulkan-amd-real
    tusdview-texture-vulkan-amd-formats-real
    tusdview-texture-tiff-metadata-real tusdview-texture-vulkan-nvidia-hdr-real
    tusdview-texture-vulkan-amd-hdr-real tusdview-texture-hip-real
    tusdview-texture-hip-hdr-real tusdview-texture-cuda-real
    tusdview-texture-cuda-hdr-real tusdview-texture-tiff-decode-real
    tusdview-texture-vulkan-validation tusdview-texture-vulkan-nvidia-bc6h-full-real
    tusdview-texture-vulkan-amd-bc6h-full-real PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${_texture_cuda_root}/lib64:${_texture_hip_root}/lib:$ENV{LD_LIBRARY_PATH}")
endif()
