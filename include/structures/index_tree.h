#ifndef _DB2EMU_STRUCTURES_INDEX_TREE_H_
#define _DB2EMU_STRUCTURES_INDEX_TREE_H_

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct IndexPage IndexPage_t;
typedef struct IndexPageEntry IndexPageEntry_t;
typedef struct RecordID RecordID_t;
typedef struct IndexTree IndexTree_t;

struct RecordID
{
    uint32_t page_num;
    uint32_t slot_num;
};

struct IndexPageEntry
{
    bool in_use;
    void *key;
    // Entry may either contain a reference to an underlying index page,
    // or a RecordID. This will be decoded based on page-type, i.e. leaf or non-leaf page.
    void *data;
};

// One page may contain m keys and m entries, with each pair pointing either to another page or to a
// data record(RID).
// Note: Pure B+ has m-1 keys and m children.
struct IndexPage
{
    // Meta-data used to manage entries.
    uint64_t page_id;
    bool is_leaf;
    // Depends on is_leaf.
    uint32_t data_size;
    // Calculated according to page-size.
    uint32_t max_entries;
    // Utilized entries.
    uint32_t num_entries;
    // The optimum way to store the entries is as a contigous array as it will increase performance.
    // Depending on the size of the page, there might be difficulties in allocating and reserving enough memory.
    // This puts a restraint on the user as it will be implemented as an array, and depending on their system,
    // they have to adjust the size of their pages.
    IndexPageEntry_t *entries;
    // In addition to multiple downward pointers, we need one upward-pointer since there are only
    // one parent.
    IndexPage_t *parent;
};

struct IndexTree
{
    // Meta-data.
    uint64_t page_counter;
    // Defined at creation.
    uint32_t key_size;
    // Defined at creation.
    uint32_t page_size;
    // Tree.
    IndexPage_t *root;
};

// Create index-tree with page-size and key-size, both in bytes.
void idxt_Create(uint32_t page_size, uint32_t key_size);

// Tear down and free all the memory used by the index-tree.
bool idxt_Destroy();

// When adding a record to a table we have to add an entry into the index table aswell.
// (Re)organizing the index-tree is handled internally, not visible to the user.
bool idxt_AddRecord(void *key, uint32_t page_num, uint32_t slot_num);

// Used to get a reference to the record in question based on key. 
// If not found, NULL is returned.
RecordID_t *idxt_FindRecord(void *key);

void idxt_DisplayTree();

#endif
