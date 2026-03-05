#pragma once

#include "daedalus/core/types.h"

#include <string_view>

namespace daedalus::rhi
{

enum class ShaderStage : u32
{
    Vertex,
    Fragment,
    Compute,
};

// ─── IShader ──────────────────────────────────────────────────────────────────
// A compiled GPU program for one shader stage.

class IShader
{
public:
    virtual ~IShader() = default;

    IShader(const IShader&)            = delete;
    IShader& operator=(const IShader&) = delete;

    [[nodiscard]] virtual ShaderStage  stage()      const noexcept = 0;
    [[nodiscard]] virtual std::string_view entryPoint() const noexcept = 0;

protected:
    IShader() = default;
};

} // namespace daedalus::rhi
