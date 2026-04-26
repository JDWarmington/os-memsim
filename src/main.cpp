#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "../include/mmu.h"
#include "../include/pagetable.h"

// 64 MB (64 * 1024 * 1024)
#define PHYSICAL_MEMORY 67108864
#define STACK_SIZE 65536

void printStartMessage(int page_size);
void createProcess(int text_size, int data_size, Mmu *mmu, PageTable *page_table);
void allocateVariable(uint32_t pid, std::string var_name, DataType type, uint32_t num_elements, Mmu *mmu, PageTable *page_table);
void setVariable(uint32_t pid, std::string var_name, uint32_t offset, void *value, Mmu *mmu, PageTable *page_table, uint8_t *memory);
void freeVariable(uint32_t pid, std::string var_name, Mmu *mmu, PageTable *page_table);
void terminateProcess(uint32_t pid, Mmu *mmu, PageTable *page_table);

static bool isPowerOfTwo(int value)
{
    return value > 0 && (value & (value - 1)) == 0;
}

static uint32_t dataTypeSize(DataType type)
{
    switch (type)
    {
        case DataType::Char: return 1;
        case DataType::Short: return 2;
        case DataType::Int: return 4;
        case DataType::Float: return 4;
        case DataType::Long: return 8;
        case DataType::Double: return 8;
        default: return 0;
    }
}

static bool parseDataType(const std::string& text, DataType& type)
{
    if (text == "char") type = DataType::Char;
    else if (text == "short") type = DataType::Short;
    else if (text == "int") type = DataType::Int;
    else if (text == "float") type = DataType::Float;
    else if (text == "long") type = DataType::Long;
    else if (text == "double") type = DataType::Double;
    else return false;

    return true;
}

static void sortVariables(Process* proc)
{
    if (proc == nullptr)
    {
        return;
    }

    std::sort(proc->variables.begin(), proc->variables.end(), [](Variable* a, Variable* b)
    {
        if (a->virtual_address != b->virtual_address)
        {
            return a->virtual_address < b->virtual_address;
        }
        return a->name < b->name;
    });
}

static std::set<int> pagesTouched(uint32_t address, uint32_t size, int page_size)
{
    std::set<int> pages;
    if (size == 0)
    {
        return pages;
    }

    uint32_t first_page = address / static_cast<uint32_t>(page_size);
    uint32_t last_page = (address + size - 1) / static_cast<uint32_t>(page_size);

    for (uint32_t page = first_page; page <= last_page; page++)
    {
        pages.insert(static_cast<int>(page));
    }

    return pages;
}

static int countNewPages(uint32_t pid, uint32_t address, uint32_t size, PageTable* page_table)
{
    int count = 0;
    std::set<int> pages = pagesTouched(address, size, page_table->getPageSize());

    for (int page : pages)
    {
        if (!page_table->hasEntry(pid, page))
        {
            count++;
        }
    }

    return count;
}

static void addPagesForRange(uint32_t pid, uint32_t address, uint32_t size, PageTable* page_table)
{
    std::set<int> pages = pagesTouched(address, size, page_table->getPageSize());

    for (int page : pages)
    {
        if (!page_table->hasEntry(pid, page))
        {
            page_table->addEntry(pid, page);
        }
    }
}

static bool rangesOverlap(uint32_t a_start, uint32_t a_size, uint32_t b_start, uint32_t b_size)
{
    if (a_size == 0 || b_size == 0)
    {
        return false;
    }

    uint64_t a_end = static_cast<uint64_t>(a_start) + a_size;
    uint64_t b_end = static_cast<uint64_t>(b_start) + b_size;

    return static_cast<uint64_t>(a_start) < b_end && static_cast<uint64_t>(b_start) < a_end;
}

static bool pageHasAllocatedVariable(Process* proc, int page_number, int page_size)
{
    uint32_t page_start = static_cast<uint32_t>(page_number * page_size);
    uint32_t page_size_u = static_cast<uint32_t>(page_size);

    for (Variable* var : proc->variables)
    {
        if (var != nullptr && var->type != DataType::FreeSpace && rangesOverlap(var->virtual_address, var->size, page_start, page_size_u))
        {
            return true;
        }
    }

    return false;
}

static void coalesceFreeSpace(Process* proc)
{
    if (proc == nullptr)
    {
        return;
    }

    sortVariables(proc);

    for (std::size_t i = 0; i + 1 < proc->variables.size(); )
    {
        Variable* current = proc->variables[i];
        Variable* next = proc->variables[i + 1];

        if (current->type == DataType::FreeSpace && next->type == DataType::FreeSpace &&
            current->virtual_address + current->size == next->virtual_address)
        {
            current->size += next->size;
            delete next;
            proc->variables.erase(proc->variables.begin() + static_cast<long>(i + 1));
        }
        else
        {
            i++;
        }
    }
}

static bool reserveAtAddress(uint32_t pid, const std::string& name, DataType type, uint32_t size, uint32_t address, Mmu* mmu)
{
    Process* proc = mmu->getProcess(pid);
    if (proc == nullptr)
    {
        return false;
    }

    if (size == 0)
    {
        mmu->addVariableToProcess(pid, name, type, size, address);
        sortVariables(proc);
        return true;
    }

    uint64_t new_end = static_cast<uint64_t>(address) + size;

    for (std::vector<Variable*>::iterator it = proc->variables.begin(); it != proc->variables.end(); ++it)
    {
        Variable* free_block = *it;
        if (free_block == nullptr || free_block->type != DataType::FreeSpace)
        {
            continue;
        }

        uint32_t free_start = free_block->virtual_address;
        uint64_t free_end = static_cast<uint64_t>(free_start) + free_block->size;

        if (address >= free_start && new_end <= free_end)
        {
            uint32_t left_size = address - free_start;
            uint32_t right_size = static_cast<uint32_t>(free_end - new_end);
            uint32_t right_start = address + size;

            delete free_block;
            proc->variables.erase(it);

            if (left_size > 0)
            {
                mmu->addVariableToProcess(pid, "<FREE_SPACE>", DataType::FreeSpace, left_size, free_start);
            }

            mmu->addVariableToProcess(pid, name, type, size, address);

            if (right_size > 0)
            {
                mmu->addVariableToProcess(pid, "<FREE_SPACE>", DataType::FreeSpace, right_size, right_start);
            }

            sortVariables(proc);
            return true;
        }
    }

    return false;
}

static bool findFirstFitAddress(uint32_t pid, uint32_t size, Mmu* mmu, PageTable* page_table, uint32_t& address)
{
    Process* proc = mmu->getProcess(pid);
    if (proc == nullptr)
    {
        return false;
    }

    sortVariables(proc);
    int page_size = page_table->getPageSize();

    // First try to reuse a hole that starts inside an already allocated page.
    for (Variable* free_block : proc->variables)
    {
        if (free_block == nullptr || free_block->type != DataType::FreeSpace || free_block->size < size)
        {
            continue;
        }

        uint32_t range_start = free_block->virtual_address;
        uint64_t range_end = static_cast<uint64_t>(range_start) + free_block->size;
        uint32_t first_page = range_start / static_cast<uint32_t>(page_size);
        uint32_t last_page = static_cast<uint32_t>((range_end - 1) / static_cast<uint32_t>(page_size));

        for (uint32_t page = first_page; page <= last_page; page++)
        {
            if (!page_table->hasEntry(pid, static_cast<int>(page)))
            {
                continue;
            }

            uint32_t page_start = page * static_cast<uint32_t>(page_size);
            uint32_t candidate = std::max(range_start, page_start);

            if (static_cast<uint64_t>(candidate) + size <= range_end)
            {
                address = candidate;
                return true;
            }
        }
    }

    // If no currently allocated page has a big enough hole, use the first free range.
    for (Variable* free_block : proc->variables)
    {
        if (free_block != nullptr && free_block->type == DataType::FreeSpace && free_block->size >= size)
        {
            address = free_block->virtual_address;
            return true;
        }
    }

    return false;
}

static bool writeBytes(uint32_t pid, uint32_t virtual_address, const uint8_t* bytes, uint32_t size, PageTable* page_table, uint8_t* memory)
{
    for (uint32_t i = 0; i < size; i++)
    {
        int physical_address = page_table->getPhysicalAddress(pid, virtual_address + i);
        if (physical_address < 0)
        {
            return false;
        }
        memory[physical_address] = bytes[i];
    }

    return true;
}

static bool readBytes(uint32_t pid, uint32_t virtual_address, uint8_t* bytes, uint32_t size, PageTable* page_table, uint8_t* memory)
{
    for (uint32_t i = 0; i < size; i++)
    {
        int physical_address = page_table->getPhysicalAddress(pid, virtual_address + i);
        if (physical_address < 0)
        {
            return false;
        }
        bytes[i] = memory[physical_address];
    }

    return true;
}

static bool setValueFromToken(uint32_t pid, const std::string& var_name, uint32_t offset, const std::string& token, Mmu* mmu, PageTable* page_table, uint8_t* memory)
{
    Variable* var = mmu->getVariable(pid, var_name);
    if (var == nullptr)
    {
        return false;
    }

    try
    {
        switch (var->type)
        {
            case DataType::Char:
            {
                char value = token.empty() ? '\0' : token[0];
                setVariable(pid, var_name, offset, &value, mmu, page_table, memory);
                break;
            }
            case DataType::Short:
            {
                int16_t value = static_cast<int16_t>(std::stoi(token));
                setVariable(pid, var_name, offset, &value, mmu, page_table, memory);
                break;
            }
            case DataType::Int:
            {
                int32_t value = static_cast<int32_t>(std::stol(token));
                setVariable(pid, var_name, offset, &value, mmu, page_table, memory);
                break;
            }
            case DataType::Float:
            {
                float value = std::stof(token);
                setVariable(pid, var_name, offset, &value, mmu, page_table, memory);
                break;
            }
            case DataType::Long:
            {
                int64_t value = static_cast<int64_t>(std::stoll(token));
                setVariable(pid, var_name, offset, &value, mmu, page_table, memory);
                break;
            }
            case DataType::Double:
            {
                double value = std::stod(token);
                setVariable(pid, var_name, offset, &value, mmu, page_table, memory);
                break;
            }
            default:
                return false;
        }
    }
    catch (...)
    {
        return false;
    }

    return true;
}

static void printVariableValue(uint32_t pid, const std::string& var_name, Mmu* mmu, PageTable* page_table, uint8_t* memory)
{
    if (!mmu->processExists(pid))
    {
        std::cout << "error: process not found" << std::endl;
        return;
    }

    Variable* var = mmu->getVariable(pid, var_name);
    if (var == nullptr)
    {
        std::cout << "error: variable not found" << std::endl;
        return;
    }

    uint32_t type_size = dataTypeSize(var->type);
    if (type_size == 0)
    {
        std::cout << std::endl;
        return;
    }

    uint32_t num_elements = var->size / type_size;
    uint32_t to_print = std::min<uint32_t>(num_elements, 4);

    for (uint32_t i = 0; i < to_print; i++)
    {
        if (i > 0)
        {
            std::cout << ", ";
        }

        uint32_t element_address = var->virtual_address + i * type_size;

        switch (var->type)
        {
            case DataType::Char:
            {
                char value = '\0';
                readBytes(pid, element_address, reinterpret_cast<uint8_t*>(&value), type_size, page_table, memory);
                std::cout << value;
                break;
            }
            case DataType::Short:
            {
                int16_t value = 0;
                readBytes(pid, element_address, reinterpret_cast<uint8_t*>(&value), type_size, page_table, memory);
                std::cout << value;
                break;
            }
            case DataType::Int:
            {
                int32_t value = 0;
                readBytes(pid, element_address, reinterpret_cast<uint8_t*>(&value), type_size, page_table, memory);
                std::cout << value;
                break;
            }
            case DataType::Float:
            {
                float value = 0;
                readBytes(pid, element_address, reinterpret_cast<uint8_t*>(&value), type_size, page_table, memory);
                std::cout << value;
                break;
            }
            case DataType::Long:
            {
                int64_t value = 0;
                readBytes(pid, element_address, reinterpret_cast<uint8_t*>(&value), type_size, page_table, memory);
                std::cout << value;
                break;
            }
            case DataType::Double:
            {
                double value = 0;
                readBytes(pid, element_address, reinterpret_cast<uint8_t*>(&value), type_size, page_table, memory);
                std::cout << value;
                break;
            }
            default:
                break;
        }
    }

    if (num_elements > 4)
    {
        std::cout << ", ... [" << num_elements << " items]";
    }

    std::cout << std::endl;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Error: you must specify the page size" << std::endl;
        return 1;
    }

    int page_size = 0;
    try
    {
        page_size = std::stoi(argv[1]);
    }
    catch (...)
    {
        std::cerr << "Error: invalid page size" << std::endl;
        return 1;
    }

    if (!isPowerOfTwo(page_size) || page_size < 1024 || page_size > 32768)
    {
        std::cerr << "Error: page size must be a power of 2 between 1024 and 32768" << std::endl;
        return 1;
    }

    printStartMessage(page_size);

    uint8_t *memory = new uint8_t[PHYSICAL_MEMORY]();
    std::memset(memory, 0, PHYSICAL_MEMORY);

    Mmu *mmu = new Mmu(PHYSICAL_MEMORY);
    PageTable *page_table = new PageTable(page_size);

    std::string command;
    std::cout << "> ";
    std::getline(std::cin, command);
    while (command != "exit")
    {
        std::istringstream iss(command);
        std::string action;
        iss >> action;

        if (action == "create")
        {
            int text_size;
            int data_size;
            std::string extra;
            if ((iss >> text_size >> data_size) && !(iss >> extra))
            {
                createProcess(text_size, data_size, mmu, page_table);
            }
            else
            {
                std::cout << "error: command not recognized" << std::endl;
            }
        }
        else if (action == "allocate")
        {
            uint32_t pid;
            std::string var_name;
            std::string type_text;
            uint32_t num_elements;
            std::string extra;

            if ((iss >> pid >> var_name >> type_text >> num_elements) && !(iss >> extra))
            {
                DataType type;
                if (!parseDataType(type_text, type))
                {
                    std::cout << "error: command not recognized" << std::endl;
                }
                else if (!mmu->processExists(pid))
                {
                    std::cout << "error: process not found" << std::endl;
                }
                else if (mmu->variableExists(pid, var_name))
                {
                    std::cout << "error: variable already exists" << std::endl;
                }
                else
                {
                    allocateVariable(pid, var_name, type, num_elements, mmu, page_table);
                }
            }
            else
            {
                std::cout << "error: command not recognized" << std::endl;
            }
        }
        else if (action == "set")
        {
            uint32_t pid;
            std::string var_name;
            uint32_t offset;

            if (iss >> pid >> var_name >> offset)
            {
                std::vector<std::string> values;
                std::string value;
                while (iss >> value)
                {
                    values.push_back(value);
                }

                if (!mmu->processExists(pid))
                {
                    std::cout << "error: process not found" << std::endl;
                }
                else
                {
                    Variable* var = mmu->getVariable(pid, var_name);
                    if (var == nullptr)
                    {
                        std::cout << "error: variable not found" << std::endl;
                    }
                    else
                    {
                        uint32_t type_size = dataTypeSize(var->type);
                        uint32_t num_elements = type_size == 0 ? 0 : var->size / type_size;

                        if (values.empty() || offset + values.size() > num_elements)
                        {
                            std::cout << "error: index out of range" << std::endl;
                        }
                        else
                        {
                            bool ok = true;
                            for (std::size_t i = 0; i < values.size(); i++)
                            {
                                if (!setValueFromToken(pid, var_name, offset + static_cast<uint32_t>(i), values[i], mmu, page_table, memory))
                                {
                                    ok = false;
                                    break;
                                }
                            }

                            if (!ok)
                            {
                                std::cout << "error: command not recognized" << std::endl;
                            }
                        }
                    }
                }
            }
            else
            {
                std::cout << "error: command not recognized" << std::endl;
            }
        }
        else if (action == "free")
        {
            uint32_t pid;
            std::string var_name;
            std::string extra;

            if ((iss >> pid >> var_name) && !(iss >> extra))
            {
                if (!mmu->processExists(pid))
                {
                    std::cout << "error: process not found" << std::endl;
                }
                else if (!mmu->variableExists(pid, var_name))
                {
                    std::cout << "error: variable not found" << std::endl;
                }
                else
                {
                    freeVariable(pid, var_name, mmu, page_table);
                }
            }
            else
            {
                std::cout << "error: command not recognized" << std::endl;
            }
        }
        else if (action == "terminate")
        {
            uint32_t pid;
            std::string extra;

            if ((iss >> pid) && !(iss >> extra))
            {
                if (!mmu->processExists(pid))
                {
                    std::cout << "error: process not found" << std::endl;
                }
                else
                {
                    terminateProcess(pid, mmu, page_table);
                }
            }
            else
            {
                std::cout << "error: command not recognized" << std::endl;
            }
        }
        else if (action == "print")
        {
            std::string object;
            std::string extra;

            if ((iss >> object) && !(iss >> extra))
            {
                if (object == "mmu")
                {
                    mmu->print();
                }
                else if (object == "page")
                {
                    page_table->print();
                }
                else if (object == "processes")
                {
                    std::vector<uint32_t> pids = mmu->getPids();
                    for (uint32_t pid : pids)
                    {
                        std::cout << pid << std::endl;
                    }
                }
                else
                {
                    size_t colon = object.find(':');
                    if (colon == std::string::npos)
                    {
                        std::cout << "error: command not recognized" << std::endl;
                    }
                    else
                    {
                        try
                        {
                            uint32_t pid = static_cast<uint32_t>(std::stoul(object.substr(0, colon)));
                            std::string var_name = object.substr(colon + 1);
                            printVariableValue(pid, var_name, mmu, page_table, memory);
                        }
                        catch (...)
                        {
                            std::cout << "error: command not recognized" << std::endl;
                        }
                    }
                }
            }
            else
            {
                std::cout << "error: command not recognized" << std::endl;
            }
        }
        else if (!action.empty())
        {
            std::cout << "error: command not recognized" << std::endl;
        }

        std::cout << "> ";
        std::getline(std::cin, command);
    }

    delete[] memory;
    delete mmu;
    delete page_table;

    return 0;
}

void printStartMessage(int page_size)
{
    std::cout << "Welcome to the Memory Allocation Simulator! Using a page size of " << page_size << " bytes." << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  * create <text_size> <data_size> (initializes a new process)" << std::endl;
    std::cout << "  * allocate <PID> <var_name> <data_type> <number_of_elements> (allocated memory on the heap)" << std::endl;
    std::cout << "  * set <PID> <var_name> <offset> <value_0> <value_1> <value_2> ... <value_N> (set the value for a variable)" << std::endl;
    std::cout << "  * free <PID> <var_name> (deallocate memory on the heap that is associated with <var_name>)" << std::endl;
    std::cout << "  * terminate <PID> (kill the specified process)" << std::endl;
    std::cout << "  * print <object> (prints data)" << std::endl;
    std::cout << "    * If <object> is \"mmu\", print the MMU memory table" << std::endl;
    std::cout << "    * if <object> is \"page\", print the page table" << std::endl;
    std::cout << "    * if <object> is \"processes\", print a list of PIDs for processes that are still running" << std::endl;
    std::cout << "    * if <object> is a \"<PID>:<var_name>\", print the value of the variable for that process" << std::endl;
    std::cout << std::endl;
}

void createProcess(int text_size, int data_size, Mmu *mmu, PageTable *page_table)
{
    if (text_size < 2048 || text_size > 16384 || data_size < 0 || data_size > 1024)
    {
        std::cout << "error: command not recognized" << std::endl;
        return;
    }

    uint32_t text = static_cast<uint32_t>(text_size);
    uint32_t data = static_cast<uint32_t>(data_size);
    uint32_t stack = STACK_SIZE;

    std::set<int> needed_pages;
    std::set<int> p1 = pagesTouched(0, text, page_table->getPageSize());
    std::set<int> p2 = pagesTouched(text, data, page_table->getPageSize());
    std::set<int> p3 = pagesTouched(text + data, stack, page_table->getPageSize());
    needed_pages.insert(p1.begin(), p1.end());
    needed_pages.insert(p2.begin(), p2.end());
    needed_pages.insert(p3.begin(), p3.end());

    if (static_cast<int>(needed_pages.size()) > page_table->getFreeFrameCount())
    {
        std::cout << "error: allocation exceeds physical memory" << std::endl;
        return;
    }

    uint32_t pid = mmu->createProcess();

    reserveAtAddress(pid, "<TEXT>", DataType::Char, text, 0, mmu);
    reserveAtAddress(pid, "<GLOBALS>", DataType::Char, data, text, mmu);
    reserveAtAddress(pid, "<STACK>", DataType::Char, stack, text + data, mmu);

    addPagesForRange(pid, 0, text, page_table);
    addPagesForRange(pid, text, data, page_table);
    addPagesForRange(pid, text + data, stack, page_table);

    std::cout << pid << std::endl;
}

void allocateVariable(uint32_t pid, std::string var_name, DataType type, uint32_t num_elements, Mmu *mmu, PageTable *page_table)
{
    uint32_t type_size = dataTypeSize(type);
    uint64_t total_size_64 = static_cast<uint64_t>(type_size) * num_elements;

    if (type_size == 0 || num_elements == 0 || total_size_64 > std::numeric_limits<uint32_t>::max())
    {
        std::cout << "error: allocation exceeds physical memory" << std::endl;
        return;
    }

    uint32_t total_size = static_cast<uint32_t>(total_size_64);
    uint32_t address = 0;

    if (!findFirstFitAddress(pid, total_size, mmu, page_table, address))
    {
        std::cout << "error: allocation exceeds physical memory" << std::endl;
        return;
    }

    int new_pages = countNewPages(pid, address, total_size, page_table);
    if (new_pages > page_table->getFreeFrameCount())
    {
        std::cout << "error: allocation exceeds physical memory" << std::endl;
        return;
    }

    if (!reserveAtAddress(pid, var_name, type, total_size, address, mmu))
    {
        std::cout << "error: allocation exceeds physical memory" << std::endl;
        return;
    }

    addPagesForRange(pid, address, total_size, page_table);
    std::cout << address << std::endl;
}

void setVariable(uint32_t pid, std::string var_name, uint32_t offset, void *value, Mmu *mmu, PageTable *page_table, uint8_t *memory)
{
    Variable* var = mmu->getVariable(pid, var_name);
    if (var == nullptr)
    {
        return;
    }

    uint32_t type_size = dataTypeSize(var->type);
    if (type_size == 0)
    {
        return;
    }

    uint32_t address = var->virtual_address + offset * type_size;
    writeBytes(pid, address, reinterpret_cast<uint8_t*>(value), type_size, page_table, memory);
}

void freeVariable(uint32_t pid, std::string var_name, Mmu *mmu, PageTable *page_table)
{
    Process* proc = mmu->getProcess(pid);
    Variable* var = mmu->getVariable(pid, var_name);

    if (proc == nullptr || var == nullptr)
    {
        return;
    }

    uint32_t address = var->virtual_address;
    uint32_t size = var->size;
    std::set<int> touched_pages = pagesTouched(address, size, page_table->getPageSize());

    mmu->removeVariableFromProcess(pid, var_name);
    mmu->addVariableToProcess(pid, "<FREE_SPACE>", DataType::FreeSpace, size, address);
    coalesceFreeSpace(proc);

    for (int page : touched_pages)
    {
        if (!pageHasAllocatedVariable(proc, page, page_table->getPageSize()))
        {
            page_table->removeEntry(pid, page);
        }
    }
}

void terminateProcess(uint32_t pid, Mmu *mmu, PageTable *page_table)
{
    page_table->removeProcess(pid);
    mmu->removeProcess(pid);
}
