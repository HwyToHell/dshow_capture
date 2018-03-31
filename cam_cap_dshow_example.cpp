// google-style order
#include "cam_cap_dshow.h"

#include <iostream>
#include <process.h> // threading
#include <string>


DWORD WINAPI showImg(void* p);


void createImg(cv::Scalar color, int width, int height) {
	for (int i = 0; i < width; ++i) 
		;
	return;
}

HANDLE startStopCtrl(void* p) {
	using namespace std;
	CamInput* pCamInput = (CamInput*)p;
	bool exit = false, running = false;
	HANDLE hShowImgThread = nullptr;
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
			//_beginthread(showImg, 0, (void*)pCamInput);
			hShowImgThread = CreateThread(nullptr, 0, showImg, (LPVOID)pCamInput, 0, 0);
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
	return hShowImgThread;
}


DWORD WINAPI getCam(void* p) {
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
	HANDLE hShowImageThread = startStopCtrl(pCamInput);
	cout << "finished" << endl;
	
	DWORD dwRet = WaitForSingleObject(hShowImageThread, INFINITE);
	if (dwRet != WAIT_OBJECT_0)
		cout << "WaitForSingleObject: failure code " << dwRet << endl; 

	delete pCamInput;
	ExitThread(0); // getCam Thread
}


DWORD WINAPI showImg(void* p) {
	CamInput* pCamInput = (CamInput*)p;
	Sleep(2000);
	
	cv::Mat image;
	while (pCamInput->read(image)) {
		//bool succ = pCamInput->read(image);
		cv::imshow("grabbed image", image);
		//cv::imwrite("yyy_grab.jpg", image);

		if (cv::waitKey(10) == 27) 	{
			std::cout << "ESC pressed -> end video processing" << std::endl;
			break;
		}
	}
	ExitThread(0);
	return 0;
}


int main (int argc, char* argv[]) {
	using namespace std;
	/* dyn array for grabbed images (RGB24)
	cv::Mat sgColorImg(16, 4, CV_8UC3, cv::Scalar(255, 255, 0));
	cout << "sgColorImg" << endl;
	cout << sgColorImg << endl << endl;
	cv::imshow("single color image", sgColorImg);

	cv::Mat image = cv::imread("yyy_grab.jpg");
	cv::imshow("grabbed frame", image);
	*/

	// TODO delete _beginthread(getCam, 0, nullptr);
	HANDLE hCamThread = CreateThread(nullptr, 0, getCam, nullptr, 0, 0);
	DWORD dwRet = WaitForSingleObject(hCamThread, INFINITE);

	if (dwRet != WAIT_OBJECT_0)
		cout << "WaitForSingleObject: failure code " << dwRet << endl; 

	cout << "Press <enter> to exit" << endl;
	string str;
	getline(cin, str);
	return 0;
}