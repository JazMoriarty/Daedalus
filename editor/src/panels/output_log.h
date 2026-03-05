#pragma once

#include <cstddef>

namespace daedalus::editor
{

class EditMapDocument;

class OutputLog
{
public:
    OutputLog()  = default;
    ~OutputLog() = default;

    void draw(const EditMapDocument& doc);

private:
    std::size_t m_lastCount = 0;  ///< Used to detect new messages for auto-scroll.
};

} // namespace daedalus::editor
