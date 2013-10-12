#include <stdio.h>
#include <stdlib.h>

#include "fileMerger.h"
#include "common.h"

int main(int argc, char** argv) {
	//check argument
	except(argc < 5, "Usage: ./fileMerger [old_path] [new1_path] [new2_path] [output_path]\n");

	//run diff3 
	diff3(argv[1], argv[2], argv[3], argv[4]);
	return 0;
}
