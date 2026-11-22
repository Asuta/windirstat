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
#include "FileChangeControl.h"
#include "Finder.h"

CFileChangeControl::CFileChangeControl() : CTreeListControl(COptions::ChangeViewColumnOrder.Ptr(), COptions::ChangeViewColumnWidths.Ptr(), LF_CHANGELIST, false)
{
    m_singleton = this;
}

bool CFileChangeControl::GetAscendingDefault(const int column)
{
    return column == COL_ITEMCHANGE_NAME;
}

BEGIN_MESSAGE_MAP(CFileChangeControl, CTreeListControl)
END_MESSAGE_MAP()

CFileChangeControl* CFileChangeControl::m_singleton = nullptr;

std::wstring CFileChangeControl::GetAllChangePaths() const
{
    std::wstring paths;
    if (m_rootItem == nullptr) return paths;

    std::vector<const CItemChange*> pending{ m_rootItem.get() };
    while (!pending.empty())
    {
        const auto* item = pending.back();
        pending.pop_back();
        if (item != m_rootItem.get())
        {
            if (!paths.empty()) paths += L"\r\n";
            paths += item->GetChangePath();
        }
        for (int i = item->GetTreeListChildCount(); i > 0; --i)
        {
            pending.push_back(static_cast<const CItemChange*>(item->GetTreeListChild(i - 1)));
        }
    }
    return paths;
}

void CFileChangeControl::SetChanges(const SnapshotGrowthResult& result)
{
    SetRootItem();
    m_changeCount = result.entries.size();
    m_rootItem->SetSnapshotInfo(result.previousSnapshotLabel, m_changeCount);
    m_rootItem->SetExpanded(false);

    std::vector<std::unique_ptr<CItemChange>> items;
    items.reserve(result.entries.size());
    m_itemsByScanItem.reserve(result.entries.size());
    for (const auto& entry : result.entries)
    {
        auto& item = items.emplace_back(std::make_unique<CItemChange>(entry, items.size()));
        if (entry.currentItem != nullptr)
        {
            m_itemsByScanItem.try_emplace(entry.currentItem, item.get());
        }
    }

    // Keep only the comparison entries, skipping ancestors that did not qualify for the result.
    for (auto& item : items)
    {
        auto* parent = m_rootItem.get();
        const auto* current = item->GetLinkedItem();
        for (auto* ancestor = current == nullptr ? nullptr : current->GetParent();
            ancestor != nullptr; ancestor = ancestor->GetParent())
        {
            if (const auto found = m_itemsByScanItem.find(ancestor); found != m_itemsByScanItem.end())
            {
                parent = found->second;
                break;
            }
        }
        parent->AddChangeItemChild(std::move(item));
    }

    if (m_changeCount != 0)
    {
        SetRedraw(FALSE);
        ExpandItem(0, false);
        SetRedraw(TRUE);
    }
    else
    {
        m_rootItem->SetExpanded(true);
    }
    UpdateSortIndicator();
    Invalidate();
}

void CFileChangeControl::ClearChanges()
{
    SetRootItem();
}

void CFileChangeControl::PrepareDeletionRefresh(const std::vector<CItem*>& roots)
{
    if (m_changeCount == 0) return;

    for (const auto* root : roots)
    {
        // The scan may destroy these CItems and recreate survivors at the same paths.
        std::vector<const CItem*> pending{ root };
        while (!pending.empty())
        {
            const auto* item = pending.back();
            pending.pop_back();
            if (const auto match = m_itemsByScanItem.find(item); match != m_itemsByScanItem.end())
            {
                auto* change = match->second;
                m_unlinkedItems.try_emplace(item->GetPath(), change);
                change->SetLinkedItem(nullptr);
                m_itemsByScanItem.erase(match);
            }
            if (item->HasChildren())
            {
                const auto& children = item->GetChildren();
                pending.insert(pending.end(), children.begin(), children.end());
            }
        }
    }
}

bool CFileChangeControl::HasUnlinkedChangesUnder(const std::wstring& path) const
{
    if (path.empty()) return false;
    if (m_unlinkedItems.contains(path)) return true;

    // Include descendants, but exclude siblings that merely share the directory name prefix.
    const auto prefix = path.ends_with(L'\\') ? path : path + L'\\';
    const auto first = m_unlinkedItems.lower_bound(prefix);
    return first != m_unlinkedItems.end() && first->first.starts_with(prefix);
}

void CFileChangeControl::FinishDeletionRefresh(const std::vector<CItem*>& roots)
{
    if (m_unlinkedItems.empty()) return;

    for (auto* root : roots)
    {
        if (m_unlinkedItems.empty()) break;
        if (!HasUnlinkedChangesUnder(root->GetPath())) continue;
        std::vector<CItem*> pending{ root };
        while (!pending.empty() && !m_unlinkedItems.empty())
        {
            auto* item = pending.back();
            pending.pop_back();
            if (item->IsTypeOrFlag(IT_DRIVE, IT_DIRECTORY, IT_FILE))
            {
                if (const auto match = m_unlinkedItems.find(item->GetPath()); match != m_unlinkedItems.end())
                {
                    match->second->SetLinkedItem(item);
                    m_itemsByScanItem.try_emplace(item, match->second);
                    m_unlinkedItems.erase(match);
                }
            }
            if (item->HasChildren())
            {
                const auto& children = item->GetChildren();
                pending.insert(pending.end(), children.begin(), children.end());
            }
        }
    }

    // A partial or cancelled scan alone does not establish that a path was deleted.
    std::unordered_set<CItemChange*> removed;
    for (const auto& [path, item] : m_unlinkedItems)
    {
        if (GetFileAttributes(Finder::MakeLongPathCompatible(path).c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) removed.insert(item);
    }
    if (!removed.empty()) RemoveChanges(removed);
}

void CFileChangeControl::RemoveChanges(std::unordered_set<CItemChange*>& removed)
{
    std::vector<CItemChange*> pending(removed.begin(), removed.end());
    std::unordered_set<CItemChange*> parents;
    std::unordered_set<const CWdsListItem*> visibleRows;
    for (size_t i = 0; i < pending.size(); ++i)
    {
        auto* item = pending[i];
        parents.insert(static_cast<CItemChange*>(item->GetParent()));
        if (auto* scanItem = item->GetLinkedItem(); scanItem != nullptr) m_itemsByScanItem.erase(scanItem);
        if (item->IsVisible()) visibleRows.insert(item);
        for (int childIndex = 0; childIndex < item->GetTreeListChildCount(); ++childIndex)
        {
            auto* child = static_cast<CItemChange*>(item->GetTreeListChild(childIndex));
            if (removed.insert(child).second) pending.push_back(child);
        }
    }

    SetRedraw(FALSE);
    if (!visibleRows.empty())
    {
        int top = GetTopIndex();
        int anchor = top;
        while (anchor < GetItemCount() && visibleRows.contains(GetItem(anchor))) ++anchor;
        if (anchor == GetItemCount())
        {
            anchor = top - 1;
            while (anchor > 0 && visibleRows.contains(GetItem(anchor))) --anchor;
        }
        const auto* firstVisible = GetItem(anchor);
        RemoveListItems(visibleRows);
        const int newIndex = FindTreeItem(firstVisible);
        if (newIndex >= 0) Scroll(CSize(0, (newIndex - GetTopIndex()) * GetRowHeight()));
    }
    std::erase_if(m_unlinkedItems, [&](const auto& entry) { return removed.contains(entry.second); });
    for (auto* parent : parents)
    {
        if (!removed.contains(parent)) parent->RemoveChangeItemChildren(removed);
    }
    m_changeCount -= removed.size();
    m_rootItem->SetChangeCount(m_changeCount);
    SetRedraw(TRUE);
    Invalidate();
}

void CFileChangeControl::AfterDeleteAllItems()
{
    m_itemsByScanItem.clear();
    m_unlinkedItems.clear();
    m_changeCount = 0;
    m_rootItem = std::make_unique<CItemChange>();
    InsertItem(0, m_rootItem.get());
    m_rootItem->SetExpanded(true);
}
