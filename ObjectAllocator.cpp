/**
 * @file ObjectAllocator.cpp
 * @author Adam Lonstein (adam.lonstein@digipen.com)
 * @brief This is a custom memory manager that can allocate and free memory blocks. It also can use padding bytes, header blocks, and more.
 * @date 01-20-2024
 */

#include "ObjectAllocator.h"
#include <cstring>

#include <iostream>

#define UNUSED(x) (void)(x)

ObjectAllocator::ObjectAllocator(size_t ObjectSize, const OAConfig& config)
{
    PageList_ = nullptr;
    FreeList_ = nullptr;

    // Initialize config
    this->config = config;

    stats.ObjectSize_ = ObjectSize;
    stats.PageSize_ = ObjectSize * config.ObjectsPerPage_ + sizeof(GenericObject*); // Set the page size

    stats.FreeObjects_ = 0;

    AllocatePage();
}

ObjectAllocator::~ObjectAllocator()
{
    
}

void* ObjectAllocator::Allocate(const char *label)
{
    UNUSED(label);

    // If we are out of free objects
    if(stats.FreeObjects_ <= 0)
    {
        // If we have another available page
        if(stats.PagesInUse_ < config.MaxPages_)
        {
            // Allocate another page
            AllocatePage();
        }
        else
        {
            // Otherwise, we are out of memory so throw an exception
            throw OAException(OAException::E_NO_PAGES, "Allocate: memory manager out of logical memory (max pages has been reached)");

            return nullptr;
        }
    }

    GenericObject* availableBlock = FreeList_;

    FreeList_ = FreeList_->Next;

    memset(availableBlock, ALLOCATED_PATTERN, stats.ObjectSize_);

    stats.FreeObjects_--;
    stats.Allocations_++;
    stats.ObjectsInUse_++;

    if(stats.Allocations_ > stats.MostObjects_)
    {
        stats.MostObjects_ = stats.Allocations_;
    }

    return availableBlock;
}

void ObjectAllocator::Free(void *Object)
{
    GenericObject* freedObject = reinterpret_cast<GenericObject*>(Object);

    if(config.DebugOn_)
    {
        // If the client is trying to double free
        if(IsObjectInList(FreeList_, freedObject))
        {
            // Throw a double free exception
            throw OAException(OAException::E_MULTIPLE_FREE, "FreeObject: Object has already been freed.");
        }

        // Get the location of where the first block would be
        uintptr_t firstBlockLocation = reinterpret_cast<uintptr_t>(PageList_) + sizeof(GenericObject*);

        // If the client is trying to free an invalid pointer (a pointer not on a boundary)
        if((reinterpret_cast<uintptr_t>(freedObject) - firstBlockLocation) % stats.ObjectSize_ != 0)
        {
            // Throw a bad boundary exception
            throw OAException(OAException::E_BAD_BOUNDARY, "validate_object: Object not on a boundary.");
        }
    }

    memset(freedObject, FREED_PATTERN, stats.ObjectSize_);

    PushFront(&FreeList_, freedObject);

    stats.FreeObjects_++;
    stats.Deallocations_++;
    stats.ObjectsInUse_--;
}

unsigned ObjectAllocator::DumpMemoryInUse(DUMPCALLBACK fn) const
{
    UNUSED(fn);
    return 0;
}

unsigned ObjectAllocator::ValidatePages(VALIDATECALLBACK fn) const
{
    UNUSED(fn);
    return 0;
}

unsigned ObjectAllocator::FreeEmptyPages()
{
    return 0;
}

bool ObjectAllocator::ImplementedExtraCredit()
{
    return false;
}

void ObjectAllocator::SetDebugState(bool State)
{
    config.DebugOn_ = State;
}

const void* ObjectAllocator::GetFreeList() const
{
    return FreeList_;
}

const void* ObjectAllocator::GetPageList() const
{
    return PageList_;
}

OAConfig ObjectAllocator::GetConfig() const
{
    return config;
}

OAStats ObjectAllocator::GetStats() const
{
    return stats;
}

// ---------- Private methods -------------

void ObjectAllocator::PushFront(GenericObject** head, GenericObject* newNode)
{
    if((*head) == nullptr)
    {
        newNode->Next = nullptr;
        (*head) = newNode;
    }
    else
    {
        newNode->Next = (*head);

        (*head) = newNode;
    }
}

void ObjectAllocator::AllocatePage()
{
    GenericObject* newPage;

    try
    {
        newPage = reinterpret_cast<GenericObject*>( new char[stats.PageSize_] );
    }
    catch(const std::bad_alloc& e)
    {
        throw OAException(OAException::E_NO_MEMORY, "allocate_new_page: No system memory available.");
    }

    PushFront(&PageList_, newPage);

    stats.PagesInUse_++;
    stats.FreeObjects_ += config.ObjectsPerPage_;

    // Set the first block one PageList forward
    GenericObject* block = reinterpret_cast<GenericObject*>( reinterpret_cast<uintptr_t>(PageList_) + sizeof(GenericObject*) );

    memset(block, UNALLOCATED_PATTERN, stats.ObjectSize_);

    FreeList_ = block;
    FreeList_->Next = nullptr;

    for(unsigned int i = 0; i < config.ObjectsPerPage_ - 1; ++i)
    {
        block = reinterpret_cast<GenericObject*>( reinterpret_cast<uintptr_t>(block) + stats.ObjectSize_);

        memset(block, UNALLOCATED_PATTERN, stats.ObjectSize_);

        PushFront(&FreeList_, block);
    }

}

void ObjectAllocator::PrintList(GenericObject* list)
{
    std::cout << std::endl;

    if(list == nullptr)
    {
        std::cout << "List is null" << std::endl << std::endl;

        return;
    }

    while(list != nullptr)
    {
        std::cout << list << std::endl;

        list = list->Next;
    }

    std::cout << std::endl;
}

bool ObjectAllocator::IsObjectInList(GenericObject* list, GenericObject* object)
{
    while(list != nullptr)
    {
        if(list == object)
        {
            return true;
        }

        list = list->Next;
    }

    return false;
}