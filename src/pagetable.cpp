#include <algorithm>
#include <iomanip>
#include <iostream>
#include "../include/pagetable.h"

static const int SIM_PHYSICAL_MEMORY = 67108864;

bool PageTableKeyComparator::operator()(const std::string& a, const std::string& b) const
{
    uint32_t pid_a, pid_b;
    int page_a, page_b;

    PageTable::parseKey(a, pid_a, page_a);
    PageTable::parseKey(b, pid_b, page_b);

    if (pid_a != pid_b)
    {
        return pid_a < pid_b;
    }

    return page_a < page_b;
}

PageTable::PageTable(int page_size)
{
    _page_size = page_size;
    _max_frames = SIM_PHYSICAL_MEMORY / _page_size;
}

PageTable::~PageTable()
{
}

void PageTable::parseKey(const std::string& key, uint32_t& pid, int& page_number)
{
    size_t sep = key.find("|");
    pid = static_cast<uint32_t>(std::stoul(key.substr(0, sep)));
    page_number = std::stoi(key.substr(sep + 1));
}

std::vector<std::string> PageTable::sortedKeys()
{
    std::vector<std::string> keys;

    std::map<std::string, int, PageTableKeyComparator>::iterator it;
    for (it = _table.begin(); it != _table.end(); it++)
    {
        keys.push_back(it->first);
    }

    std::sort(keys.begin(), keys.end(), PageTableKeyComparator());

    return keys;
}

int PageTable::getPageSize() const
{
    return _page_size;
}

int PageTable::getFrameCount() const
{
    return _max_frames;
}

int PageTable::getUsedFrameCount() const
{
    return static_cast<int>(_used_frames.size());
}

int PageTable::getFreeFrameCount() const
{
    return _max_frames - static_cast<int>(_used_frames.size());
}

bool PageTable::hasEntry(uint32_t pid, int page_number) const
{
    std::string entry = std::to_string(pid) + "|" + std::to_string(page_number);
    return _table.count(entry) > 0;
}

void PageTable::addEntry(uint32_t pid, int page_number)
{
    std::string entry = std::to_string(pid) + "|" + std::to_string(page_number);

    if (_table.count(entry) > 0)
    {
        return;
    }

    int frame = -1;
    for (int i = 0; i < _max_frames; i++)
    {
        if (_used_frames.count(i) == 0)
        {
            frame = i;
            break;
        }
    }

    if (frame == -1)
    {
        return;
    }

    _table[entry] = frame;
    _used_frames.insert(frame);
}

void PageTable::removeEntry(uint32_t pid, int page_number)
{
    std::string entry = std::to_string(pid) + "|" + std::to_string(page_number);

    std::map<std::string, int, PageTableKeyComparator>::iterator it = _table.find(entry);
    if (it != _table.end())
    {
        _used_frames.erase(it->second);
        _table.erase(it);
    }
}

void PageTable::removeProcess(uint32_t pid)
{
    for (std::map<std::string, int, PageTableKeyComparator>::iterator it = _table.begin(); it != _table.end(); )
    {
        uint32_t entry_pid;
        int page_number;
        parseKey(it->first, entry_pid, page_number);

        if (entry_pid == pid)
        {
            _used_frames.erase(it->second);
            it = _table.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

int PageTable::getPhysicalAddress(uint32_t pid, uint32_t virtual_address)
{
    int page_number = static_cast<int>(virtual_address / static_cast<uint32_t>(_page_size));
    int page_offset = static_cast<int>(virtual_address % static_cast<uint32_t>(_page_size));

    std::string entry = std::to_string(pid) + "|" + std::to_string(page_number);
    
    int address = -1;
    if (_table.count(entry) > 0)
    {
        int frame = _table[entry];
        address = frame * _page_size + page_offset;
    }

    return address;
}

void PageTable::print()
{
    std::cout << " PID  | Page Number | Frame Number" << std::endl;
    std::cout << "------+-------------+--------------" << std::endl;

    std::vector<std::string> keys = sortedKeys();

    for (int i = 0; i < static_cast<int>(keys.size()); i++)
    {
        uint32_t pid;
        int page_number;
        parseKey(keys[i], pid, page_number);

        std::cout << " " << std::setw(4) << pid << " | "
                  << std::setw(11) << page_number << " | "
                  << std::setw(12) << _table[keys[i]] << std::endl;
    }
}
