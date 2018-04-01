// google-style order
#include "cam_cap_dshow.h"

#include <iomanip>
#include <iostream>
#include <process.h> // threading
#include <string>


DWORD WINAPI showImg(void* p);

void prnTime(std::string message) {
	using namespace std;
	static LARGE_INTEGER lastTime = { 0 } ;
	LARGE_INTEGER freq, newTime;

	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&newTime);

	// absolute time in millisec
	double absTimeInMs = (double)(newTime.QuadPart * 1000) / (double)freq.QuadPart;
	
	// relative time in millisec since last measurement
	double diffTime = 0;
	if (lastTime.QuadPart > 0)
		diffTime = (double)(newTime.QuadPart - lastTime.QuadPart);

	double diffTimeInMs = (diffTime * 1000) / (double)freq.QuadPart;

	lastTime = newTime;

	// output
	int width = 30 - message.size();
	cout << message << setw(width) << setprecision(4);
	cout << "diff: " << diffTimeInMs << " ms" << endl;

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


DWORD WINAPI first_getCam(void* p) {
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


struct CamThreadPara {
	CamInput* camInput;
	HANDLE evFilterRunning;
	HANDLE evDestructCam;
};


DWORD WINAPI getCam(void* p) {
	using namespace std;	

	CamThreadPara* pCamThreadPara = (CamThreadPara*)p;
	pCamThreadPara->camInput = new CamInput;
	CamInput* pCamInput = pCamThreadPara->camInput;

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
	pCamInput->runGraph();

	prnTime("graph started");

	// check if graph running and signal event "filter running"
	HANDLE hEventFilterRunning = pCamThreadPara->evFilterRunning;
	for (int initCnt = 1; initCnt < 11; ++initCnt) {
		cout << initCnt * 100 << " ms: "; 
		if (pCamInput->isGraphRunning()) {
			cout << "graph running" << endl;
			SetEvent(hEventFilterRunning);
			break;
		} else {
			cout << "graph not running yet" << endl;
			delete pCamInput;
			ExitThread(-1);
		}
	}

	prnTime("graph is running");

	// wait for event "destruct cam input"
	HANDLE hEventDestructCam = pCamThreadPara->evDestructCam;
	DWORD dwRet = WaitForSingleObject(hEventDestructCam, INFINITE);
	if (dwRet != WAIT_OBJECT_0) {
		cout << "getCam: WaitForSingleObject failure code: " << hex << dwRet << endl; 
		DWORD dwError = GetLastError();
		cout << "last error: " << hex << dwError << endl;
	}

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


int first_main (int argc, char* argv[]) {
	using namespace std;

	// TODO 
	HANDLE hCamThread = CreateThread(nullptr, 0, getCam, nullptr, 0, 0);

	DWORD dwRet = WaitForSingleObject(hCamThread, INFINITE);

	if (dwRet != WAIT_OBJECT_0)
		cout << "WaitForSingleObject: failure code " << dwRet << endl; 

	cout << "Press <enter> to exit" << endl;
	string str;
	getline(cin, str);
	return 0;
}



int main (int argc, char* argv[]) {
	using namespace std;

	prnTime("main started");

	CamThreadPara camThreadPara;
	CamThreadPara* pCamThreadPara = &camThreadPara;
	camThreadPara.camInput = nullptr;
	camThreadPara.evFilterRunning = CreateEvent(nullptr, true, false, L"filter running");
	camThreadPara.evDestructCam = CreateEvent(nullptr, true, false, L"destruct cam");

	// thread for setting up cam 
	HANDLE hCamThread = CreateThread(nullptr, 0, getCam, pCamThreadPara, 0, 0);

	prnTime("thread 1 created");

	DWORD dwRet = WaitForSingleObject(	pCamThreadPara->evFilterRunning, INFINITE);
	if (dwRet != WAIT_OBJECT_0) {
		cout << "main: filter graph does not run" << dwRet << endl; 

	} else {
		cv::Mat image;
		while (pCamThreadPara->camInput->read(image)) {
			cv::imshow("grabbed image", image);

			if (cv::waitKey(10) == 27) 	{
				std::cout << "ESC pressed -> end video processing" << std::endl;
				break;
			}
		}

	} // end else

	// signal event "destruct cam input"
	SetEvent(camThreadPara.evDestructCam);

	dwRet = WaitForSingleObject(hCamThread, INFINITE);
	if (dwRet != WAIT_OBJECT_0)
		cout << "WaitForSingleObject: failure code " << dwRet << endl; 

	cout << "Press <enter> to exit" << endl;
	string str;
	getline(cin, str);
	return 0;
}