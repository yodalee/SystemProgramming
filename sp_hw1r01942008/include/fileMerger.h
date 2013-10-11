#ifndef FILEMERGER_H_
#define FILEMERGER_H_ value

#ifdef __cplusplus
extern "C" {
#include <fcntl.h>
#include <unistd.h>
}
#endif

#include <cstdio>
#include <cstring>
#include <vector>
using namespace std;

typedef enum {left, up, upLeft} LCSdirection;
typedef struct {
	LCSdirection LCSdir;
	unsigned int num;
} LCSitem;
typedef struct {
	unsigned int row;
	unsigned int col;
} Point;

//--------------------------------------------
// Function: countLine(FILE*, vector &)
// Description: read fd, count line number in fd, then set fd to beginning
//--------------------------------------------
void countLine(FILE* fd, vector<unsigned int> &);

//--------------------------------------------
// Function: buildLCS(LCSitem*, FILE*, FILE*, vector<unsigned int>, vector<unsigned int>)
// Description: build LCS table, LCSitem* should be initialize first(?)
//--------------------------------------------
void buildLCS(LCSitem*, FILE*, FILE*, const vector<unsigned int>,const vector<unsigned int>);

//--------------------------------------------
// Function: buildStack(LCSitem*, vector<Point>&, unsigned int, unsigned int
// Description: walk back from the last point to 0,0, push 
//  every point on the raod into vector
//--------------------------------------------
void buildStack(LCSitem*, vector<Point>&, unsigned int, unsigned int);

//--------------------------------------------
// Function: comparePos(FILE*, FILE*, unsigned int, unsigned int, unsigned int)
// Description: compare content of two files from offset position, compare 
//--------------------------------------------
bool comparePos(FILE*, FILE*, unsigned int, unsigned int, unsigned int);

//--------------------------------------------
// Function: writePos(FILE*, FILE*, unsigned int, unsigned int)
// Description: copy content length n of one file into another file
//--------------------------------------------
void writePos(FILE*, FILE*, unsigned int offset, unsigned int length);

//--------------------------------------------
// Function: writeLCS(FILE*, FILE*, FILE*, LCSitem*, vector<unsigned int>, vector<unsigned int>)
// Description: write diff files into file descriptor
//--------------------------------------------
void writeLCS(FILE*, FILE*, FILE*, LCSitem*, vector<unsigned int>, vector<unsigned int>);

//--------------------------------------------
// Function: writeLCS2(FILE*, FILE*, FILE*, LCSitem*, LCSitem*, vector<unsigned int>, vector<unsigned int>, vector<unsigned int>)
// Description: write merge diff file into file descriptor
//--------------------------------------------

//--------------------------------------------
// Function: diff2(char*, char*, char*);
// Description: compare two file and write diff to third file
//--------------------------------------------
void diff2(char* ,char* ,char*);

//--------------------------------------------
// Function: diff3(char*, char*, char*, char*);
// Description: compare v1, v2 with origin and write conflict to fourth file
//--------------------------------------------
void diff3(char* ,char* ,char* ,char*);

#endif /* end of include guard: FILEMERGER_H_ */
