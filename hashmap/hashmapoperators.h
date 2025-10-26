#ifndef HASHMAP_H_
#define HASHMAP_H_

#define SIZE 20

struct DataItem {
   char *data;   
   int key;
};

int hashCode(int key);
struct DataItem *search(int key);
void insert(int key, char *data);
struct DataItem* deleteItem(struct DataItem* item);
void display();

#endif