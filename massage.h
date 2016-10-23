#ifndef __MASSAGE_H__
#define __MASSAGE_H__

void defrag(int alloc_timer);
int exhaust(std::vector<struct ion_data *> &chunks, int min_bytes, bool mmap = true);

#endif
