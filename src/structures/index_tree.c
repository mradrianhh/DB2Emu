#include <stdio.h>
#include <string.h>
#include "index_tree.h"

static IndexTree_t tree;
static int level;

static IndexPage_t *CreateEmptyPage(bool is_leaf, IndexPage_t *parent);
static IndexPage_t *ProcessNonleafPage(IndexPage_t *page, void *key);
static RecordID_t *ProcessLeafPage(IndexPage_t *page, void *key);
static IndexPageEntry_t *InsertLeafPageEntry(IndexPage_t *page, void *key, uint32_t page_num, uint32_t slot_num);
static IndexPageEntry_t *BalanceAndInsertLeafPageEntry(IndexPage_t *page, void *key, uint32_t page_num, uint32_t slot_num);
static IndexPageEntry_t *InsertNonleafPageEntry(IndexPage_t *target, IndexPage_t *source);
static IndexPageEntry_t *GetNonleafPageEntry(IndexPage_t *source, IndexPage_t *target);
static void DisplayPage(IndexPage_t *page);

void idxt_Create(uint32_t page_size, uint32_t key_size)
{
    // Set meta-data and create root.
    tree.page_counter = 0;
    tree.page_size = page_size;
    tree.key_size = key_size;
    // B+-tree algorithm places constraints on the root. It needs atleast two child pages.
    tree.root = CreateEmptyPage(/* is_leaf */ true, /* no parent */ NULL);
}

bool idxt_Destroy()
{
    return false;
}

bool idxt_AddRecord(void *key, uint32_t page_num, uint32_t slot_num)
{
    // Inserting an index for a record into the index tree is complicated.
    // It requires balancing the tree by keeping track of the number of entries in each page,
    // while at the same time ensuring that the keys are stored in sequential order.
    // We have to traverse down the tree to the leaf page where we would like to insert the entry.
    IndexPage_t *current = tree.root;
    while (!current->is_leaf)
    {
        // Get the next page.
        current = ProcessNonleafPage(current, key);
        // We need to check for NULL here in case there is an error in the tree structure.
        if (current == NULL)
        {
            // If current is NULL, it means that the last page was a non-leaf page, and there are no more
            // child pages. In other words, we have a non-leaf page at level 0 and there is a severe error.
            fprintf(stderr, "Error in index tree: non-leaf page at level 0.\n");
            exit(EXIT_FAILURE);
        }
    }
    // We need to check if there is any room.
    // If not, we have to insert a new page with ensuing balancing acts to bite our ass.
    if (current->num_entries < current->max_entries)
    {
        // Yey, there is room! Now we just need to re-organize the entries so we maintain
        // ascending sequential order...
        InsertLeafPageEntry(current, key, page_num, slot_num);
        return true;
    }
    else
    {
        // Fuck...
        BalanceAndInsertLeafPageEntry(current, key, page_num, slot_num);
        return true;
    }
}

RecordID_t *idxt_FindRecord(void *key)
{
    // Starting at the root:
    // 1.  Check if leaf page.
    // 2a. If leaf page, search entries for key. If found, return RecordID, if not, return NULL.
    // 2b. If non-leaf page, walk through the entries and compare the key.
    // 3a. If key is less than the highest key in an entry, go to the child page. Repeat step 1.
    // 3b. Else, go to the next entry. Repeat step 3a.

    // Note: If we reach level 0(no more children), and the page is not a leaf page,
    // there is an error in the tree structure. It should not happen.

    IndexPage_t *current = tree.root;
    // Traverse the three to level 0(the leaf pages).
    while (!current->is_leaf)
    {
        // Get the next page.
        current = ProcessNonleafPage(current, key);
        // We need to check for NULL here in case there is an error in the tree structure.
        if (current == NULL)
        {
            // If current is NULL, it means that the last page was a non-leaf page, and there are no more
            // child pages. In other words, we have a non-leaf page at level 0 and there is a severe error.
            fprintf(stderr, "Error in index tree: non-leaf page at level 0.\n");
            exit(EXIT_FAILURE);
        }
    }

    // After traversing to the correct leaf page, we have to find and return the correct entry.
    return ProcessLeafPage(current, key);
}

void idxt_DisplayTree()
{
    printf("Index tree:\n");
    printf("\t Page size: %d\n", tree.page_size);
    printf("\t Page count: %ld\n", tree.page_counter);
    printf("\t Key size: %d\n", tree.key_size);
    level = 0;
    DisplayPage(tree.root);
}

IndexPage_t *CreateEmptyPage(bool is_leaf, IndexPage_t *parent)
{
    IndexPage_t *page = calloc(1, sizeof(IndexPage_t));
    page->is_leaf = is_leaf;
    page->parent = parent;
    page->page_id = tree.page_counter++;
    // If leaf page, data size is the size of RID, else it's the size of a pointer to
    // another page.
    if (page->is_leaf)
    {
        page->data_size = sizeof(RecordID_t);
    }
    else
    {
        page->data_size = sizeof(IndexPage_t *);
    }
    // We need to calculate the number of entries in the page based on
    // the size of the key, the size of the data, and the page size.
    // page->max_entries = tree.page_size / (tree.key_size + page->data_size);
    page->max_entries = 4;
    // Finally, allocate the entries and set utilized entries to 0.
    page->entries = calloc(page->max_entries, sizeof(IndexPageEntry_t));
    page->num_entries = 0;

    return page;
}

IndexPage_t *ProcessNonleafPage(IndexPage_t *current, void *key)
{
    // A non-leaf page represents a sparse index, meaning that we simply have markers for different
    // intervals of the key. We have to process each entry sequentially, comparing the key with the highest
    // key value of the child page of that entry. If the key is less than the highest key value, it means
    // that the record exists in one of the underlying child pages because keys are stored in sequence from
    // left to right. If all the entries in the non-leaf page have a lower key, it means that the record
    // doesn't exist in the tree.

    for (int i = 0; i < current->num_entries; i++)
    {
        if (memcmp(current->entries[i].key, key, tree.key_size) >= 0)
        {
            return (IndexPage_t *)current->entries[i].data;
        }
    }

    // If we get here, it means that the record doesn't exist. We return NULL(not found).
    return NULL;
}

RecordID_t *ProcessLeafPage(IndexPage_t *current, void *key)
{
    // The entries are sorted in ascending order,
    // so we use binary search.

    // First we set the boundaries and check them.
    uint32_t max = current->num_entries;
    /*if (current->entries[max].key == key)
    {
        // We found the key, return the RecordID.
        return (RecordID_t *)current->entries[max].data;
    }*/
    uint32_t min = 0;
    /*if (current->entries[min].key == key)
    {
        // We found it.
        return (RecordID_t *)current->entries[min].data;
    }*/

    while (max >= min)
    {
        int mid = min + (max - min) / 2;

        int keycmp = memcmp(current->entries[mid].key, key, tree.key_size);
        if (keycmp == 0)
            return (RecordID_t *)current->entries[mid].data;

        if (keycmp < 0)
            min = mid + 1;
        else
            max = mid - 1;
    }

    // If we reach this, no record was found and we return NULL(not found).
    return NULL;
}

IndexPageEntry_t *InsertLeafPageEntry(IndexPage_t *page, void *key, uint32_t page_num, uint32_t slot_num)
{
    // We create the RecordID.
    RecordID_t *rid = calloc(1, sizeof(RecordID_t));
    rid->page_num = page_num;
    rid->slot_num = slot_num;

    // We need to maintain order, so first we need to position ourselves.
    // We either have to reorder the list, or we find an available position.
    int isrt_pos = -1;
    for (int i = 0; i < page->max_entries; i++)
    {
        if (!page->entries[i].in_use || memcmp(key, page->entries[i].key, tree.key_size) < 0)
        {
            isrt_pos = i;
            break;
        }
    }

    // If we find an available position, we simply take it and insert.
    if (!page->entries[isrt_pos].in_use)
    {
        page->entries[isrt_pos].in_use = true;
        // We check if the key and data is initialized first.
        page->entries[isrt_pos].key = calloc(1, tree.key_size);
        page->entries[isrt_pos].data = calloc(1, page->data_size);

        memcpy(page->entries[isrt_pos].key, key, tree.key_size);
        memcpy(page->entries[isrt_pos].data, (void *)rid, page->data_size);
        page->num_entries++;
        return &page->entries[isrt_pos];
    }

    // We have to shift the other entries to the right using memmove and insert.
    memmove(&page->entries[isrt_pos + 1], &page->entries[isrt_pos], sizeof(IndexPageEntry_t) * (page->num_entries - isrt_pos));
    // Then we insert our entry at isrt_pos.
    page->entries[isrt_pos].in_use = true;
    // We need to allocate new key and data fields for this entry.
    page->entries[isrt_pos].key = calloc(1, tree.key_size);
    page->entries[isrt_pos].data = calloc(1, page->data_size);
    memcpy(page->entries[isrt_pos].key, key, tree.key_size);
    memcpy(page->entries[isrt_pos].data, (void *)rid, page->data_size);
    page->num_entries++;
    return &page->entries[isrt_pos];
}

IndexPageEntry_t *BalanceAndInsertLeafPageEntry(IndexPage_t *page, void *key, uint32_t page_num, uint32_t slot_num)
{
    // We create the RecordID.
    RecordID_t *rid = calloc(1, sizeof(RecordID_t));
    rid->page_num = page_num;
    rid->slot_num = slot_num;

    // To balance, we need to know the candidates and order them.
    // We also need a split ratio: how should we divide the entries between the two pages?
    //
    // The algorithm for balancing is the following:
    // 1. Order the entries in ascending sequence.
    // 2. Based on the cut ratio, calculate the position of the split.
    //    If, for example, 10 candidates, and a 50/50 split, then we should split the range 0..4 into the
    //    first page, and range 5..9 into the second page.
    // 3. In the parent, the previous entry pointing to the pre-split table need to be split into two entries:
    //    - The lower-key table with key[4] as highest key(because they are ordered ascending).
    //    - The higher-key table with key[9] as highest key.
    // 4. If the parent table is full also, repeat from step 1.

    // We need room for the existing entries plus the new one.
    IndexPageEntry_t candidates[page->num_entries + 1];
    // We find the isrt_pos of our new entry with respect to sequence.
    int isrt_pos = -1;
    for (int i = 0; i < page->num_entries; i++)
    {
        if (memcmp(key, page->entries[i].key, tree.key_size) < 0)
        {
            isrt_pos = i;
            break;
        }
    }
    // We know, before getting here, that our key is lower than atleast the highest key of this page,
    // so we should almost never see isrt_pos = -1 here. The exception is if this is a single root page.
    if (isrt_pos == -1)
    {
        // If the key is not less than any of the keys present, we insert it right after them.
        isrt_pos = page->num_entries;
        // First we copy all the existing entries.
        memcpy(candidates, page->entries, sizeof(IndexPageEntry_t) * (page->num_entries));
        // Then, we insert our new entry.
        candidates[isrt_pos].in_use = true;
        candidates[isrt_pos].key = calloc(1, tree.key_size);
        candidates[isrt_pos].data = calloc(1, page->data_size);
        memcpy(candidates[isrt_pos].key, key, tree.key_size);
        memcpy(candidates[isrt_pos].data, (void *)rid, page->data_size);
    }
    else
    {
        // We now create our candidates list. All the existing entries up to the isrt_pos is kept where they are,
        // all the entries after the isrt_pos is shifted to the right.
        memcpy(candidates, page->entries, sizeof(IndexPageEntry_t) * isrt_pos);
        // Insert our new entry.
        candidates[isrt_pos].in_use = true;
        page->entries[isrt_pos].key = calloc(1, tree.key_size);
        page->entries[isrt_pos].data = calloc(1, page->data_size);
        memcpy(candidates[isrt_pos].key, key, tree.key_size);
        memcpy(candidates[isrt_pos].data, (void *)rid, page->data_size);
        // Copy the rest.
        memcpy(candidates + isrt_pos + 1, page->entries + isrt_pos, sizeof(IndexPageEntry_t) * (page->num_entries - isrt_pos));
    }

    // We now have our candidates ordered in ascending sequence.
    // We will use a 50/50 left-biased split by default. This means that if we have 9 candidates,
    // 5 will be to the left and 4 will be to the right. Mathematically, we say that we floor
    // the result of the divide, and we add the remainder to the left side.
    int lowpage_range_end = (page->num_entries + 1) / 2 + (page->num_entries + 1) % 2 - 1;
    int highpage_range_end = lowpage_range_end + (page->num_entries + 1) / 2;

    // If we need to create a new leaf page, and there are no parent(non-leaf) page, we need to create one.
    // This occurs if we only have a single root page.
    IndexPageEntry_t *lower_page_entry;
    if (page->parent == NULL)
    {
        tree.root = CreateEmptyPage(false, NULL);
        lower_page_entry = InsertNonleafPageEntry(tree.root, page);
    }
    // We use the existing page as the low page, so we keep the lower key-partition of the candidates
    // in the existing page and update it's entry in the parent.
    // We clear the current entries and copy the lower-part from candidates.
    memset(page->entries, 0, sizeof(IndexPageEntry_t) * (page->num_entries - 1));
    page->num_entries = lowpage_range_end + 1;
    memcpy(page->entries, candidates, sizeof(IndexPageEntry_t) * page->num_entries);
    // Then we get a reference to it's entry in the parent and update it with the new highest key.
    if(!lower_page_entry)
        lower_page_entry = GetNonleafPageEntry(page->parent, page);
    memcpy(lower_page_entry->key, page->entries[page->num_entries - 1].key, tree.key_size);

    // We need to create a new page for the higher key-partition of the candidates.
    IndexPage_t *new_page = CreateEmptyPage(true, page->parent);
    // We first insert the higher-key candidates into it's entries.
    new_page->num_entries = highpage_range_end - lowpage_range_end;
    memcpy(new_page->entries, candidates + lowpage_range_end + 1, sizeof(IndexPageEntry_t) * page->num_entries);
    // Then, we insert it into the parent.
    InsertNonleafPageEntry(page->parent, new_page);

    // TEMP!
    for(int i = 0; i < page->num_entries; i++)
    {
        if(memcmp(key, page->entries[i].key, tree.key_size) == 0)
            return &page->entries[i];
    }

    for(int i = 0; i < new_page->num_entries; i++)
    {
        if(memcmp(key, new_page->entries[i].key, tree.key_size) == 0)
            return &new_page->entries[i];
    }

    return NULL;
}

IndexPageEntry_t *InsertNonleafPageEntry(IndexPage_t *target, IndexPage_t *source)
{
    // First, we get the highest key from the source, which is the highest entry in use.
    void *key = source->entries[source->num_entries - 1].key;

    // We need to maintain order, so first we need to position ourselves.
    // We either have to reorder the list, or we find an available position.
    int isrt_pos = -1;
    for (int i = 0; i < target->max_entries; i++)
    {
        if (!target->entries[i].in_use || memcmp(key, target->entries[i].key, tree.key_size) < 0)
        {
            isrt_pos = i;
            break;
        }
    }

    // If we find an available position, we simply take it and insert.
    if (!target->entries[isrt_pos].in_use)
    {
        target->entries[isrt_pos].in_use = true;
        // Allocate a key and data.
        target->entries[isrt_pos].key = calloc(1, tree.key_size);
        target->entries[isrt_pos].data = calloc(1, target->data_size);
        memcpy(target->entries[isrt_pos].key, key, tree.key_size);
        target->entries[isrt_pos].data = (void *)source;
        target->num_entries++;
        // Insertion was succesfull, so we also update parent pointer in child.
        source->parent = target;
        return &target->entries[isrt_pos];
    }

    // We have to shift the other entries to the right using memmove and insert.
    memmove(&target->entries[isrt_pos + 1], &target->entries[isrt_pos], sizeof(IndexPageEntry_t) * (target->num_entries - isrt_pos));
    // Then we insert our entry at isrt_pos.
    target->entries[isrt_pos].in_use = true;
    // We need to allocate new key and data fields for this entry.
    target->entries[isrt_pos].key = calloc(1, tree.key_size);
    target->entries[isrt_pos].data = calloc(1, target->data_size);
    memcpy(target->entries[isrt_pos].key, key, tree.key_size);
    target->entries[isrt_pos].data = (void *)source;
    target->num_entries++;
    // Insertion was succesfull, so we also update parent pointer in child.
    source->parent = target;
    return &target->entries[isrt_pos];
}

IndexPageEntry_t *GetNonleafPageEntry(IndexPage_t *source, IndexPage_t *target)
{
    // Walk through entries. 
    // If highest key of target matches key of entry, return entry.
    // Else, return NULL(not found).

    void *highest_key_target = target->entries[target->num_entries - 1].key;
    for(int i = 0; i < source->num_entries; i++)
    {
        if(memcmp(highest_key_target, source->entries[i].key, tree.key_size) == 0)
        {
            return &source->entries[i];
        }
    }

    return NULL;
}

void DisplayPage(IndexPage_t *page)
{
    printf("\nLevel %d - Page %ld\n", level, page->page_id);
    printf("\tLeaf: %d\n", page->is_leaf);
    printf("\tNum entries: %d\n", page->num_entries);
    printf("\tMax entries: %d\n", page->max_entries);
    printf("\tData size: %d\n", page->data_size);

    if (page->is_leaf)
    {
        for (int i = 0; i < page->num_entries; i++)
        {
            printf("\tEntry %d:\n", i);
            printf("\t\t-Key: 0x");
            // Print key
            for (int j = tree.key_size - 1; j >= 0; j--)
            {
                printf("%02x", ((uint8_t *)(page->entries[i].key))[j]);
            }
            printf("\n");
            printf("\t\t-Page num: %d\n", ((RecordID_t *)page->entries[i].data)->page_num);
            printf("\t\t-Slot num: %d\n", ((RecordID_t *)page->entries[i].data)->slot_num);
        }
        return;
    }

    for (int i = 0; i < page->num_entries; i++)
    {
        printf("\tEntry %d\n", i);
        printf("\t-Key: 0x");
        // Print key
        for (int j = tree.key_size; j >= 0; j--)
        {
            printf("%02x", ((uint8_t *)(page->entries[i].key))[j]);
        }
        printf("\n");
        printf("\t-Child page-id: %ld\n", ((IndexPage_t *)page->entries[i].data)->page_id);
    }

    for (int i = 0; i < page->num_entries; i++)
    {
        level++;
        DisplayPage((IndexPage_t *)page->entries[i].data);
        level--;
    }
}
