#ifndef COMMON_H_
#define COMMON_H_ value

#include <cstdio>
#include <cstdlib>

//--------------------------------------------
// Function: openfile(FILE*, filename, openmode)
// Description: open FILE* to filename with mode: openmode
//	then lock file
//
// Function: closefile(FILE*)
// Description: unlock and close FILE*
//--------------------------------------------
void openfile(FILE* &file, const char* filename,const char* openmode);
void closefile(FILE* &);

//--------------------------------------------
// Function: exception(bool, char)
// Description: if bool is true, output message and terminate program 
//--------------------------------------------
void except(bool condition, const char* err);

#endif /* end of include guard: COMMON_H_ */
