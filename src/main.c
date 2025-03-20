#include <stdio.h>
#include "structures/index_tree.h"

int main()
{
    uint32_t id = 1;
    idxt_Create(4096 /* 4 KB */, 4);
    idxt_AddRecord(&id, 2,3);
    uint32_t id2 = 2;
    idxt_AddRecord(&id2, 2,4);
    uint32_t id4 = 4;
    uint32_t id3 = 3;
    idxt_AddRecord(&id4, 2,5);
    idxt_AddRecord(&id3, 2,6);
    uint32_t id5 = 5;
    idxt_AddRecord(&id5, 2,7);
    idxt_DisplayTree();
}
