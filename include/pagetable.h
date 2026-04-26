#ifndef __PAGETABLE_H_
#define __PAGETABLE_H_

#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>

struct PageTableKeyComparator
{
    bool operator()(const std::string& str1, const std::string& str2) const;
};

class PageTable {
private:
    int _page_size;
    int _max_frames;
    std::map<std::string, int, PageTableKeyComparator> _table;
    std::set<int> _used_frames;

    std::vector<std::string> sortedKeys();

public:
    PageTable(int page_size);
    ~PageTable();

    int getPageSize() const;
    int getFrameCount() const;
    int getUsedFrameCount() const;
    int getFreeFrameCount() const;

    void addEntry(uint32_t pid, int page_number);
    bool hasEntry(uint32_t pid, int page_number) const;
    void removeEntry(uint32_t pid, int page_number);
    void removeProcess(uint32_t pid);

    int getPhysicalAddress(uint32_t pid, uint32_t virtual_address);
    void print();

    static void parseKey(const std::string& key, uint32_t& pid, int& page_number);
};

#endif // __PAGETABLE_H_
