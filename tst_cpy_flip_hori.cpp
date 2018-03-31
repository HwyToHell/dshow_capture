#include "cam_cap_dshow.h"

#include <iostream>

void prnBuffer(const BYTE buf[], int height, int width);

int main (int argc, char* argv[]) {
	using namespace std;

	const int height = 4, width = 3, depth = 3; 
	const BYTE src[] = { 11, 12, 13, 14, 15, 16, 17, 18, 19,
						 21, 22, 23, 24, 25, 26, 27, 28, 29,
						 31, 32, 33, 34, 35, 36, 37, 38, 39,
						 41, 42, 43, 44, 45, 46, 47, 48, 49 };
	const size_t bufSize = sizeof(src) / sizeof(src[0]);
	BYTE dst[bufSize];

	cout << "buffer before flipping:" << endl;
	prnBuffer(src, height, width * depth);
	cpyFlipHori(dst, src, height, width, depth); 
	
	cout << endl;
	cout << "buffer after flipping:" << endl;
	prnBuffer(dst, height, width * depth);

	cout << "Press <enter> to exit" << endl;
	string str;
	getline(cin, str);
	return 0;
}

void prnBuffer(const BYTE buf[], int height, int width) {
	using namespace std;
	for (int i_row = 0; i_row < height; ++i_row) {
		for (int n_col = 0; n_col < width; ++n_col)
			cout << (int)buf[i_row*width + n_col] << " ";
		cout << endl;
	}
	cout << endl;
	return;
}