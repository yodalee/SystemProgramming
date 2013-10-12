#include "fileMerger.h"
#include "common.h"

void 
diff2(char* fname1, char* fname2, char* outname) 
{
	//declare resource
	FILE *file1;
	FILE *file2;
	FILE *outfile;
	LCSitem *table;
	vector<Point> LCSstack;
	vector<unsigned int> linCount1;
	vector<unsigned int> linCount2;

	//initial file descriptor
	openfile(file1, fname1, "r");
	openfile(file2, fname2, "r");
	openfile(outfile, outname, "w");

	//count line, declare LCS resource
	countLine(file1, linCount1);
	countLine(file2, linCount2);
	table = new LCSitem[linCount1.size()*linCount2.size()];
	LCSstack.reserve(linCount1.size()+linCount2.size());
	
	//build LCS table
	buildLCS(table, file1, file2, linCount1, linCount2);
	
	//backtrace LCS table
	buildStack(table, LCSstack, linCount1.size(), linCount2.size());

	//write LCS table
	writeLCS(file1, file2, outfile, LCSstack, linCount1, linCount2); 
	
	//close file descriptor
	closefile(file1);
	closefile(file2);
	closefile(outfile);
}

//--------------------------------------------
// Function: buildLCS
// Description: row file1, column file2
//    1: * a b c d e f g
// 2:
//  *    1 1 1 1 1 1 1 1
//  z    1
//  y    1
//  x    1
//--------------------------------------------
void 
buildLCS(LCSitem* table, FILE* file1, FILE* file2, \
	const vector<unsigned int> linCount1, \
	const vector<unsigned int> linCount2)  
{
	unsigned int colMax = linCount1.size();
	unsigned int rowMax = linCount2.size();
	LCSdirection direct;
	unsigned int num;
	for (unsigned int col = 0; col < colMax; ++col) {
		table[col].LCSdir = left;
		table[col].num = 1;
	}
	for (unsigned int row = 0; row < rowMax; ++row) {
		table[row*colMax].LCSdir = up;
		table[row*colMax].num = 1;
	}
	table[0].LCSdir = upLeft;

	for (unsigned int col = 1; col < colMax; ++col) {
		for (unsigned int row = 1; row < rowMax; ++row) {
			bool equal = true;
			unsigned int len1 = linCount1[col]-linCount1[col-1];
			unsigned int len2 = linCount2[row]-linCount2[row-1];
			if (len1 != len2) {
				equal = false;
			} else {
				equal = comparePos(file1, file2, linCount1[col-1], linCount2[row-1], len1);
			}

			if (equal) {
				direct = upLeft;
				num = table[row*colMax+col-colMax-1].num+1;
			} else {
				if (table[row*colMax+col-1].num >= table[row*colMax+col-colMax].num) {
					direct = left;
					num = table[row*colMax+col-1].num;
				} else {
					direct = up;
					num = table[row*colMax+col-colMax].num;
				}
			}
			table[row*colMax+col].LCSdir = direct;
			table[row*colMax+col].num = num;
		}
	}
}

void writeLCS(FILE* file1, FILE* file2, FILE* outfile, vector<Point> LCSstack, vector<unsigned int> linCount1, vector<unsigned int>linCount2) 
{
	//initial variable
	Point curPoint;
	Point nextPoint;
	unsigned int startPos = 0;

	//walk through LCS table
	curPoint = LCSstack.back();
	startPos = LCSstack.size()-1;
	while (findLeftUp(nextPoint, LCSstack, startPos)) {
		if ((nextPoint.row - curPoint.row) > 1 and (nextPoint.col - curPoint.col) > 1) {
			fprintf(outfile, "%u,%uc%u,%u\n", curPoint.col+1, nextPoint.col-1, curPoint.row+1, nextPoint.row-1);
		} else {
			if (nextPoint.row - curPoint.row > 1)
				fprintf(outfile, "%ua%u,%u\n", curPoint.col, curPoint.row+1, nextPoint.row-1);
			if (nextPoint.col - curPoint.col > 1)
				fprintf(outfile, "%u,%ud%u\n", curPoint.col+1, nextPoint.col-1, curPoint.row);
		}
		curPoint = nextPoint;
	}
}

void 
buildStack(LCSitem* table, vector<Point> &LCSstack, unsigned int colMax, unsigned int rowMax) 
{
	unsigned int row = rowMax-1;
	unsigned int col = colMax-1;
	Point point;
	while (!(row==0 and col==0 )) {
		point.row = row;
		point.col = col;
		point.LCSdir = table[row*colMax+col].LCSdir;
		LCSstack.push_back(point);
		switch(point.LCSdir){
		  case up:
			  --row;
		      break;
		  case left:
			  --col;
			  break;
		  case upLeft:
			  --row;
			  --col;
			  break;
		}
	}
	point.row = row;
	point.col = col;
	point.LCSdir = upLeft;
	LCSstack.push_back(point);
}

void 
countLine(FILE* fd, vector<unsigned int> &lineOffset) 
{
	unsigned int offset = 0;
	char ch;
	fseek(fd, 0L, SEEK_SET);
	lineOffset.clear();
	lineOffset.push_back(0);
	while (EOF != (ch=fgetc(fd))){
		offset++;
		if (ch=='\n'){ lineOffset.push_back(offset); }
	}
	fseek(fd, 0L, SEEK_SET);
}

bool 
findLeftUp(Point &nextPoint, vector<Point> &LCSstack, unsigned int &startPos) 
{
	vector<Point>::const_iterator idx = LCSstack.begin() + startPos;
	if (idx == LCSstack.begin()) {
		return false;
	} else {
		while (idx != LCSstack.begin()) {
			--idx;
			if (idx->LCSdir == upLeft) {
				if (startPos - (idx - LCSstack.begin()) < 3) {
					continue;
				}
				startPos = idx - LCSstack.begin();
				nextPoint.row = idx->row;
				nextPoint.col = idx->col;
				nextPoint.LCSdir = idx->LCSdir;
				return true;
			}
		}
		startPos = 0;
		nextPoint.row = idx->row+1;
		nextPoint.col = idx->col+1;
		nextPoint.LCSdir = upLeft;
		return true;
	}
}

bool 
comparePos(FILE* file1, FILE* file2, unsigned int off1, unsigned int off2, unsigned int len) 
{
	char buf1[BUFSIZ];
	char buf2[BUFSIZ];
	bool equal = true;
	unsigned int readsize = 0;
	fseek(file1, off1, SEEK_SET);
	fseek(file2, off2, SEEK_SET);
	while (len > 0) {
		readsize = (len > BUFSIZ)? BUFSIZ : len;
		fgets(buf1, readsize, file1);
		fgets(buf2, readsize, file2);
		if (strncmp(buf1, buf2, readsize) != 0) {
			return false;
		}
		len -= readsize;
	}
	return equal;
}

void 
copyPos(FILE* file1, FILE* file2, unsigned int off, unsigned int len) 
{
	char buf[BUFSIZ];
	unsigned int readsize = 0;
	fseek(file1, off, SEEK_SET);
	while (len > 0) {
		readsize = (len > BUFSIZ)? BUFSIZ : len;
		fgets(buf, readsize, file1);
		fprintf(file2, "%s", buf);
		len -= readsize;
	}
	fflush(file2);
}
