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

#include "pch.h"
#include "ItemChange.h"

namespace
{
    ULONGLONG Magnitude(const LONGLONG value)
    {
        return value >= 0 ? static_cast<ULONGLONG>(value) : static_cast<ULONGLONG>(-(value + 1)) + 1;
    }

    std::wstring FormatSignedBytes(const LONGLONG value)
    {
        if (value == 0) return FormatBytes(0);
        return (value > 0 ? L"+" : L"-") + FormatBytes(Magnitude(value));
    }

    std::wstring FormatSignedCountValue(const LONGLONG value)
    {
        if (value == 0) return L"0";
        return value > 0 ? L"+" + FormatCount(value) : L"-" + FormatCount(Magnitude(value));
    }
}

CItemChange::CItemChange(const SnapshotGrowthEntry& entry, const size_t resultIndex) : m_entry(entry), m_resultIndex(resultIndex)
{
    m_cachedText[COL_ITEMCHANGE_NAME] = m_entry->path;
    m_cachedText[COL_ITEMCHANGE_DELTA] = FormatSignedBytes(m_entry->deltaSizePhysical);
    m_cachedText[COL_ITEMCHANGE_CURRENT_SIZE] = FormatBytes(m_entry->currentSizePhysical);
    m_cachedText[COL_ITEMCHANGE_PREVIOUS_SIZE] = FormatBytes(m_entry->previousSizePhysical);
    m_cachedText[COL_ITEMCHANGE_FILES_DELTA] = FormatSignedCountValue(m_entry->deltaFiles);
    m_cachedText[COL_ITEMCHANGE_FOLDERS_DELTA] = FormatSignedCountValue(m_entry->deltaFolders);
}

bool CItemChange::DrawSubItem(const int subitem, CDC* pdc, const CRect rc, const UINT state, int* width, int* focusLeft)
{
    return subitem == COL_ITEMCHANGE_NAME ? CTreeListItem::DrawSubItem(subitem, pdc, rc, state, width, focusLeft) : false;
}

std::wstring CItemChange::GetText(const int subitem) const
{
    if (GetParent() == nullptr)
    {
        if (subitem != COL_ITEMCHANGE_NAME) return {};

        std::wstring text = Localization::Lookup(IDS_CHANGES);
        if (!m_previousSnapshotLabel.empty()) text += L" (" + m_previousSnapshotLabel + L", " + FormatCount(m_changeCount) + L")";
        return text;
    }

    if (!m_entry.has_value() || subitem < 0 || subitem >= static_cast<int>(m_cachedText.size())) return {};
    return m_cachedText[subitem];
}

int CItemChange::CompareSibling(const CTreeListItem* tlib, const int subitem) const
{
    if (GetParent() == nullptr || !m_entry.has_value()) return 0;

    const auto* other = reinterpret_cast<const CItemChange*>(tlib);
    if (other == nullptr || !other->m_entry.has_value()) return 0;

    switch (subitem)
    {
    case COL_ITEMCHANGE_DELTA:
        return signum(m_entry->deltaSizePhysical - other->m_entry->deltaSizePhysical);
    case COL_ITEMCHANGE_CURRENT_SIZE:
        return usignum(m_entry->currentSizePhysical, other->m_entry->currentSizePhysical);
    case COL_ITEMCHANGE_PREVIOUS_SIZE:
        return usignum(m_entry->previousSizePhysical, other->m_entry->previousSizePhysical);
    case COL_ITEMCHANGE_FILES_DELTA:
        return signum(m_entry->deltaFiles - other->m_entry->deltaFiles);
    case COL_ITEMCHANGE_FOLDERS_DELTA:
        return signum(m_entry->deltaFolders - other->m_entry->deltaFolders);
    case COL_ITEMCHANGE_NAME:
    default:
        return signum(_wcsicmp(m_entry->path.c_str(), other->m_entry->path.c_str()));
    }
}

int CItemChange::CompareSort(const CWdsListItem* other, const SSorting& sorting) const
{
    const auto* left = this;
    const auto* right = static_cast<const CItemChange*>(other);
    if (left == right) return 0;

    if (left->GetParent() != right->GetParent())
    {
        // Sort branches by their sibling roots and keep ancestors before their descendants.
        while (left->GetIndent() > right->GetIndent()) left = static_cast<const CItemChange*>(left->GetParent());
        if (left == right) return 1;
        while (right->GetIndent() > left->GetIndent()) right = static_cast<const CItemChange*>(right->GetParent());
        if (left == right) return -1;

        while (left->GetParent() != right->GetParent())
        {
            left = static_cast<const CItemChange*>(left->GetParent());
            right = static_cast<const CItemChange*>(right->GetParent());
        }
    }

    if (const int order = left->CWdsListItem::CompareSort(right, sorting); order != 0) return order;
    return usignum(left->m_resultIndex, right->m_resultIndex);
}

int CItemChange::GetTreeListChildCount() const
{
    return static_cast<int>(m_children.size());
}

CTreeListItem* CItemChange::GetTreeListChild(const int i) const
{
    return m_children[i].get();
}

HICON CItemChange::GetIcon()
{
    if (m_visualInfo == nullptr) return nullptr;
    if (!m_entry.has_value() || m_entry->currentItem == nullptr) return GetIconHandler()->GetSearchImage();

    if (m_visualInfo->icon == nullptr)
    {
        // The lookup can outlive the visible row when a branch is collapsed and reopened.
        GetIconHandler()->DoAsyncShellInfoLookup(std::make_tuple(this, m_visualInfo->control,
            m_entry->currentItem->GetPath(), m_entry->currentItem->GetAttributes(), &m_shellIcon, nullptr));
        m_visualInfo->icon = m_shellIcon;
    }
    return m_shellIcon;
}

void CItemChange::AddChangeItemChild(std::unique_ptr<CItemChange> child)
{
    child->SetParent(this);
    m_children.push_back(std::move(child));
}

void CItemChange::RemoveChangeItemChildren(const std::unordered_set<CItemChange*>& removed)
{
    std::erase_if(m_children, [&](const auto& child) { return removed.contains(child.get()); });
    if (IsVisible() && m_children.empty()) SetExpanded(false);
}

void CItemChange::SetSnapshotInfo(const std::wstring& label, const size_t changeCount)
{
    m_previousSnapshotLabel = label;
    m_changeCount = changeCount;
}
