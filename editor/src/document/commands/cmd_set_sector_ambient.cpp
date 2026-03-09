#include "cmd_set_sector_ambient.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetSectorAmbient::CmdSetSectorAmbient(EditMapDocument& doc,
                                         world::SectorId  sectorId,
                                         glm::vec3        newColor,
                                         float            newIntensity)
    : m_doc(doc)
    , m_sectorId(sectorId)
    , m_newColor(newColor)
    , m_newIntensity(newIntensity)
{
    const auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        m_oldColor     = sectors[m_sectorId].ambientColor;
        m_oldIntensity = sectors[m_sectorId].ambientIntensity;
    }
}

void CmdSetSectorAmbient::execute()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        sectors[m_sectorId].ambientColor     = m_newColor;
        sectors[m_sectorId].ambientIntensity = m_newIntensity;
    }
}

void CmdSetSectorAmbient::undo()
{
    auto& sectors = m_doc.mapData().sectors;
    if (m_sectorId < sectors.size())
    {
        sectors[m_sectorId].ambientColor     = m_oldColor;
        sectors[m_sectorId].ambientIntensity = m_oldIntensity;
    }
}

} // namespace daedalus::editor
