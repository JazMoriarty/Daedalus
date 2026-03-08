// tools/vox_bake/main.cpp
// DaedalusVoxBake — offline glTF → MagicaVoxel .vox converter.
//
// Usage:
//   DaedalusVoxBake input.glb output_dir [--resolution N]
//
// Arguments:
//   input.glb     glTF 2.0 mesh to voxelise (binary .glb or text .gltf)
//   output_dir    directory to write <input_stem>.vox into (created if absent)
//   --resolution N  voxel resolution (default: 64; max: 255)
//
// Output:
//   output_dir/<input_stem>.vox   MagicaVoxel v150 binary file
//
// Exit codes:
//   0  success
//   1  bad arguments
//   2  voxelisation error
//   3  output write error

#include "surface_voxelizer.h"
#include "vox_writer.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace daedalus::tools;

static void print_usage()
{
    std::fprintf(stderr,
        "Usage: DaedalusVoxBake input.glb output_dir [--resolution N]\n"
        "\n"
        "  input.glb       glTF 2.0 mesh (.glb or .gltf)\n"
        "  output_dir      directory for output .vox file\n"
        "  --resolution N  voxel grid size on each axis (default 64, max 255)\n");
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        print_usage();
        return 1;
    }

    const std::string inputPath  = argv[1];
    const std::string outputDir  = argv[2];
    uint32_t          resolution = 64u;

    for (int i = 3; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--resolution" && i + 1 < argc)
        {
            resolution = static_cast<uint32_t>(std::atoi(argv[++i]));
            if (resolution < 1 || resolution > 255)
            {
                std::fprintf(stderr, "Error: --resolution must be in [1, 255]\n");
                return 1;
            }
        }
        else
        {
            std::fprintf(stderr, "Warning: unknown argument '%s' (ignored)\n", argv[i]);
        }
    }

    // Derive output path: output_dir/<stem>.vox
    const fs::path stem      = fs::path(inputPath).stem();
    const fs::path outPath   = fs::path(outputDir) / (stem.string() + ".vox");

    // Create output directory if needed
    std::error_code ec;
    fs::create_directories(outputDir, ec);
    if (ec)
    {
        std::fprintf(stderr, "Error: cannot create output directory '%s': %s\n",
                     outputDir.c_str(), ec.message().c_str());
        return 3;
    }

    std::printf("[VoxBake] input   : %s\n", inputPath.c_str());
    std::printf("[VoxBake] output  : %s\n", outPath.string().c_str());
    std::printf("[VoxBake] resolution: %u³\n", resolution);

    // Voxelise
    VoxGrid grid;
    VoxelizerConfig cfg;
    cfg.resolution = resolution;

    const std::string err = voxelize_gltf(inputPath, grid, cfg);
    if (!err.empty())
    {
        std::fprintf(stderr, "Error: %s\n", err.c_str());
        return 2;
    }

    std::printf("[VoxBake] voxels  : %zu\n", grid.voxels.size());

    // Write .vox
    if (!write_vox(outPath.string(), grid))
    {
        std::fprintf(stderr, "Error: failed to write '%s'\n", outPath.string().c_str());
        return 3;
    }

    std::printf("[VoxBake] done → %s\n", outPath.string().c_str());
    return 0;
}
