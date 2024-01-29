/**
 * @file ObjectAllocator.cpp
 * @author Adam Lonstein (adam.lonstein@digipen.com)
 * @brief This is a custom memory manager that can allocate and free memory blocks. It also can use 
 *        padding bytes, header blocks, check for double frees and corruption, and more. The Object 
 *        Allocator (OA) works by creating pages which can hold a certain amount of objects with equal 
 *        size. When these objects are free, they will be added to the free list. When allocated, they 
 *        get removed from the free list and are sent to the client.
 * @date 01-20-2024
 */

#include "ObjectAllocator.h"
#include <cstring>

/**
 * @brief Creates and sets the configurations and stats for the object allocator. Allocates the first page.
 * 
 * @param ObjectSize - the size each object in the allocator will be
 * @param config - the configuration settings (objects per page, pad bytes, etc.)
 */
ObjectAllocator::ObjectAllocator(size_t ObjectSize, const OAConfig& config)
{
    PageList_ = nullptr;
    FreeList_ = nullptr;

    // Initialize config
    this->config = config;

    stats.ObjectSize_ = ObjectSize;

    // Calculate the size of each block, including the object, the header block, and the 2 pad blocks
    FullBlockSize = ObjectSize + config.PadBytes_ * 2 + config.HBlockInfo_.size_;
    stats.PageSize_ = FullBlockSize * config.ObjectsPerPage_ + sizeof(GenericObject*); // Set the page size

    // If it's using the CPP manager, return so the first page doesn't get created
    if(config.UseCPPMemManager_)
        return;

    stats.FreeObjects_ = 0;

    // Allocate the first page of the OA
    AllocatePage();
}

/**
 * @brief Destroy the object allocator. If the header block is external, walk through each object and 
 *        delete the allocated block. Delete each page.
 */
ObjectAllocator::~ObjectAllocator()
{
    // Delete all the allocated external header blocks
    if(config.HBlockInfo_.type_ == OAConfig::hbExternal)
    {
        // This will walk the pages
        GenericObject* pageWalker = PageList_;

        // This will go along the pages block to block
        char* block = reinterpret_cast<char*>(PageList_);

        block += sizeof(GenericObject*) + config.HBlockInfo_.size_ + config.PadBytes_;

        // Get the header block location
        MemBlockInfo **externalHeaderBlock = reinterpret_cast<MemBlockInfo**>(block - config.PadBytes_ - config.HBlockInfo_.size_);

        // As long as we still have pages
        while(pageWalker != nullptr)
        {
            // As long as we are still in the page
            while(block < reinterpret_cast<char*>(pageWalker) + stats.PageSize_)
            {
                if(IsObjectInList(FreeList_, block))
                {
                    // If the object is in the free list, it is not allocated so just skip over it
                    block += stats.ObjectSize_ + config.HBlockInfo_.size_ + config.PadBytes_ * 2;

                    continue;
                }

                // Free the label
                if((*externalHeaderBlock)->label != nullptr)
                {
                    delete [] (*externalHeaderBlock)->label;

                    (*externalHeaderBlock)->label = nullptr;
                }
                
                // Free the structure
                delete (*externalHeaderBlock);

                (*externalHeaderBlock) = nullptr;

                // Go to the next block
                block += stats.ObjectSize_ + config.HBlockInfo_.size_ + config.PadBytes_ * 2;
                // Get the header block location
                externalHeaderBlock = reinterpret_cast<MemBlockInfo**>(block - config.PadBytes_ - config.HBlockInfo_.size_);
            }

            // Go to the next page
            pageWalker = pageWalker->Next;

            block = reinterpret_cast<char*>(pageWalker);
            // Go to the first block in the page
            block += sizeof(GenericObject*) + config.HBlockInfo_.size_ + config.PadBytes_;
            // Get the header block location
            externalHeaderBlock = reinterpret_cast<MemBlockInfo**>(block - config.PadBytes_ - config.HBlockInfo_.size_);
        }
    }

    // Delete each page
    while (PageList_)
    {
        GenericObject *temp = PageList_->Next;
        delete [] reinterpret_cast<char *>(PageList_);
        PageList_ = temp;
    }
}

/**
 * @brief Allocates a block in the memory manager.
 * 
 * @param label - the label to assign an external block
 * @return void* - The allocated block
 */
void* ObjectAllocator::Allocate(const char *label)
{
    if(config.UseCPPMemManager_)
    {
        return AllocateWithCPPManager();
    }

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

    // Update the stats
    stats.FreeObjects_--;
    stats.Allocations_++;
    stats.ObjectsInUse_++;

    // Get an available block (first on the free list)
    char* availableBlock = reinterpret_cast<char*>( FreeList_ );
    AssignHeaderBlockValues(availableBlock, true, label);

    // Move to the next object in the free list
    FreeList_ = FreeList_->Next;

    // Set the memory to the allocated pattern
    if(config.DebugOn_)
        memset(availableBlock, ALLOCATED_PATTERN, stats.ObjectSize_); 

    // Update the most objects statistic
    if(stats.Allocations_ > stats.MostObjects_)
    {
        stats.MostObjects_ = stats.Allocations_;
    }

    return availableBlock;
}

/**
 * @brief Frees a block from the memory manager (adds it back to the free list).
 * 
 * @param Object - the object to free
 */
void ObjectAllocator::Free(void *Object)
{
    char* freedObject = reinterpret_cast<char*>(Object);

    if(config.UseCPPMemManager_)
    {
        // Delete with the CPP manager if it's on
        stats.Deallocations_++;
        stats.ObjectsInUse_--;

        delete [] freedObject;

        return;
    }

    // Checks for exceptions if debug is on
    if(config.DebugOn_)
    {
        // If the client is trying to double free
        if(IsObjectInList(FreeList_, freedObject))
        {
            // Throw a double free exception
            throw OAException(OAException::E_MULTIPLE_FREE, "FreeObject: Object has already been freed.");
        }

        CheckForBadBoundary(freedObject);
        
        if(config.PadBytes_ > 0)
        {
            // Get the location of the block (the comparison with PAD_PATTERN only works if this is also an unsigned char*)
            const unsigned char* block = static_cast<const unsigned char*>( Object );
            if(CheckForPaddingCorruption(block))
            {
                throw OAException(OAException::E_CORRUPTED_BLOCK, "FreeObject: Object block has been corrupted.");
            }
        }
    }

    // Reset the header block values
    AssignHeaderBlockValues(freedObject, false);

    // Set the memory back to the freed pattern
    if(config.DebugOn_)
        memset(freedObject, FREED_PATTERN, stats.ObjectSize_);

    // Put the block back on the free list
    PushFront(&FreeList_, freedObject);

    // Update the stats
    stats.FreeObjects_++;
    stats.Deallocations_++;
    stats.ObjectsInUse_--;
}

/**
 * @brief Calls the callback fn for each block in use by the client.
 * 
 * @param fn - callback
 * @return The amount of objects in use.
 */
unsigned ObjectAllocator::DumpMemoryInUse(DUMPCALLBACK fn) const
{
    // This will walk the pages
    GenericObject* pageWalker = PageList_;

    // This will go along the pages block to block
    char* allocatedBlock = reinterpret_cast<char*>(PageList_);

    // Go to the first block
    allocatedBlock += sizeof(GenericObject*) + config.HBlockInfo_.size_ + config.PadBytes_;

    // As long as we still have pages
    while(pageWalker != nullptr)
    {
        // As long as we are still in the page
        while(allocatedBlock < reinterpret_cast<char*>(pageWalker) + stats.PageSize_)
        {
            if(IsObjectInList(FreeList_, allocatedBlock))
            {
                // If the object is in the free list, it is not allocated so just skip over it
                allocatedBlock += stats.ObjectSize_ + config.HBlockInfo_.size_ + config.PadBytes_ * 2;

                continue;
            }

            // Otherwise, call the callback for the block
            fn(allocatedBlock, stats.ObjectSize_);

            // Go to the next block
            allocatedBlock += stats.ObjectSize_ + config.HBlockInfo_.size_ + config.PadBytes_ * 2;

        }

        // Go to the next page
        pageWalker = pageWalker->Next;

        // Set the block to the first block in the new page
        allocatedBlock = reinterpret_cast<char*>(pageWalker);
        allocatedBlock += sizeof(GenericObject*) + config.HBlockInfo_.size_ + config.PadBytes_;
    }

    return stats.ObjectsInUse_;
}

/**
 * @brief Calls the callback fn for each block that is potentially corrupted.
 * 
 * @param fn - callback
 * @return unsigned - Number of corrupted blocks
 */
unsigned ObjectAllocator::ValidatePages(VALIDATECALLBACK fn) const
{
    int numCorruptions = 0;

    // This will walk the pages
    GenericObject* pageWalker = PageList_;

    // This will go along the pages block to block
    const unsigned char* block = reinterpret_cast<const unsigned char*>(PageList_);

    block += sizeof(GenericObject*) + config.HBlockInfo_.size_ + config.PadBytes_;

    // As long as we still have pages
    while(pageWalker != nullptr)
    {
        // As long as we are still in the page
        while(block < reinterpret_cast<unsigned char*>(pageWalker) + stats.PageSize_)
        {
            // If the block is corrupted, call the callback
            if(CheckForPaddingCorruption(block))
            {
                numCorruptions++;

                fn(block, stats.ObjectSize_);
            }

            // Go to the next block
            block += stats.ObjectSize_ + config.HBlockInfo_.size_ + config.PadBytes_ * 2;
        }

        // Go to the next page
        pageWalker = pageWalker->Next;

        // Go to the first block in the next page
        block = reinterpret_cast<unsigned char*>(pageWalker);
        block += sizeof(GenericObject*) + config.HBlockInfo_.size_ + config.PadBytes_;
    }

    return numCorruptions;
}

/**
 * @brief Did not implement.
 * 
 * @return unsigned 
 */
unsigned ObjectAllocator::FreeEmptyPages()
{
    return 0;
}

/**
 * @brief Did not implement.
 * 
 * @return true 
 * @return false 
 */
bool ObjectAllocator::ImplementedExtraCredit()
{
    return false;
}

/**
 * @brief Sets the debug state of the OA.
 * 
 * @param State 
 */
void ObjectAllocator::SetDebugState(bool State)
{
    config.DebugOn_ = State;
}

/**
 * @brief Returns the free list (all blocks that are free).
 */
const void* ObjectAllocator::GetFreeList() const
{
    return FreeList_;
}

/**
 * @brief Returns the page list.
 */
const void* ObjectAllocator::GetPageList() const
{
    return PageList_;
}

/**
 * @brief Returns the configurations of the OA.
 */
OAConfig ObjectAllocator::GetConfig() const
{
    return config;
}

/**
 * @brief Returns the statistics of the OA.
 */
OAStats ObjectAllocator::GetStats() const
{
    return stats;
}

// ---------- Private methods -------------

/**
 * @brief Pushes a node to a list.
 * 
 * @param head - head of the list to push to
 * @param newNode - the node to push
 */
void ObjectAllocator::PushFront(GenericObject** head, char* newNode)
{
    // Turn the node to a GenericObject
    GenericObject* node = reinterpret_cast<GenericObject*>(newNode);

    // Push the node to the front
    node->Next = (*head);
    (*head) = node;
}

/**
 * @brief Allocates a page and adds it to the page list.
 */
void ObjectAllocator::AllocatePage()
{
    char* newPage; // Will be the newly allocated page

    try
    {
        newPage = new char[stats.PageSize_];
    }
    catch(const std::bad_alloc& e)
    {
        throw OAException(OAException::E_NO_MEMORY, "allocate_new_page: No system memory available.");
    }

    // Location of the header block
    char* hbLocation = newPage + sizeof(GenericObject*);
    // Set header block values to 0
    memset(hbLocation, 0, config.HBlockInfo_.size_);

    char* paddingLocation = newPage + config.HBlockInfo_.size_ + sizeof(GenericObject*);
    if(config.DebugOn_)
    {
        // Set pad pattern for the start of the first block
        memset(paddingLocation, PAD_PATTERN, config.PadBytes_);
    }

    // Push the newly allocated page to the page list
    PushFront(&PageList_, newPage);

    stats.PagesInUse_++;
    stats.FreeObjects_ += config.ObjectsPerPage_;

    // Set the first block one PageList forward
    char* block = paddingLocation + config.PadBytes_;
    if(config.DebugOn_)
    {
        // Set the unallocated pattern the first block
        memset(block, UNALLOCATED_PATTERN, stats.ObjectSize_);
    }

    paddingLocation = block + stats.ObjectSize_;
    if(config.DebugOn_)
    {
        // Set pad pattern for the end of the first block
        memset(paddingLocation, PAD_PATTERN, config.PadBytes_);
    }

    // Set the free list to the first block
    FreeList_ = reinterpret_cast<GenericObject*>(block);
    FreeList_->Next = nullptr;

    for(unsigned int i = 0; i < config.ObjectsPerPage_ - 1; ++i)
    {
        // Location of the header block
        char* hbLocation = paddingLocation + config.PadBytes_;
        // Set header block values to 0
        memset(hbLocation, 0, config.HBlockInfo_.size_);

        paddingLocation = paddingLocation + config.PadBytes_ + config.HBlockInfo_.size_;
        if(config.DebugOn_)
        {
            // Set padding pattern for the beginning of the data block
            memset(paddingLocation, PAD_PATTERN, config.PadBytes_);
        }

        block = paddingLocation + config.PadBytes_;
        if(config.DebugOn_)
        {
            // Set the pattern for the free data block
            memset(block, UNALLOCATED_PATTERN, stats.ObjectSize_);
        }

        paddingLocation = block + stats.ObjectSize_;
        if(config.DebugOn_)
        {
            // Set padding pattern for the end of the data block
            memset(paddingLocation, PAD_PATTERN, config.PadBytes_);
        }

        // Add the data block to the free list
        PushFront(&FreeList_, block);
    }
}

/**
 * @brief Walks through the given list and check if one of its nodes is the given object.
 * 
 * @param list - the list to walk.
 * @param object - the object to compare for.
 * @return - whether it found the object.
 */
bool ObjectAllocator::IsObjectInList(GenericObject* list, char* object) const
{
    // Walk the list
    while(list != nullptr)
    {
        // If it finds the object, return true
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

/**
 * @brief Assigns the values to the header block of the given object depending on the header block type.
 * 
 * @param object - the object to assign the header block values to
 * @param alloc - if true, it's allocating so will set/increase values. if false, it's freeing so will clear all values.
 * @param label - the label of the header block. this will only have a value if it's an external header block.
 */
void ObjectAllocator::AssignHeaderBlockValues(char* object, bool alloc, const char* label)
{
    if(config.HBlockInfo_.type_ == config.hbNone)
        return;

    if(config.HBlockInfo_.type_ == config.hbBasic || config.HBlockInfo_.type_ == config.hbExtended)
    {
        // Get the location of the header block flag byte
        char* flag = object - config.PadBytes_ - 1;
        // Set the flag if allocating, clear it if freeing
        alloc ? (*flag) |= 1 : (*flag) &= ~1;

        // Get the location of the header block 4-byte alloc#
        char* allocNum = flag - 4;
        // Set the 4-byte alloc# if allocating, clear it if freeing
        (*reinterpret_cast<unsigned int*>(allocNum)) = alloc ? stats.Allocations_ : 0;
        
        if(config.HBlockInfo_.type_ == config.hbExtended)
        {
            // Get the location of the extended header block 2-byte reuse number
            char* reuseNum = allocNum - 2;

            // Increase the reuse number if allocating
            if(alloc)
                (*reinterpret_cast<unsigned short*>(reuseNum))++;
        }
    }
    else if(config.HBlockInfo_.type_ == config.hbExternal)
    {
        // Get the location of the external header block structure
        MemBlockInfo **externalHeaderBlock = reinterpret_cast<MemBlockInfo**>(object - config.PadBytes_ - config.HBlockInfo_.size_);

        // Allocate or free the header block
        if(alloc)
        {
            AllocateExternalHeaderBlock(externalHeaderBlock, label);
        }
        else
        {
            FreeExternalHeaderBlock(externalHeaderBlock);
        }
    }
}

/**
 * @brief Allocates the external header.
 * 
 * @param externalHeaderBlock - the location where the external header should be
 * @param label - the label for the external header
 */
void ObjectAllocator::AllocateExternalHeaderBlock(MemBlockInfo** externalHeaderBlock, const char* label)
{
    try
    {
        // Allocate the external header block
        (*externalHeaderBlock) = new MemBlockInfo;
    }
    catch(const std::bad_alloc& e)
    {
        throw OAException(OAException::E_NO_MEMORY, "assign_header_block: No system memory available.");
    }

    if(label != nullptr)
    {
        try
        {
            // Allocate memory for the label
            (*externalHeaderBlock)->label = new char[strlen(label) + 1];
        }
        catch(const std::exception& e)
        {
            throw OAException(OAException::E_NO_MEMORY, "assign_header_block: No system memory available.");
        }
        
        // Copy the label
        strncpy((*externalHeaderBlock)->label, label, strlen(label) + 1);
    }
    else
    {
        // Set the label to null otherwise it doesn't clean up the label
        (*externalHeaderBlock)->label = nullptr;
    }

    // Set the header block values
    (*externalHeaderBlock)->alloc_num = stats.Allocations_;
    (*externalHeaderBlock)->in_use = true;
}

/**
 * @brief Frees the external header block.
 * 
 * @param externalHeaderBlock - the location where the external header should be
 */
void ObjectAllocator::FreeExternalHeaderBlock(MemBlockInfo** externalHeaderBlock)
{
    // Free the label
    if((*externalHeaderBlock)->label != nullptr)
    {
        delete [] (*externalHeaderBlock)->label;

        (*externalHeaderBlock)->label = nullptr;
    }
    
    // Free the header block
    delete (*externalHeaderBlock);

    (*externalHeaderBlock) = nullptr;
}

/**
 * @brief Checks if the given block is on a bad boundary. For example, if a block of memory starts at
 *        0x04 and the client is trying to free 0x05.
 * 
 * @param block 
 */
void ObjectAllocator::CheckForBadBoundary(char* const block)
{
    // Get the location of where the first block would be
    char* firstBlockLocation = ObjectPageLocation(block) + config.HBlockInfo_.size_ + config.PadBytes_ + sizeof(GenericObject*);

    // If the client is trying to free an invalid pointer (a pointer not on a boundary)
    if((block - firstBlockLocation) % FullBlockSize != 0)
    {
        // Throw a bad boundary exception
        throw OAException(OAException::E_BAD_BOUNDARY, "validate_object: Object not on a boundary.");
    }
}

/**
 * @brief Checks for corruption for an object. In other words, checks if the pad bytes have been changed.
 * 
 * @param object - the object to check corruption at
 * @return whether it found corruption
 */
bool ObjectAllocator::CheckForPaddingCorruption(const unsigned char* object) const
{
    // Get the locations of the pad bytes
    const unsigned char* leftPadding = object - config.PadBytes_;
    const unsigned char* rightPadding = object + stats.ObjectSize_;

    // Go through each pad byte and check if it has been changed
    for (unsigned int i = 0; i < config.PadBytes_; i++)
    {
        // If it has been changed, throw a corruption exception
        if(leftPadding[i] != PAD_PATTERN || rightPadding[i] != PAD_PATTERN)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Makes an allocation with the CPP manager.
 * 
 * @return char* - the allocation made
 */
char* ObjectAllocator::AllocateWithCPPManager()
{
    // Update the statistics
    stats.Allocations_++;
    stats.ObjectsInUse_++;

    if(stats.Allocations_ > stats.MostObjects_)
    {
        stats.MostObjects_ = stats.Allocations_;
    }

    char* cppAllocation;

    // Create the allocation
    try
    {
        cppAllocation = new char[stats.ObjectSize_];
    }
    catch(const std::bad_alloc& e)
    {
        throw OAException(OAException::E_NO_MEMORY, "allocate_cpp_manager: No system memory available.");
    }

    return cppAllocation;
}