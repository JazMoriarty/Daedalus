#include "cmd_set_map_meta.h"
#include "daedalus/editor/edit_map_document.h"

namespace daedalus::editor
{

CmdSetMapMeta::CmdSetMapMeta(EditMapDocument& doc,
                             std::string      oldName,
                             std::string      newName,
                             std::string      oldAuthor,
                             std::string      newAuthor)
    : m_doc(doc)
    , m_oldName(std::move(oldName))
    , m_newName(std::move(newName))
    , m_oldAuthor(std::move(oldAuthor))
    , m_newAuthor(std::move(newAuthor))
{}

void CmdSetMapMeta::execute()
{
    m_doc.mapData().name   = m_newName;
    m_doc.mapData().author = m_newAuthor;
    m_doc.markDirty();
}

void CmdSetMapMeta::undo()
{
    m_doc.mapData().name   = m_oldName;
    m_doc.mapData().author = m_oldAuthor;
    m_doc.markDirty();
}

} // namespace daedalus::editor
