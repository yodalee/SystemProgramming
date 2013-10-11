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
	vector<unsigned int> linCount1;
	vector<unsigned int> linCount2;

	//initial file descriptor
	openfile(file1, fname1, "r");
	openfile(file2, fname2, "r");
	openfile(outfile, outname, "w");

	//count line, declare LCS resource
	countLine(file1, linCount1);
	countLine(file2, linCount2);

	//find longest string lenght, as bufsiz
	table = new LCSitem[linCount1.size()*linCount2.size()];
	
	//build LCS table
	buildLCS(table, file1, file2, linCount1, linCount2);

	//write LCS table
	writeLCS(file1, file2, outfile, table, linCount1, linCount2); 

	//close file descriptor
	closefile(file1);
	closefile(file2);
	closefile(outfile);
}

void
diff3(char* fname_orig,char* fname_v1, \
		char* fname_v2,char* outname){
	//declare resource
	FILE *file_orig;
	FILE *file_v1;
	FILE *file_v2;
	FILE *outfile;
	LCSitem *table_1v2;
	LCSitem *table_1v3;
	vector<unsigned int> linCount_orig;
	vector<unsigned int> linCount_v1;
	vector<unsigned int> linCount_v2;

	//initial file descriptor
	openfile(file_orig, fname_orig, "r");
	openfile(file_v1, fname_v1, "r");
	openfile(file_v2, fname_v2, "r");
	openfile(outfile, outname, "w");

	//count line, declare LCS resource
	countLine(file_orig, linCount_orig);
	countLine(file_v1, linCount_v1);
	countLine(file_v2, linCount_v2);
	table_1v2 = new LCSitem[linCount_orig.size()*linCount_v1.size()];
	table_1v3 = new LCSitem[linCount_orig.size()*linCount_v2.size()];

	//build LCS table
	buildLCS(table_1v2, file_orig, file_v1, linCount_orig, linCount_v1);
	buildLCS(table_1v3, file_orig, file_v2, linCount_orig, linCount_v2);

	//write LCS table
	//close file descriptor
	closefile(file_orig);
	closefile(file_v1);
	closefile(file_v2);
	closefile(outfile);
}

void 
writeLCS(FILE* file1, FILE* file2, FILE* outfile, LCSitem* table, vector<unsigned int> linCount1, vector<unsigned int>linCount2) 
{
	//initial variable
	unsigned int colMax = linCount1.size();
	unsigned int row = 0;
	unsigned int col = 0;
	vector<Point> LCSstack;
	Point curPoint;
	LCSdirection curdir = upLeft;
	unsigned int rowStart = 0;
	unsigned int colStart = 0;

	//walk through LCS table
	buildStack(table, LCSstack, linCount1.size(), linCount2.size());

	//start writing 
	while (!LCSstack.empty()) {
		curPoint = LCSstack.back();
		row = curPoint.row;
		col = curPoint.col;
		printf("row: %u, col: %u\n", row, col);
		if (curdir != table[row*colMax+col].LCSdir) {
			if (curdir == up) {
				//write add to outfile from file2
				fprintf(outfile, "%ua%u,%u\n", col, rowStart, row);
				writePos(file2, outfile, linCount2[rowStart-1], linCount2[row] - linCount2[rowStart-1]);
			} else if (curdir == left) {
				//write delete to outfile from file1
				fprintf(outfile, "%u,%ud%u\n", colStart, col, row);
				writePos(file1, outfile, linCount1[colStart-1], linCount1[col] - linCount1[colStart-1]);
			}
			curdir = table[row*colMax+col].LCSdir;
			if (curdir == up) {
				rowStart = row;
			} else if(curdir == left){
				colStart = col;
			}
		}
		LCSstack.pop_back();
	}
	if (curdir == up) {
		//write add to outfile from file2
		fprintf(outfile, "%ua%u,%u\n", col, rowStart, row);
		writePos(file2, outfile, linCount2[rowStart-1], linCount2[row] - linCount2[rowStart-1]);
	} else if (curdir == left) {
		//write delete to outfile from file1
		fprintf(outfile, "%u,%ud%u\n", colStart, col, row);
		writePos(file1, outfile, linCount1[colStart-1], linCount1[col] - linCount1[colStart-1]);
	}
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

void 
buildStack(LCSitem* table, vector<Point> &LCSstack, unsigned int colMax, unsigned int rowMax) 
{
	unsigned int row = rowMax-1;
	unsigned int col = colMax-1;
	Point point;
	while (!(row==0 and col==0 )) {
		point.row = row;
		point.col = col;
		LCSstack.push_back(point);
		switch(table[row*colMax+col].LCSdir){
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
}

bool 
comparePos(FILE* file1, FILE* file2, unsigned int off1, unsigned int off2, unsigned int len) 
{
	char buf1[len];
	char buf2[len];
	fseek(file1, off1, SEEK_SET);
	fseek(file2, off2, SEEK_SET);
	fgets(buf1, len, file1);
	fgets(buf2, len, file2);
	return !strncmp(buf1, buf2, len);
}

void 
writePos(FILE* file1, FILE* file2, unsigned int off, unsigned int len) 
{
	char buf[len];
	fseek(file1, off, SEEK_SET);
	fgets(buf, len, file1);
	fprintf(file2, "%s", buf);
	fflush(file2);
}
