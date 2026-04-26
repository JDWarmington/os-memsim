#include <algorithm>
#include <iomanip>
#include <iostream>
#include "../include/mmu.h"

Mmu::Mmu(int memory_size)
{
    _next_pid = 1024;
    _max_size = static_cast<uint32_t>(memory_size);
}

Mmu::~Mmu()
{
    for (Process* proc : _processes)
    {
        if (proc != nullptr)
        {
            for (Variable* var : proc->variables)
            {
                delete var;
            }
            delete proc;
        }
    }
}

uint32_t Mmu::createProcess()
{
    Process *proc = new Process();
    proc->pid = _next_pid;

    Variable *var = new Variable();
    var->name = "<FREE_SPACE>";
    var->type = DataType::FreeSpace;
    var->virtual_address = 0;
    var->size = _max_size;
    proc->variables.push_back(var);

    _processes.push_back(proc);

    _next_pid++;
    
    return proc->pid;
}

void Mmu::addVariableToProcess(uint32_t pid, std::string var_name, DataType type, uint32_t size, uint32_t address)
{
    Process *proc = getProcess(pid);
    if (proc != nullptr)
    {
        Variable *var = new Variable();
        var->name = var_name;
        var->type = type;
        var->virtual_address = address;
        var->size = size;
        proc->variables.push_back(var);
    }
}

bool Mmu::processExists(uint32_t pid) const
{
    return getProcess(pid) != nullptr;
}

bool Mmu::variableExists(uint32_t pid, const std::string& var_name) const
{
    return getVariable(pid, var_name) != nullptr;
}

Process* Mmu::getProcess(uint32_t pid) const
{
    std::vector<Process*>::const_iterator it = std::find_if(_processes.begin(), _processes.end(), [pid](Process* p)
    {
        return p != nullptr && p->pid == pid;
    });

    if (it == _processes.end())
    {
        return nullptr;
    }

    return *it;
}

Variable* Mmu::getVariable(uint32_t pid, const std::string& var_name) const
{
    Process* proc = getProcess(pid);
    if (proc == nullptr)
    {
        return nullptr;
    }

    std::vector<Variable*>::const_iterator it = std::find_if(proc->variables.begin(), proc->variables.end(), [&var_name](Variable* v)
    {
        return v != nullptr && v->type != DataType::FreeSpace && v->name == var_name;
    });

    if (it == proc->variables.end())
    {
        return nullptr;
    }

    return *it;
}

void Mmu::removeVariableFromProcess(uint32_t pid, const std::string& var_name)
{
    Process* proc = getProcess(pid);
    if (proc == nullptr)
    {
        return;
    }

    for (std::vector<Variable*>::iterator it = proc->variables.begin(); it != proc->variables.end(); ++it)
    {
        if (*it != nullptr && (*it)->type != DataType::FreeSpace && (*it)->name == var_name)
        {
            delete *it;
            proc->variables.erase(it);
            return;
        }
    }
}

void Mmu::removeProcess(uint32_t pid)
{
    for (std::vector<Process*>::iterator it = _processes.begin(); it != _processes.end(); ++it)
    {
        if (*it != nullptr && (*it)->pid == pid)
        {
            for (Variable* var : (*it)->variables)
            {
                delete var;
            }
            delete *it;
            _processes.erase(it);
            return;
        }
    }
}

std::vector<uint32_t> Mmu::getPids() const
{
    std::vector<uint32_t> pids;
    for (Process* proc : _processes)
    {
        if (proc != nullptr)
        {
            pids.push_back(proc->pid);
        }
    }

    std::sort(pids.begin(), pids.end());
    return pids;
}

void Mmu::print()
{
    std::cout << " PID  | Variable Name | Virtual Addr | Size" << std::endl;
    std::cout << "------+---------------+--------------+------------" << std::endl;

    std::vector<Process*> processes = _processes;
    std::sort(processes.begin(), processes.end(), [](Process* a, Process* b)
    {
        return a->pid < b->pid;
    });

    for (Process* proc : processes)
    {
        std::vector<Variable*> variables = proc->variables;
        std::sort(variables.begin(), variables.end(), [](Variable* a, Variable* b)
        {
            if (a->virtual_address != b->virtual_address)
            {
                return a->virtual_address < b->virtual_address;
            }
            return a->name < b->name;
        });

        for (Variable* var : variables)
        {
            if (var == nullptr || var->type == DataType::FreeSpace)
            {
                continue;
            }

            std::cout << " " << std::setw(4) << proc->pid << " | "
                      << std::left << std::setw(13) << var->name << std::right << " | "
                      << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << var->virtual_address
                      << std::dec << std::nouppercase << std::setfill(' ') << " | "
                      << std::setw(10) << var->size << std::endl;
        }
    }
}
