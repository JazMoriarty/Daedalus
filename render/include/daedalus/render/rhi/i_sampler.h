#pragma once

#include "daedalus/render/rhi/rhi_types.h"

namespace daedalus::rhi
{

class ISampler
{
public:
    virtual ~ISampler() = default;

    ISampler(const ISampler&)            = delete;
    ISampler& operator=(const ISampler&) = delete;

    [[nodiscard]] virtual const SamplerDescriptor& descriptor() const noexcept = 0;

protected:
    ISampler() = default;
};

} // namespace daedalus::rhi
