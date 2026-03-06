// test_billboard_render_system.cpp
// Unit tests for the pure functions in billboard_render_system.h:
//   makeUnitQuadMesh()    — vertex layout, UV, index winding
//   makeBillboardMatrix() — position, size scaling, camera-axis extraction

#include "daedalus/render/systems/billboard_render_system.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace daedalus::render;

static constexpr float k_eps = 1e-5f;

// ─── makeUnitQuadMesh ─────────────────────────────────────────────────────────

TEST(UnitQuadMesh, HasFourVertices)
{
    const MeshData mesh = makeUnitQuadMesh();
    ASSERT_EQ(mesh.vertices.size(), 4u);
}

TEST(UnitQuadMesh, HasSixIndices)
{
    const MeshData mesh = makeUnitQuadMesh();
    ASSERT_EQ(mesh.indices.size(), 6u);
}

TEST(UnitQuadMesh, AllVerticesLieInZeroZPlane)
{
    const MeshData mesh = makeUnitQuadMesh();
    for (const auto& v : mesh.vertices)
        EXPECT_NEAR(v.pos[2], 0.0f, k_eps);
}

TEST(UnitQuadMesh, VertexPositionsFormUnitSquare)
{
    const MeshData mesh = makeUnitQuadMesh();
    // v0=BL, v1=BR, v2=TR, v3=TL
    // BL
    EXPECT_NEAR(mesh.vertices[0].pos[0], -0.5f, k_eps);
    EXPECT_NEAR(mesh.vertices[0].pos[1], -0.5f, k_eps);
    // BR
    EXPECT_NEAR(mesh.vertices[1].pos[0],  0.5f, k_eps);
    EXPECT_NEAR(mesh.vertices[1].pos[1], -0.5f, k_eps);
    // TR
    EXPECT_NEAR(mesh.vertices[2].pos[0],  0.5f, k_eps);
    EXPECT_NEAR(mesh.vertices[2].pos[1],  0.5f, k_eps);
    // TL
    EXPECT_NEAR(mesh.vertices[3].pos[0], -0.5f, k_eps);
    EXPECT_NEAR(mesh.vertices[3].pos[1],  0.5f, k_eps);
}

TEST(UnitQuadMesh, AllNormalsPointTowardPositiveZ)
{
    const MeshData mesh = makeUnitQuadMesh();
    for (const auto& v : mesh.vertices)
    {
        EXPECT_NEAR(v.normal[0], 0.0f, k_eps);
        EXPECT_NEAR(v.normal[1], 0.0f, k_eps);
        EXPECT_NEAR(v.normal[2], 1.0f, k_eps);
    }
}

TEST(UnitQuadMesh, TangentsArePositiveX)
{
    const MeshData mesh = makeUnitQuadMesh();
    for (const auto& v : mesh.vertices)
    {
        EXPECT_NEAR(v.tangent[0], 1.0f, k_eps);
        EXPECT_NEAR(v.tangent[1], 0.0f, k_eps);
        EXPECT_NEAR(v.tangent[2], 0.0f, k_eps);
        EXPECT_NEAR(v.tangent[3], 1.0f, k_eps);  // handedness = +1
    }
}

TEST(UnitQuadMesh, UVLayoutMatchesSpec)
{
    // Metal UV convention: (0,0) = top-left.
    // v0=BL (0,1)  v1=BR (1,1)  v2=TR (1,0)  v3=TL (0,0)
    const MeshData mesh = makeUnitQuadMesh();

    EXPECT_NEAR(mesh.vertices[0].uv[0], 0.0f, k_eps);  // BL u
    EXPECT_NEAR(mesh.vertices[0].uv[1], 1.0f, k_eps);  // BL v

    EXPECT_NEAR(mesh.vertices[1].uv[0], 1.0f, k_eps);  // BR u
    EXPECT_NEAR(mesh.vertices[1].uv[1], 1.0f, k_eps);  // BR v

    EXPECT_NEAR(mesh.vertices[2].uv[0], 1.0f, k_eps);  // TR u
    EXPECT_NEAR(mesh.vertices[2].uv[1], 0.0f, k_eps);  // TR v

    EXPECT_NEAR(mesh.vertices[3].uv[0], 0.0f, k_eps);  // TL u
    EXPECT_NEAR(mesh.vertices[3].uv[1], 0.0f, k_eps);  // TL v
}

TEST(UnitQuadMesh, IndexWindingMatches_0_2_1__0_3_2)
{
    // Matches the box-mesh vertical-face convention.
    const MeshData mesh = makeUnitQuadMesh();
    EXPECT_EQ(mesh.indices[0], 0u);
    EXPECT_EQ(mesh.indices[1], 2u);
    EXPECT_EQ(mesh.indices[2], 1u);
    EXPECT_EQ(mesh.indices[3], 0u);
    EXPECT_EQ(mesh.indices[4], 3u);
    EXPECT_EQ(mesh.indices[5], 2u);
}

// ─── makeBillboardMatrix ──────────────────────────────────────────────────────

TEST(BillboardMatrix, PositionStoredInColumn3)
{
    const glm::mat4 view(1.0f);
    const glm::mat4 m = makeBillboardMatrix({3.0f, -1.5f, 7.0f}, {1.0f, 1.0f}, view);

    EXPECT_NEAR(m[3][0],  3.0f, k_eps);
    EXPECT_NEAR(m[3][1], -1.5f, k_eps);
    EXPECT_NEAR(m[3][2],  7.0f, k_eps);
    EXPECT_NEAR(m[3][3],  1.0f, k_eps);
}

TEST(BillboardMatrix, IdentityViewRightIsWorldX_UpIsWorldY)
{
    // Identity view → camera right = (1,0,0), up = (0,1,0).
    // Column 0 of the billboard matrix = right × size.x.
    // Column 1 = up × size.y.
    const glm::mat4 view(1.0f);
    const glm::mat4 m = makeBillboardMatrix({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, view);

    EXPECT_NEAR(m[0][0], 1.0f, k_eps);  // right.x × 1
    EXPECT_NEAR(m[0][1], 0.0f, k_eps);
    EXPECT_NEAR(m[0][2], 0.0f, k_eps);

    EXPECT_NEAR(m[1][0], 0.0f, k_eps);
    EXPECT_NEAR(m[1][1], 1.0f, k_eps);  // up.y × 1
    EXPECT_NEAR(m[1][2], 0.0f, k_eps);
}

TEST(BillboardMatrix, SizeScalesCameraAxes)
{
    const glm::mat4 view(1.0f);
    const glm::mat4 m = makeBillboardMatrix({0.0f, 0.0f, 0.0f}, {3.0f, 5.0f}, view);

    // Column 0 magnitude should equal size.x = 3.
    const glm::vec3 col0 = { m[0][0], m[0][1], m[0][2] };
    EXPECT_NEAR(glm::length(col0), 3.0f, k_eps);

    // Column 1 magnitude should equal size.y = 5.
    const glm::vec3 col1 = { m[1][0], m[1][1], m[1][2] };
    EXPECT_NEAR(glm::length(col1), 5.0f, k_eps);
}

TEST(BillboardMatrix, ExtractedAxesMatchViewMatrix)
{
    // Use a non-trivial view matrix and verify the billboard extracts the
    // correct right/up vectors from it.
    const glm::mat4 view = glm::lookAtLH(
        glm::vec3(0.0f, 2.0f, -5.0f),   // eye
        glm::vec3(0.0f, 0.0f,  0.0f),   // target
        glm::vec3(0.0f, 1.0f,  0.0f));  // up

    // Extract expected axes by the same formula used in makeBillboardMatrix.
    const glm::vec3 expectedRight = { view[0][0], view[1][0], view[2][0] };
    const glm::vec3 expectedUp    = { view[0][1], view[1][1], view[2][1] };

    const glm::mat4 m = makeBillboardMatrix({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, view);

    EXPECT_NEAR(m[0][0], expectedRight.x, k_eps);
    EXPECT_NEAR(m[0][1], expectedRight.y, k_eps);
    EXPECT_NEAR(m[0][2], expectedRight.z, k_eps);

    EXPECT_NEAR(m[1][0], expectedUp.x, k_eps);
    EXPECT_NEAR(m[1][1], expectedUp.y, k_eps);
    EXPECT_NEAR(m[1][2], expectedUp.z, k_eps);
}

TEST(BillboardMatrix, ForwardAxisIsUnitLength)
{
    // The forward column (col 2) must always be unit-length since it is
    // cross(normalised_right, normalised_up) of an orthonormal view matrix.
    const glm::mat4 view = glm::lookAtLH(
        glm::vec3(3.0f, 1.0f, -4.0f),
        glm::vec3(0.0f, 0.0f,  0.0f),
        glm::vec3(0.0f, 1.0f,  0.0f));

    const glm::mat4 m   = makeBillboardMatrix({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, view);
    const glm::vec3 fwd = { m[2][0], m[2][1], m[2][2] };

    EXPECT_NEAR(glm::length(fwd), 1.0f, k_eps);
}
