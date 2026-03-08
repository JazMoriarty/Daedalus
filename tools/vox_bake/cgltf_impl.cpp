// cgltf_impl.cpp
// Single-header implementation for cgltf.  Must be compiled in exactly one
// translation unit of DaedalusVoxBake.  Not compiled for tests (they only
// test vox_writer.h and struct layouts, not cgltf I/O).

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
