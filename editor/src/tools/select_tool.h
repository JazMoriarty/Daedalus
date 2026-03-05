#pragma once

#include "daedalus/editor/i_editor_tool.h"
#include "daedalus/world/map_data.h"

namespace daedalus::editor
{

class SelectTool final : public IEditorTool
{
public:
    SelectTool()  = default;
    ~SelectTool() = default;

    void onMouseDown(EditMapDocument& doc,
                     float mapX, float mapZ,
                     int   button) override;

private:
    [[nodiscard]] world::SectorId hitTestSector(
        const world::WorldMapData& map,
        float mapX, float mapZ) const noexcept;
};

} // namespace daedalus::editor
