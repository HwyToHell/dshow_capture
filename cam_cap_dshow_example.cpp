// google-style order
#include "cam_cap_dshow.h"

#include <iostream>
#include <process.h> // threading
#include <string>

void startStopCtrl(void* p) {
	using namespace std;
	CamInput* pCamInput = (CamInput*)p;
	bool exit = false, running = false;
	string cmd; 
	cout << "press enter to start / stop video, hit e to exit" << endl; 
	while (!exit) {
		getline(cin, cmd);
		if (running) {
			cout << "stop video" << endl;
			if (pCamInput->stopGraph())
				running = false;
			else {
				cout << "stop error" << endl;
				break;
			}
		} else { // not running
			cout << "start video" << endl;
			if (pCamInput->runGraph())
				running = true;
			else {
				cout << "start error" << endl;
				break;
			}
		}
		if (cmd.size() > 0 && cmd.front() == 'e')
			exit = true;
	} // end while

//	_endthread();
}


int main (int argc, char* argv[]) {
	using namespace std;
	CamInput camInput;
	CamInput* pCamInput = &camInput;
	int iDevices = camInput.enumerateDevices();

	// init graph with first device
	bool success = camInput.initGraph(0);

	// check capabilities of capture device in graph
	int iCaps = camInput.enumerateStreamCaps();
	camInput.printStreamCaps();
	double height = 0, width = 0;
	if (camInput.setResolution(6)) {
		height = camInput.get(CV_CAP_PROP_FRAME_WIDTH);
		width = camInput.get(CV_CAP_PROP_FRAME_HEIGHT);
		cout << "actual resolution: " << height << "x" << width << endl;
	} else
		cout << "resolution not set" << endl;

	// dyn array for grabbed images (RGB24)
	BYTE* bitmap;
	size_t bmpSize = (size_t)height * (size_t)width * 3;
	bitmap = new BYTE[bmpSize];

	// read image
		
	// con input for starting and stopping the graph
	startStopCtrl(pCamInput);
	cout << "finished" << endl;


	delete[] bitmap;
	return 0;
}