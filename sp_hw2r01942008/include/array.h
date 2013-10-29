#ifndef ARRAY_H_
#define ARRAY_H_ value
#include <malloc.h>

typedef struct {
	char path[PATH_MAX];
	struct stat statbuf;
} fileinfo;

typedef struct {
	fileinfo *array;
	size_t used;
	size_t size;
} filearray;

void initArray(filearray *a, size_t initialSize) {
	a->array = (fileinfo*)malloc(initialSize*sizeof(fileinfo));
	a->used = 0;
	a->size = initialSize;
}

int delArray(filearray *a, int idx){
	int i;
	if (idx >= a->used) {
		return -1;
	}
	for (i = idx; i < a->used-1; ++i) {
		a->array[i] = a->array[i+1];
	}
	a->used--;
	if (a->used < a->size/2) {
		a->size /= 2;
		a->array = (fileinfo*)realloc(a->array, a->size*sizeof(fileinfo));
	}
	return 0;
}

void insertArray(filearray *a, fileinfo element){
	if (a->used == a->size) {
		a->size *= 2;
		a->array = (fileinfo*)realloc(a->array, a->size*sizeof(fileinfo));
	}
	a->array[a->used++] = element;
}

void freeArray(filearray *a){
	free(a->array);
	a->array = NULL;
	a->used = a->size = 0;
}

#endif /* end of include guard: ARRAY_H_ */
