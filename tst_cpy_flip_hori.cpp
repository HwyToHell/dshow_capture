#include "cam_cap_dshow.h"

#include <iostream>

void prnBuffer(const BYTE buf[], int height, int width);

int main (int argc, char* argv[]) {
	using namespace std;

	const BYTE src[] = {17, 18, 19, 27, 28, 29, 117, 118, 119, 127, 128, 129};
	BYTE dst[12];
	int height = 2, width = 2; 

	prnBuffer(src, 2, 6);
	cpyFlipHori(dst, src, 2, 2, 3); 
	
	cout << endl;
	prnBuffer(dst, 2, 6);

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