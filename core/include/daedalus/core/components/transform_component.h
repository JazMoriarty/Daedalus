// transform_component.h
// ECS component representing a world-space transform decomposed into
// translation, rotation, and scale.  Combine with StaticMeshComponent or
// BillboardSpriteComponent to drive the render pipeline via ECS systems.

#pragma once

#include "daedalus/core/types.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace daedalus
{

// ─── TransformComponent ───────────────────────────────────────────────────────
// Stores a world-space transform as position + orientation (quaternion) + scale.
// Call toMatrix() to get the combined model matrix for GPU upload.

struct TransformComponent
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // w=1 identity
    glm::vec3 scale    = glm::vec3(1.0f);

    // Returns the TRS model matrix: translate(position) * mat4(rotation) * scale(scale).
    [[nodiscard]] glm::mat4 toMatrix() const noexcept
    {
        const glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
        const glm::mat4 R = glm::mat4_cast(rotation);
        const glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        return T * R * S;
    }
};

} // namespace daedalus
