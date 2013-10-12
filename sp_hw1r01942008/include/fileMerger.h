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
	LCSdirection LCSdir;
} Point;

//--------------------------------------------
// Function: diff2(char*, char*, char*);
// Description: compare two file and write diff to third file
//--------------------------------------------
void diff2(char* ,char* ,char*);

//--------------------------------------------
// Function: buildLCS(LCSitem*, FILE*, FILE*, vector<unsigned int>, vector<unsigned int>)
// Description: build LCS table, LCSitem* should be initialize first(?)
//--------------------------------------------
void buildLCS(LCSitem*, FILE*, FILE*, const vector<unsigned int>,const vector<unsigned int>);

//--------------------------------------------
// Function: writeLCS(FILE*, FILE*, FILE*, vector<unsigned int>, vector<unsigned int>)
// Description: write diff files into file descriptor
//--------------------------------------------
void writeLCS(FILE*, FILE*, FILE*, const vector<Point>, const vector<unsigned int>, const vector<unsigned int>);

//--------------------------------------------
// Function: buildStack(LCSitem*, vector<Point>&, unsigned int, unsigned int
// Description: walk back from the last point to 0,0, push 
//  every point on the raod into vector
//--------------------------------------------
void buildStack(LCSitem*,  vector<Point>&, unsigned int, unsigned int);

//--------------------------------------------
// Function: countLine(FILE*, vector &)
// Description: read fd, count line number in fd, then set fd to beginning
//--------------------------------------------
void countLine(FILE* fd, vector<unsigned int> &);

//--------------------------------------------
// Function: findLeftUp
// Description: walk through LCS stack, and find leftUp direction, and return Point by reference
//--------------------------------------------
bool findLeftUp(Point&, vector<Point>&, unsigned int&);

//--------------------------------------------
// Function: comparePos(FILE*, FILE*, unsigned int, unsigned int, unsigned int)
// Description: compare content of two files from offset position, compare 
//--------------------------------------------
bool comparePos(FILE*, FILE*, unsigned int, unsigned int, unsigned int);

//--------------------------------------------
// Function: copyPos(FILE*, FILE*, unsigned int, unsigned int)
// Description: copy content length n of one file into another file
//--------------------------------------------
void copyPos(FILE*, FILE*, unsigned int offset, unsigned int length);

#endif /* end of include guard: FILEMERGER_H_ */
