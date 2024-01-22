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

    FullBlockSize = ObjectSize + config.PadBytes_ * 2 + config.HBlockInfo_.size_;
    stats.PageSize_ = FullBlockSize * config.ObjectsPerPage_ + sizeof(GenericObject*); // Set the page size

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
    char* freedObject = reinterpret_cast<char*>(Object);

    if(config.DebugOn_)
    {
        // If the client is trying to double free
        if(IsObjectInList(FreeList_, freedObject))
        {
            // Throw a double free exception
            throw OAException(OAException::E_MULTIPLE_FREE, "FreeObject: Object has already been freed.");
        }

        // Get the location of where the first block would be
        char* firstBlockLocation = ObjectPageLocation(freedObject) + config.PadBytes_ + sizeof(GenericObject*);

        // If the client is trying to free an invalid pointer (a pointer not on a boundary)
        if((freedObject - firstBlockLocation) % FullBlockSize != 0)
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

void ObjectAllocator::PushFront(GenericObject** head, char* newNode)
{
    GenericObject* node = reinterpret_cast<GenericObject*>(newNode);

    if((*head) == nullptr)
    {
        node->Next = nullptr;
        (*head) = node;
    }
    else
    {
        node->Next = (*head);

        (*head) = node;
    }
}

void ObjectAllocator::AllocatePage()
{
    char* newPage;

    try
    {
        newPage = new char[stats.PageSize_];
    }
    catch(const std::bad_alloc& e)
    {
        throw OAException(OAException::E_NO_MEMORY, "allocate_new_page: No system memory available.");
    }

    // Add pad bytes for the start of the first block
    char* paddingLocation = newPage + sizeof(GenericObject*);
    memset(paddingLocation, PAD_PATTERN, config.PadBytes_);

    PushFront(&PageList_, newPage);

    stats.PagesInUse_++;
    stats.FreeObjects_ += config.ObjectsPerPage_;

    // Set the first block one PageList forward
    char* block = reinterpret_cast<char*>(PageList_) + config.PadBytes_ + sizeof(GenericObject*);

    //paddingLocation = reinterpret_cast<GenericObject*>(reinterpret_cast<uintptr_t>(block) + sizeof(GenericObject*));

    // Set the unallocated pattern the first block
    memset(block, UNALLOCATED_PATTERN, stats.ObjectSize_);

    // Set the pad pattern for the end of the first block
    paddingLocation = block + stats.ObjectSize_;
    memset(paddingLocation, PAD_PATTERN, config.PadBytes_);

    FreeList_ = reinterpret_cast<GenericObject*>(block);
    FreeList_->Next = nullptr;

    // diagrams example 5 for padding
    // should draw out what function is doing step by step
    // we have the padding working for the start and end of the first data
    // now just put padding for all the data in the for loop

    for(unsigned int i = 0; i < config.ObjectsPerPage_ - 1; ++i)
    {
        // Set padding at the beginning of the data block
        paddingLocation = paddingLocation + config.PadBytes_;
        memset(paddingLocation, PAD_PATTERN, config.PadBytes_);

        // Set the data block
        block = paddingLocation + config.PadBytes_;
        memset(block, UNALLOCATED_PATTERN, stats.ObjectSize_);

        // Set padding at the end of the data block
        paddingLocation = block + stats.ObjectSize_;
        memset(paddingLocation, PAD_PATTERN, config.PadBytes_);

        // Add the data block to the free list
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

bool ObjectAllocator::IsObjectInList(GenericObject* list, char* object)
{
    while(list != nullptr)
    {
        if(reinterpret_cast<char*>(list) == object)
        {
            return true;
        }

        list = list->Next;
    }

    return false;
}

/**
 * @brief Walks the page list and returns the address of which page contains the given Object
 * 
 * @param Object - the object to find the page of
 * @return char** 
 */
char* ObjectAllocator::ObjectPageLocation(char* Object)
{
    GenericObject* walker = PageList_;

    while(walker != nullptr)
    {
        char* page = reinterpret_cast<char*>(walker);

        if(Object >= page && Object <= page + stats.PageSize_)
        {
            return page;
        }

        walker = walker->Next;
    }

    return nullptr;
}