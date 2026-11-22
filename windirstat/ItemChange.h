// WinDirStat - Directory Statistics
// Copyright © WinDirStat Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#pragma once

#include "pch.h"
#include "SnapshotHistory.h"
#include "TreeListControl.h"

using ITEMCHANGECOLUMNS = enum : std::uint8_t
{
    COL_ITEMCHANGE_NAME,
    COL_ITEMCHANGE_DELTA,
    COL_ITEMCHANGE_CURRENT_SIZE,
    COL_ITEMCHANGE_PREVIOUS_SIZE,
    COL_ITEMCHANGE_FILES_DELTA,
    COL_ITEMCHANGE_FOLDERS_DELTA,
};

class CItemChange final : public CTreeListItem
{
    static constexpr size_t CachedTextCount = COL_ITEMCHANGE_FOLDERS_DELTA + 1;

    std::vector<std::unique_ptr<CItemChange>> m_children;
    std::optional<SnapshotGrowthEntry> m_entry;
    std::array<std::wstring, CachedTextCount> m_cachedText;
    std::wstring m_previousSnapshotLabel;
    size_t m_changeCount = 0;
    size_t m_resultIndex = 0;
    HICON m_shellIcon = nullptr;

public:
    CItemChange(const CItemChange&) = delete;
    CItemChange(CItemChange&&) = delete;
    CItemChange& operator=(const CItemChange&) = delete;
    CItemChange& operator=(CItemChange&&) = delete;
    CItemChange() = default;
    CItemChange(const SnapshotGrowthEntry& entry, size_t resultIndex);

    bool DrawSubItem(int subitem, CDC* pdc, CRect rc, UINT state, int* width, int* focusLeft) override;
    std::wstring GetText(int subitem) const override;
    int CompareSibling(const CTreeListItem* tlib, int subitem) const override;
    int CompareSort(const CWdsListItem* other, const SSorting& sorting) const override;
    int GetTreeListChildCount() const override;
    CTreeListItem* GetTreeListChild(int i) const override;
    HICON GetIcon() override;
    CItem* GetLinkedItem() noexcept override { return m_entry.has_value() ? m_entry->currentItem : nullptr; }

    const std::wstring& GetChangePath() const { return m_entry->path; }
    void AddChangeItemChild(std::unique_ptr<CItemChange> child);
    void RemoveChangeItemChildren(const std::unordered_set<CItemChange*>& removed);
    void SetLinkedItem(CItem* item) noexcept { m_entry->currentItem = item; }
    void SetChangeCount(size_t count) noexcept { m_changeCount = count; }
    void SetSnapshotInfo(const std::wstring& label, size_t changeCount);
};
