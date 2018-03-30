#include "cam_cap_dshow.h"

#include <iostream>
#include <process.h> // threading



void showImg(void* p) {
	CamInput* pCamInput = (CamInput*)p;
	cv::Mat image;

	Sleep(2000);
	while (pCamInput->read(image)) {
		//bool succ = pCamInput->read(image);
		cv::imshow("grabbed image", image);
		//cv::imwrite("yyy_grab.jpg", image);

		if (cv::waitKey(10) == 27) 	{
			std::cout << "ESC pressed -> end video processing" << std::endl;
			break;
		}
	}

	_endthread();
	return;
}

void startStopCtrl(void* p) {
	using namespace std;
	CamInput* pCamInput = (CamInput*)p;
	bool exit = false, running = false;
	string cmd; 
	cout << "press enter to start / stop video, hit e to exit" << endl; 
	while (!exit) {
		getline(cin, cmd);
		if (running) {
			cout << "video stopped" << endl;
			if (pCamInput->stopGraph())
				running = false;
			else {
				cout << "stop error" << endl;
				break;
			}
		} else { // not running
			cout << "video started" << endl;
			_beginthread(showImg, 0, (void*)pCamInput);
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
}


void getCam(void* p) {
	using namespace std;	
	//CamInput camInput;
	//CamInput* pCamInput = &camInput;
	CamInput* pCamInput = new CamInput;

	int iDevices = pCamInput->enumerateDevices();

	// init graph with first device
	bool success = pCamInput->initGraph(0);

	// check capabilities of capture device in graph
	int iCaps = pCamInput->enumerateStreamCaps();
	pCamInput->printStreamCaps();
	double height = 0, width = 0;
	if (pCamInput->setResolution(6)) {
		height = pCamInput->get(CV_CAP_PROP_FRAME_WIDTH);
		width = pCamInput->get(CV_CAP_PROP_FRAME_HEIGHT);
		cout << "actual resolution: " << height << "x" << width << endl;
	} else
		cout << "resolution not set" << endl;
		
	// con input for starting and stopping the graph
	startStopCtrl(pCamInput);
	cout << "finished" << endl;

	delete pCamInput;
	_endthread();
}



int alt_main (int argc, char* argv[]) {
	using namespace std;
	/*// dyn array for grabbed images (RGB24)
	cv::Mat sgColorImg(16, 4, CV_8UC3, cv::Scalar(0, 0, 255));
	cout << "sgColorImg" << endl;
	cout << sgColorImg << endl << endl;
	cv::imshow("single color image", sgColorImg);
	*/
	cv::Mat image = cv::imread("yyy_grab.jpg");
	cv::imshow("grabbed frame", image);

	_beginthread(getCam, 0, nullptr);




	cv::waitKey(0);
	return 0;
}