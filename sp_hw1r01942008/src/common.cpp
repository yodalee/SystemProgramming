#include "common.h"

void 
openfile(FILE* &file,const char* filename,const char* openmode) 
{
	file = fopen(filename, openmode);
	except(NULL==file, "Cannot open file!\n");
	flockfile(file);
}

void 
closefile(FILE* &file) 
{
	funlockfile(file);
	fclose(file);
}

void
except(bool condition, const char* err)
{
	if (condition) {
		fprintf(stderr, "(EE): %s", err);
		exit(EXIT_FAILURE);
	}   
}
